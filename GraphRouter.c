/*
 * GraphIPC
 * Copyright (C) 2017  John Vidler (john@johnvidler.co.uk)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <assert.h>
#include "lib/GraphNetwork.h"
#include "lib/RingBuffer.h"
#include "lib/utility.h"
#include <glib-2.0/glib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <getopt.h>

#define MAX_BUFFER_SIZE   4096
#define MAX_INPUT_BUFFER  1024
#define COMMAND_PIPE_PATH "/tmp/GraphRouter.pipe"

struct _configuration {
    int network_mtu;
};

struct _configuration config;

typedef struct client_context {
    gnw_address_t address;
    pthread_t * thread_state;
    int socket_fd;
    RingBuffer_t * rx_buffer;
    int state;
    uint64_t bytes_in;
    uint64_t bytes_out;

    int link_policy;
    struct client_context * links[GNW_MAX_LINKS];
    int roundrobin_index;
} client_context_t;

GList * client_list = NULL;
pthread_mutex_t client_list_mutex;

volatile uint64_t nextAddress = 1;

int getListenSocket( struct addrinfo * hints ) {
    int ret;
    struct addrinfo *server_info;

    if((ret = getaddrinfo(NULL, ROUTER_PORT, hints, &server_info )) != 0 ) {
        fprintf( stderr, "Unable to get local NIC details: %s\n", gai_strerror(ret) );
        return -1;
    }

    if( server_info == NULL ) {
        fprintf( stderr, "Failed to bind to any interface" );
        return -1;
    }

    int yes = 1;
    int sockfd = -1;
    struct addrinfo * p;
    for( p = server_info; p != NULL; p = p->ai_next ) {
        if( (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1 ) {
            perror( "Server: socket" );
            continue;
        }

        if( (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1 ) {
            perror( "setsockopt" );
            sockfd = -1;
            break;
        }

        if( bind(sockfd, p->ai_addr, p->ai_addrlen) == -1 ) {
            close( sockfd );
            perror( "Server: bind" );
            continue;
        }
    }

    freeaddrinfo( server_info );

    return sockfd;
}

void emitStatistics( FILE * stream ) {
    fprintf( stream, "UID\tState\tIn\tOut\tPolicy\t\tLinks\n" );
    pthread_mutex_lock( &client_list_mutex );
    GList * iter = client_list;
    while( iter != NULL ) {
        client_context_t * context = (client_context_t *)iter->data;

        // Just hide zombie processes for now - this should be cleaned up properly at some point...
        if( context->state != GNW_STATE_ZOMBIE ) {
            char state_str[32] = {0};
            switch (context->state) {
                case GNW_STATE_OPEN:
                    sprintf(state_str, "OPEN");
                    break;
                case GNW_STATE_RUN:
                    sprintf(state_str, "RUN");
                    break;
                case GNW_STATE_CLOSE:
                    sprintf(state_str, "CLOSE");
                    break;
                default:
                    sprintf(state_str, "???");
            }

            char policy_str[32] = {0};
            switch (context->link_policy) {
                case GNW_BROADCAST:
                    sprintf(policy_str, "BROADCAST");
                    break;
                case GNW_ANYCAST:
                    sprintf(policy_str, "ANYCAST");
                    break;
                case GNW_ROUNDROBIN:
                    sprintf(policy_str, "ROUNDROBIN");
                    break;
                default:
                    sprintf(policy_str, "???");
            }

            char * ref = NULL;
            fmt_humanSize( context->bytes_out, ref );

            fprintf( stream, "%llx\t%s\t%lluB\t%lluB\t%s\t{ ",
                     (unsigned long long int) context->address,
                     state_str,
                     (unsigned long long int) context->bytes_in,
                     (unsigned long long int) context->bytes_out,
                     policy_str);

            for (int i = 0; i < GNW_MAX_LINKS; i++) {
                if (context->links[i] != NULL)
                    fprintf( stream, "%llu ", context->links[i]->address );
            }
            fprintf( stream, "}\n" );

            if (context->state == GNW_STATE_CLOSE)
                context->state = GNW_STATE_ZOMBIE;
        }

        iter = iter->next;
    }
    pthread_mutex_unlock( &client_list_mutex );
    fprintf( stream, "\n" );
}

void context_cleanup( client_context_t * context ) {
    context->state = GNW_STATE_CLOSE;
    close( context->socket_fd );
    ringbuffer_destroy( context->rx_buffer );

    free( context );
}

void * clientProcess( void * _context ) {
    client_context_t * context = (client_context_t *)_context;
    context->state = GNW_STATE_OPEN;

    unsigned char clientAddress[20] = { 0 };
    gnw_format_address( clientAddress, context->address );

    printf( "%s\tState: OPEN\n", clientAddress );

    // Fire up our ring buffer
    context->rx_buffer = ringbuffer_init( config.network_mtu );

    // Probably should be part of the thread context structure, but here for now...
    gnw_state_t parser_context = { .state = 0 };

    printf( "Client poll loop running...\n" );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 1 );

    watch_fd[0].fd = context->socket_fd;
    watch_fd[0].events = POLLIN;

    unsigned char iBuffer[config.network_mtu];

    int logout_timeout = 10;
    ssize_t bytes = 1;
    while( bytes > 0 ) {
        int rv = poll( watch_fd, 1, 1000 );

        // Wait error, drop back to callee
        if( rv == -1 ) {
            fprintf( stderr, "IO Error, dropping client %s.\n", clientAddress );
            context_cleanup( context );
            return NULL;
        }

        // Timeout...
        if( rv == 0 ) {
            printf( "Timeout (State: %d)\n", context->state );
            if( context->state == GNW_STATE_OPEN ) {
                if (logout_timeout-- < 0) {
                    printf( "Client %s timed out, dropping them.\n", clientAddress );
                    context_cleanup( context );
                    return NULL;
                }

                gnw_emitCommandPacket( context->socket_fd, GNW_DATA, NULL, 0 ); // Poke the client with a null data packet
            }

            continue;
        }
        logout_timeout = 10; // Timeout reset, we have data.

        memset( iBuffer, 0, config.network_mtu ); // Here purely for sanity, remove for speeeeeed
        bytes = read( context->socket_fd, iBuffer, config.network_mtu );
        if( ringbuffer_write( context->rx_buffer, iBuffer, bytes ) != bytes )
            fprintf( stderr, "Buffer overflow! %d Bytes lost!\n", bytes );  // Hopefully this won't happen, as we more aggressively parse than fill the buffer

        // May as well re-use the input buffer, we've already copied the contents to the ring buffer
        while( gnw_nextPacket( context->rx_buffer, &parser_context, iBuffer ) ) {
            gnw_header_t * header = (gnw_header_t *)iBuffer;
            unsigned char * payload = (unsigned char *)( iBuffer + sizeof(gnw_header_t) );

            switch( header->type ) {
                case GNW_COMMAND:
                    if( header->length < 1 ) {
                        fprintf( stderr, "%s\tClient sent a command with no operator, skipping.\n", clientAddress );
                        break;
                    }

                    // Select based on the first byte -> The operator
                    switch( *payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            fprintf( stdout, "%s\tAddress request.\n", clientAddress );

                            unsigned char replyBuffer[ sizeof(gnw_address_t) + 1 ];
                            replyBuffer[0] = GNW_CMD_NEW_ADDRESS;
                            *((gnw_address_t *)(replyBuffer+1)) = context->address;
                            gnw_emitCommandPacket( context->socket_fd, GNW_COMMAND | GNW_REPLY, replyBuffer, sizeof(gnw_address_t) );
                            break;

                        default:
                            fprintf( stderr, "%s\tUnknown command, skipped.\n", clientAddress );
                            break;
                    }

                    break;

                default:
                    fprintf( stderr, "%s\tBad response from client, skipping.\n", clientAddress );
                    break;
            }
        }


    }

    printf( "%s\nState: CLOSE.\n", clientAddress );
    context_cleanup( context );
    return NULL;
}

void * commandProcess( void * none ) {
    // Set up the command pipe
    int cmd_fd;
    mkfifo( COMMAND_PIPE_PATH, 0666 );
    cmd_fd = open( COMMAND_PIPE_PATH, O_RDWR | O_CREAT ); // ToDo: Close the command pipe when the router is shut down!
    FILE * cmd_file = fdopen( cmd_fd, "w+" );

    printf( "Waiting for commands...\n" );
    char buffer[1024] = { 0 };
    while( 1 ) {
        memset( buffer, 0, 1024 );
        ssize_t bytesRead = read( cmd_fd, buffer, 1024 );

        if( bytesRead == -1 )
            break;

        if( strncmp( buffer, "status", 6 ) == 0 ) {
            printf( "Status request\n" );

            emitStatistics( cmd_file );
            fprintf( cmd_file, "EOL\n" );
            fflush( cmd_file );
        } else {
            fprintf( cmd_file, "No such command. Did nothing.\n" );
        }

        emitStatistics( stdout );

    }

    printf( "Command process shut down, bye!\n" );
    unlink( COMMAND_PIPE_PATH );

    return NULL;
}

int router_process() {

    // Set up the socket server
    struct addrinfo listen_hints;
    int ret;

    memset( &listen_hints, 0, sizeof listen_hints );
    listen_hints.ai_family   = AF_UNSPEC;
    listen_hints.ai_socktype = SOCK_STREAM;
    listen_hints.ai_flags    = AI_PASSIVE;

    int listen_fd = getListenSocket( &listen_hints );

    if( listen( listen_fd, ROUTER_BACKLOG ) == -1 ) {
        perror( "listen" );
        exit( 1 );
    }

    pthread_mutex_init( &client_list_mutex, NULL );

    pthread_t command_context;
    pthread_create( &command_context, NULL, commandProcess, NULL );

    while( 1 ) {
        struct sockaddr_storage remote_socket;
        socklen_t newSock_len = sizeof( socklen_t );
        int remote_fd = accept( listen_fd, (struct sockaddr *)&remote_socket, &newSock_len );
        if( remote_fd == -1 ) {
            perror( "accept" );
            continue;
        }

        client_context_t * new_context = (client_context_t *)malloc( sizeof(client_context_t) );
        memset( new_context, 0, sizeof( client_context_t ) );
        new_context->thread_state = (pthread_t *)malloc( sizeof(pthread_t) );
        new_context->socket_fd = remote_fd;
        new_context->address = nextAddress++;
        new_context->link_policy = GNW_BROADCAST; // Default to broadcast

        for( int i=0; i<GNW_MAX_LINKS; i++ )
            new_context->links[i] = NULL;

        int result = pthread_create( new_context->thread_state, NULL, clientProcess, new_context );
        assert( !result );
        pthread_detach( *new_context->thread_state ); // Release the thread, so it auto cleans on exit without a join()
        pthread_mutex_lock( &client_list_mutex );
        client_list = g_list_prepend( client_list, new_context );
        pthread_mutex_unlock( &client_list_mutex );

        //printf( "%d active threads...\n", g_list_length( client_list ) );
    }

    if( listen_fd != -1 )
        close( listen_fd );

    return EXIT_SUCCESS;
}

#define ARG_STATUS     0
#define ARG_POLICY     1
#define ARG_CONNECT    2
#define ARG_DISCONNECT 3

int main(int argc, char ** argv ) {

    config.network_mtu = getIFaceMTU( "lo" );

    if( config.network_mtu == -1 ) {
        fprintf( stderr, "Unable to query the local interface MTU, defaulting to 4k\n" );
        //config.network_mtu = 4096;
        config.network_mtu = 4096 * 20;
    }

    struct option longOptions[2] = { 0 };
    longOptions[ARG_STATUS].name    = "status";
    longOptions[ARG_STATUS].has_arg = no_argument;
    longOptions[ARG_STATUS].flag    = NULL;

    // Argument Parsing //
    int arg;
    int indexPtr = 0;
    while( (arg = getopt_long( argc, argv, "", longOptions, &indexPtr )) != -1 ) {
        switch( arg ) {
            case ARG_STATUS: {
                int cmd_fd = open( COMMAND_PIPE_PATH, O_RDWR );
                FILE * cmd_file = fdopen( cmd_fd, "w+" );

                printf( ">>> status\n" );
                fprintf( cmd_file, "status\n" );
                fflush( cmd_file );

                char buffer[1024] = { 0 };
                while( 1 ) {
                    memset( buffer, 0, 1024 );
                    ssize_t bytesRead = read( cmd_fd, buffer, 1024 );

                    if (bytesRead == -1)
                        break;

                    if( bytesRead > 0 ) {
                        printf("<<<\n");

                        if (buffer + bytesRead - 4 > 0 && strncmp(buffer + bytesRead - 4, "EOL\n", 4) == 0)
                            break;

                        printf("[%d] %s\n", bytesRead, buffer);
                    }
                }

                close( cmd_fd );
                return EXIT_SUCCESS;
            }

            case ARG_POLICY:
                return EXIT_FAILURE;

            case ARG_CONNECT:
                return EXIT_FAILURE;

            case ARG_DISCONNECT:
                return EXIT_FAILURE;

            default:
                fprintf( stderr, "Bad command combination. STOP." );
                return EXIT_FAILURE;
        }
    }

    return router_process();
}