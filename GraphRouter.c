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
#include "GraphNetwork.h"
#include <glib-2.0/glib.h>
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>

#define COMMAND_PIPE_PATH "/tmp/GraphRouter.pipe"

typedef struct client_context {
    uint64_t address;
    pthread_t * thread_state;
    int socket_fd;
    int state;
    uint64_t bytes_in;
    uint64_t bytes_out;

    int link_policy;
    struct client_context * links[GNW_MAX_LINKS];
    int roundrobin_index;
} client_context_t;

GList * client_list = NULL;
pthread_mutex_t client_list_mutex;

volatile uint64_t cleanup = 0;
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

const char * const fmt_sizeStr[6] = { "B", "KB", "MB", "GB", "TB", "PB" };
unsigned long long fmt_humanSize( unsigned long long size, char * unitRef ) {
    unsigned long long mul = 0;
    while( size > 1024 ) {
        size = size / 1024;
        printf( "Size: %llu %s\n", size, fmt_sizeStr[mul] );
        mul++;
    }
    return size;
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
                case GNW_STATE_SETUP:
                    sprintf(state_str, "SETUP");
                    break;
                case GNW_STATE_RUN:
                    sprintf(state_str, "RUN");
                    break;
                case GNW_STATE_COMMAND:
                    sprintf(state_str, "CMD");
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

void * clientProcess( void * _context ) {
    client_context_t * context = (client_context_t *)_context;
    context->state = GNW_STATE_OPEN;

    char clientAddress[20] = { 0 };
    gnw_format_address( clientAddress, context->address );

    printf( "%s\tState: OPEN\n", clientAddress );

    // Say hello to the client
    gnw_emitCommandPacket( context->socket_fd, GNW_ACK, NULL, 0 );

    // Wait for instructions...
    char buffer[128] = { 0 };
    while( context->state != GNW_STATE_RUN ) {
        memset( buffer, 0, 128 );
        ssize_t length = gnw_wait( context->socket_fd, GNW_COMMAND, buffer, 128 );

        if( length < 1 ) {
            fprintf( stderr, "%s\tIO error, dropping client.\n", clientAddress );
            context->state = GNW_STATE_CLOSE;
            close( context->socket_fd );
            cleanup++;
            return NULL;
        }
        gnw_header_t * header = (gnw_header_t *)buffer;
        char * data = buffer+sizeof( gnw_header_t );
        if( header->type == GNW_COMMAND ) {
            printf( "Command packet\n" );

            if( length - sizeof( gnw_header_t ) <= 0 ) {
                close( context->socket_fd );
                context->state = GNW_STATE_CLOSE;
                printf( "%s\tBad state, sent command with no payload? Dropped client.\n", clientAddress );
                cleanup++;
                return NULL;
            }

            switch( *data ) {
                case GNW_CMD_NEW_ADDRESS:
                    gnw_emitCommandPacket( context->socket_fd, GNW_ACK, (char *)&(context->address), 8 );
                    gnw_format_address( clientAddress, context->address );

                    printf( "%s\tState:RUN\n", clientAddress );

                    context->state = GNW_STATE_RUN;
                    break;

                default:
                    fprintf( stderr, "Unknown command type [%d], ignored.\n", *data );
            }
        }
    }

    /*const char * tempBuffer = "Contrary to popular belief, Lorem Ipsum is not simply random text. It has roots in a piece of classical Latin literature from 45 BC, making it over 2000 years old. Richard McClintock, a Latin professor at Hampden-Sydney College in Virginia, looked up one of the more obscure Latin words, consectetur, from a Lorem Ipsum passage, and going through the cites of the word in classical literature, discovered the undoubtable source. Lorem Ipsum comes from sections 1.10.32 and 1.10.33 of \"de Finibus Bonorum et Malorum\" (The Extremes of Good and Evil) by Cicero, written in 45 BC. This book is a treatise on the theory of ethics, very popular during the Renaissance. The first line of Lorem Ipsum, \"Lorem ipsum dolor sit amet..\", comes from a line in section 1.10.32.";
    write( remote_fd, tempBuffer, strlen(tempBuffer) );*/

    if( context->state == GNW_STATE_RUN ) {
        ssize_t bytesRead = 1;
        while (bytesRead > 0) {
            char buffer[1024] = {0};
            bytesRead = read(context->socket_fd, buffer, 1024);
            context->bytes_in += bytesRead;

            //gnw_dumpPacket( stdout, buffer, bytesRead );
            //printf( "\n" );

            // Apply the forwarding policy
            switch( context->link_policy ) {
                case GNW_BROADCAST:
                    for( int i=0; i<GNW_MAX_LINKS; i++ ) {
                        if( context->links[i] != NULL ) {
                            ssize_t bytesWritten = write( context->links[i]->socket_fd, buffer, (size_t)bytesRead );
                            context->links[i]->bytes_out += bytesWritten;
                        }
                    }
                    break;

                case GNW_ANYCAST:
                    fprintf( stderr, "Anycast policy not currently supported! Skipped.\n" );
                    break;

                case GNW_ROUNDROBIN:
                    fprintf( stderr, "Round-Robin policy not currently supported! Skipped.\n" );
                    break;

                default:
                    fprintf( stderr, "Unsupported or bad link policy! Skipped.\n" );
            }
        }
    }

    close( context->socket_fd );
    context->state = GNW_STATE_CLOSE;
    printf( "%s\nState: CLOSE.\n", clientAddress );
    cleanup++;
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

            for( int i=0; i<100; i++ ) {
                fprintf(cmd_file, "???\n");
                fflush(cmd_file);
            }

            emitStatistics( cmd_file );
            fflush( cmd_file );
        } else {
            fprintf( cmd_file, "No such command. Did nothing.\n" );
        }

    }

    printf( "Command process shut down, bye!\n" );
    unlink( COMMAND_PIPE_PATH );

    return NULL;
}

void * statisticsProcess(void *none) {
    while (1) {
        emitStatistics( stdout );

        sleep( 10 );
    }
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

    pthread_t stat_context;
    pthread_create( &stat_context, NULL, statisticsProcess, NULL );

    pthread_t command_context;
    pthread_create( &command_context, NULL, commandProcess, NULL );

    client_context_t * old_context = NULL;

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

        if( old_context != NULL )
            new_context->links[0] = old_context;

        int result = pthread_create( new_context->thread_state, NULL, clientProcess, new_context );
        assert( !result );
        pthread_mutex_lock( &client_list_mutex );
        client_list = g_list_prepend( client_list, new_context );
        pthread_mutex_unlock( &client_list_mutex );

        old_context = new_context;

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

                printf( ">>>\n" );
                fprintf( cmd_file, "status\n" );
                fflush( cmd_file );

                printf( "<<<\n" );
                char buffer[1024] = { 0 };
                while( 1 ) {
                    memset( buffer, 0, 1024 );
                    ssize_t bytesRead = read( cmd_fd, buffer, 1024 );

                    if( bytesRead == -1 )
                        break;

                    if( buffer+bytesRead-4 > 0 && strncmp( buffer+bytesRead-4, "EOF\n", 4 ) == 0 )
                        break;

                    fprintf( cmd_file, "%s\n", buffer );
                    fflush( cmd_file );

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