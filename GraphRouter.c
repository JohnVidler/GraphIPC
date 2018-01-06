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
#include "lib/GraphNetwork.h"
#include "lib/utility.h"
#include "lib/Assert.h"
#include "common.h"
#include "lib/LinkedList.h"
#include <poll.h>
#include <sys/un.h>
#include <getopt.h>

#define SYSTEM_ACTIVE 1
#define SYSTEM_STOP   0

#define MAX_BUFFER_SIZE   4096
#define MAX_INPUT_BUFFER  1024
#define COMMAND_PIPE_PATH "/tmp/GraphRouter.pipe"

struct _configuration {
    int network_mtu;
    int system_state;
};

volatile struct _configuration config;

typedef struct client_context {
    gnw_address_t address;
    pthread_t * thread_state;
    int socket_fd;
    RingBuffer_t * rx_buffer;
    int state;
    uint64_t packets_in;
    uint64_t packets_out;
    uint64_t bytes_in;
    uint64_t bytes_out;

    int link_policy;
    struct client_context * links[GNW_MAX_LINKS];
    int roundrobin_index;
} client_context_t;

linked_list_t * client_list = NULL;
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
    fprintf( stream, "UID          | State     | In            | Out           | Policy    | Links\n" );
    pthread_mutex_lock( &client_list_mutex );

    struct list_element *iter;
    for (iter = client_list->head; iter != NULL; iter = iter->next) {
        client_context_t * context = (client_context_t *)iter->data;

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
                case GNW_STATE_ZOMBIE:
                    sprintf(state_str, "ZOMBIE");
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

            char *out_suffix = NULL;
            double scaled_bytes_out = fmt_iec_size(context->bytes_out, &out_suffix);

            char *in_suffix = NULL;
            double scaled_bytes_in = fmt_iec_size(context->bytes_in, &in_suffix);

            fprintf(stream, "%012lx |%10s |%10.2f %3s |%10.2f %3s |%10s |",
                    context->address,
                    state_str,
                    scaled_bytes_in,
                    in_suffix,
                    scaled_bytes_out,
                    out_suffix,
                    policy_str);

            for (int i = 0; i < GNW_MAX_LINKS; i++) {
                if (context->links[i] != NULL)
                    fprintf(stream, "%lu ", context->links[i]->address);
            }
            fprintf(stream, "\n");
        }
    }
    pthread_mutex_unlock( &client_list_mutex );
    fprintf( stream, "\n" );
}

client_context_t * findByAddress( gnw_address_t address ) {
    pthread_mutex_lock(&client_list_mutex);
    bool done = false;
    struct list_element *iter = client_list->head;
    while (iter != NULL && !done) {
        client_context_t *context = (client_context_t *) iter->data;

        if (context->address == address) {
            done = true;
            break;
        }
        iter = iter->next;
    }
    if( iter == NULL ) {
        pthread_mutex_unlock(&client_list_mutex);
        return NULL;
    }

    client_context_t * data_ptr = iter->data;
    pthread_mutex_unlock(&client_list_mutex);

    return data_ptr;
}

void context_cleanup( client_context_t * context ) {
    context->state = GNW_STATE_CLOSE;
    close( context->socket_fd );
    ringbuffer_destroy( context->rx_buffer );

    // Become zombie, ask to be cleaned up.
    context->state = GNW_STATE_ZOMBIE;
}

void * clientProcess( void * _context ) {
    client_context_t * context = (client_context_t *)_context;
    context->state = GNW_STATE_OPEN;

    unsigned char clientAddress[20] = { 0 };
    gnw_format_address( clientAddress, context->address );

    printf( "%s\tState: OPEN\n", clientAddress );

    // Fire up our ring buffer
    context->rx_buffer = ringbuffer_init( config.network_mtu * 100 ); // 20 packets of MTU-size

    // Probably should be part of the thread context structure, but here for now...
    gnw_state_t parser_context = { .state = 0 };

    printf( "Client poll loop running...\n" );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 1 );

    watch_fd[0].fd = context->socket_fd;
    watch_fd[0].events = POLLIN;

    unsigned char latchBuffer[config.network_mtu];
    unsigned char iBuffer[config.network_mtu];

    bool read_back_off = false;
    int tick_rate = 1000;
    int logout_timeout = 10;
    ssize_t bytes = 1;
    while( bytes > 0 ) {
        int rv = poll( watch_fd, 1, tick_rate );

        // Wait error, drop back to callee
        if( rv == -1 ) {
            fprintf( stderr, "IO Error, dropping client %s.\n", clientAddress );
            context_cleanup( context );
            return NULL;
        }

        // Timeout...
        if( rv == 0 ) {
            if( context->state == GNW_STATE_OPEN ) {
                if (logout_timeout-- < 0) {
                    fprintf( stdout, "Client %s timed out, dropping them.\n", clientAddress );
                    context_cleanup( context );
                    return NULL;
                }

                gnw_emitCommandPacket( context->socket_fd, GNW_DATA, NULL, 0 ); // Poke the client with a null data packet
            }

            continue;
        }
        logout_timeout = 10; // Timeout reset, we have data.

        // Only read if we're not backing off, otherwise we'll trash the buffer
        if( read_back_off == false ) {
            memset( latchBuffer, 0, config.network_mtu ); // Here purely for sanity, remove for speeeeeed
            bytes = read(context->socket_fd, latchBuffer, config.network_mtu);
            context->bytes_in += bytes;
            context->packets_in++;
        }

        // Attempt (or re-attempt) to push to the ring, backing off as required.
        if( ringbuffer_write( context->rx_buffer, latchBuffer, bytes ) != bytes ) {
            fprintf(stderr, "Buffer overflow! Read backoff enabled, trying to drain the buffer...\n" );
            read_back_off = true;
        } else {
            read_back_off = false;
        }

        while( gnw_nextPacket( context->rx_buffer, &parser_context, iBuffer ) ) {
            gnw_header_t *  packet_header  = (gnw_header_t *)iBuffer;
            uint32_t        packet_length  = sizeof( gnw_header_t ) + packet_header->length;
            unsigned char * packet_payload = (unsigned char *)( iBuffer + sizeof(gnw_header_t) );

            switch( packet_header->type ) {
                case GNW_DATA:

                    switch( context->link_policy ) {
                        case GNW_BROADCAST:
                            for( int i=0; i<GNW_MAX_LINKS; i++ ) {
                                if( context->links[i] != NULL ) {
                                    gnw_emitDataPacket(context->links[i]->socket_fd, packet_payload, packet_header->length);
                                    context->bytes_out += packet_header->length;
                                    context->packets_out++;
                                    context->links[i]->bytes_in += packet_header->length;
                                    context->links[i]->packets_in++;
                                }
                            }
                            break;

                        case GNW_ANYCAST: {
                            int offset = rand() % GNW_MAX_LINKS; // FIXME: This looks super non-uniform distribution! Is there a de-facto version of Anycast selection?
                            while (context->links[offset] == NULL) {
                                offset = (offset + 1) % GNW_MAX_LINKS;
                            }

                            gnw_emitDataPacket(context->links[offset]->socket_fd, packet_payload, packet_header->length);
                            context->bytes_out += packet_header->length;
                            context->packets_out++;
                            context->links[offset]->bytes_in += packet_header->length;
                            context->links[offset]->packets_in++;

                            break;
                        }

                        case GNW_ROUNDROBIN:
                            context->roundrobin_index = (context->roundrobin_index + 1) % GNW_MAX_LINKS;
                            while( context->links[context->roundrobin_index] == NULL )
                                context->roundrobin_index = (context->roundrobin_index + 1) % GNW_MAX_LINKS;

                            gnw_emitDataPacket(context->links[context->roundrobin_index]->socket_fd, packet_payload, packet_header->length);
                            context->bytes_out += packet_header->length;
                            context->packets_out++;
                            context->links[context->roundrobin_index]->bytes_in += packet_header->length;
                            context->links[context->roundrobin_index]->packets_in++;

                            break;
                    }

                    break;

                case GNW_COMMAND:
                    if( packet_header->length < 1 ) {
                        fprintf( stderr, "%s\tClient sent a command with no operator, skipping.\n", clientAddress );
                        break;
                    }

                    // Select based on the first byte -> The operator
                    switch( *packet_payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            fprintf( stdout, "%s\tAddress request.\n", clientAddress );

                            unsigned char replyBuffer[ sizeof(gnw_address_t) + 1 ];
                            replyBuffer[0] = GNW_CMD_NEW_ADDRESS;
                            *((gnw_address_t *)(replyBuffer+1)) = context->address;
                            gnw_emitCommandPacket( context->socket_fd, GNW_COMMAND | GNW_REPLY, replyBuffer, sizeof(gnw_address_t) );

                            // Going live (run state)
                            context->state = GNW_STATE_RUN;
                            fprintf( stdout, "%s\tClient going to RUN state, will not time out.\n", clientAddress );
                            tick_rate = 10000; // Now tick at 10 second intervals, just so we don't spin unnecessarily
                            break;

                        case GNW_CMD_STATUS:
                            fprintf( stdout, "%s\tStatus request.\n", clientAddress );

                            emitStatistics( stdout );

                            break;

                        case GNW_CMD_POLICY: {
                            fprintf(stdout, "%s\tNode policy change.\n", clientAddress);

                            unsigned char newPolicy = *(packet_payload + 1);
                            gnw_address_t target = *(gnw_address_t *) (packet_payload + 2);

                            if( target < 1 ) {
                                fprintf( stderr, "No endpoint can ever be address zero!\n" );
                                break;
                            }

                            client_context_t * node = findByAddress( target );
                            if( node == NULL ) {
                                fprintf( stderr, "No such valid address -> %lx\n", target );
                                break;
                            }
                            fprintf(stdout, "%lx policy changed from %d to %d\n", target, node->link_policy,
                                    newPolicy);
                            node->link_policy = newPolicy;

                            break;
                        }


                        case GNW_CMD_CONNECT:
                            fprintf( stdout, "%s\tNode connect request.\n", clientAddress );

                            gnw_address_t source_address = *(gnw_address_t *)(packet_payload+1);
                            gnw_address_t target_address = *(gnw_address_t *)(packet_payload+1+sizeof(gnw_address_t));

                            fprintf( stdout, "%d -> %d\n", source_address, target_address );

                            client_context_t * source = findByAddress( source_address );
                            if( source == NULL ) {
                                fprintf( stderr, "No such valid source address -> %lx\n", source_address );
                                break;
                            }

                            client_context_t * target = findByAddress( target_address );
                            if( target == NULL ) {
                                fprintf( stderr, "No such valid target address -> %lx\n", target_address );
                                break;
                            }

                            for( int i=0; i<10; i++ ) {
                                if( source->links[i] == NULL ) {
                                    source->links[i] = target;
                                    break;
                                }
                            }

                            break;

                        default:
                            fprintf( stderr, "%s\tUnknown command, skipped.\n", clientAddress );
                            gnw_dumpPacket( stdout, iBuffer, -1 );
                            break;
                    }

                    break;

                default:
                    //fprintf( stderr, "%s\tBad response from client, skipping.\n", clientAddress );
                    //gnw_dumpPacket( stdout, iBuffer, packet_length );
                    break;
            }
        }


    }

    printf( "%s\nState: CLOSE.\n", clientAddress );
    context_cleanup( context );
    return NULL;
}

// Just emit some stats ever 5 seconds
void * status_process( void * notused ) {
    while( config.system_state ) {
        emitStatistics( stdout );

        sleep( 5 );
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
        exit( EXIT_FAILURE );
    }

    // Set up the client list
    client_list = ll_create();
    pthread_mutex_init( &client_list_mutex, NULL );

    // Begin running the status process (maybe this should become a watchdog in the future?)
    printf( "STATUS CREATE\n" );
    pthread_t status_context = { 0 };
    pthread_create( &status_context, NULL, status_process, NULL );
    pthread_detach( status_context );

    while( config.system_state ) {
        struct sockaddr_storage remote_socket;
        socklen_t newSock_len = sizeof( socklen_t );
        int remote_fd = accept( listen_fd, (struct sockaddr *)&remote_socket, &newSock_len );
        if( remote_fd == -1 ) {
            perror( "accept" );
            continue;
        }

        printf( "SOCK: %d\n", remote_fd );

        client_context_t *new_context = malloc(sizeof(client_context_t));
        memset(new_context, 0, sizeof(client_context_t));
        new_context->thread_state = malloc(sizeof(pthread_t));
        new_context->socket_fd = remote_fd;
        new_context->address = nextAddress++;
        new_context->link_policy = GNW_BROADCAST; // Default to broadcast

        for (int i = 0; i < GNW_MAX_LINKS; i++)
            new_context->links[i] = NULL;

        pthread_mutex_lock(&client_list_mutex);
        ll_append( client_list, new_context );

        // While we have the lock, go find any dead processes
        /*struct list_element * prev = NULL;
        struct list_element * iter = client_list->head;
        while( iter != NULL ) {
            if( ((client_context_t *)iter->data)->state == GNW_STATE_ZOMBIE ) {
                printf( "Removing %lx\n", ((client_context_t *)iter->data)->address );
                if( prev == NULL )
                    client_list->head = iter->next;
                else
                    prev->next = iter->next;

                struct list_element * next = iter->next;
                free( iter->data );
                free( iter );

                iter = next;
                continue;
            }
            iter = iter->next;
            prev = iter;
        }*/

        pthread_mutex_unlock(&client_list_mutex);

        int result = pthread_create( new_context->thread_state, NULL, clientProcess, new_context );
        assert( !result, "Could not create a new child process!" );
        pthread_detach( *new_context->thread_state ); // Release the thread, so it auto cleans on exit without a join()
    }

    if( listen_fd != -1 )
        close( listen_fd );

    return EXIT_SUCCESS;
}

#define ARG_STATUS     0
#define ARG_POLICY     1
#define ARG_CONNECT    2
#define ARG_DISCONNECT 3
#define ARG_SOURCE     4
#define ARG_TARGET     5

int main(int argc, char ** argv ) {

    setExitOnAssert( true ); // Crash out on assertion failures!

    config.network_mtu = getIFaceMTU( "lo" );
    config.system_state = SYSTEM_ACTIVE;

    if( config.network_mtu == -1 ) {
        fprintf( stderr, "Unable to query the local interface MTU, defaulting to 1500\n" );
        //config.network_mtu = 4096;
        config.network_mtu = 1500;
    }
    fprintf( stderr, "Network MTU detected as %d B\n", config.network_mtu );

    // If we have any arguments, assume that this is a remote command.
    if( argc > 1 ) {
        int rfd = socket_connect( "127.0.0.1", ROUTER_PORT ); // Assume local, for now.

        struct option longOptions[6] = {0};
        longOptions[ARG_STATUS].name = "status";
        longOptions[ARG_STATUS].has_arg = no_argument;
        longOptions[ARG_STATUS].flag = NULL;

        longOptions[ARG_POLICY].name = "policy";
        longOptions[ARG_POLICY].has_arg = required_argument;
        longOptions[ARG_POLICY].flag = NULL;

        longOptions[ARG_CONNECT].name = "connect";
        longOptions[ARG_CONNECT].has_arg = no_argument;
        longOptions[ARG_CONNECT].flag = NULL;

        longOptions[ARG_DISCONNECT].name = "disconnect";
        longOptions[ARG_DISCONNECT].has_arg = no_argument;
        longOptions[ARG_DISCONNECT].flag = NULL;

        longOptions[ARG_SOURCE].name = "source";
        longOptions[ARG_SOURCE].has_arg = required_argument;
        longOptions[ARG_SOURCE].flag = NULL;

        longOptions[ARG_TARGET].name = "target";
        longOptions[ARG_TARGET].has_arg = required_argument;
        longOptions[ARG_TARGET].flag = NULL;

        gnw_address_t arg_source_address = 0;
        gnw_address_t arg_target_address = 0;

        // Argument Parsing //
        int arg;
        int indexPtr = 0;
        while ((arg = getopt_long(argc, argv, "", longOptions, &indexPtr)) != -1) {
            switch (indexPtr) {
                case ARG_STATUS: {
                    unsigned char buffer[1] = { GNW_CMD_STATUS };
                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 1);

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case ARG_POLICY: {
                    unsigned char buffer[2 + sizeof(gnw_address_t)];
                    *buffer = GNW_CMD_POLICY;

                    if( strncmp(optarg, "broadcast", 9 ) == 0 )
                        *(buffer+1) = GNW_BROADCAST;
                    else if( strncmp(optarg, "roundrobin", 10 ) == 0 )
                        *(buffer+1) = GNW_ROUNDROBIN;
                    else if( strncmp(optarg, "anycast", 7 ) == 0 )
                        *(buffer+1) = GNW_ANYCAST;

                    *(gnw_address_t *)(buffer+2) = arg_target_address;

                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 2+sizeof(gnw_address_t) );

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case ARG_CONNECT: {
                    printf( "Connect!\n" );
                    unsigned char buffer[1 + (sizeof(gnw_address_t)*2) ];
                    *buffer = GNW_CMD_CONNECT;
                    gnw_address_t * source = (gnw_address_t *)(buffer+1);
                    gnw_address_t * target = (gnw_address_t *)(buffer+1+sizeof(gnw_address_t));

                    *source = arg_source_address;
                    *target = arg_target_address;

                    printf( "%x -> %x\n", arg_source_address, arg_target_address );

                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 1+ (sizeof(gnw_address_t)*2) );

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case ARG_DISCONNECT: {
                    unsigned char buffer[1] = {GNW_CMD_DISCONNECT};
                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 1);

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case ARG_SOURCE: arg_source_address = strtoul( optarg, NULL, 10 ); break;
                case ARG_TARGET: arg_target_address = strtoul( optarg, NULL, 10 ); break;

                default:
                    fprintf(stderr, "Bad command combination. STOP.");
                    close( rfd );
                    return EXIT_SUCCESS;
            }
        }

        fprintf( stderr, "Unrecognised argument combination...?\n" );
        close( rfd );
        return EXIT_SUCCESS;
    }

    return router_process();
}