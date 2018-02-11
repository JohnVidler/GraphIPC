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
#include "IndexTable.h"
#include "NodeTable.h"
#include "ForwardTable.h"
#include "Log.h"
#include <poll.h>
#include <sys/un.h>
#include <getopt.h>

#define SYSTEM_ACTIVE 1
#define SYSTEM_STOP   0

struct _configuration {
    int network_mtu;
    int system_state;
};

volatile struct _configuration config;

/**
 * Context for a given connection to a running node or subgraph-router.
 */
typedef struct {
    pthread_t * thread_state;
    int socket_fd;
    RingBuffer_t * rx_buffer;
    int state;
    uint64_t packets_in;
    uint64_t packets_out;
    uint64_t bytes_in;
    uint64_t bytes_out;

} context_t;

// Note: Replace client_list with the address table (table_address)!

linked_list_t    * context_list;
volatile gnw_address_t nextNodeAddress = 0;

gnw_address_t genNextValidAddress() {
    nextNodeAddress += 0x1000;
    while( node_table_find( nextNodeAddress ) != NULL && nextNodeAddress != 0 )
        nextNodeAddress += 0x1000;

    if( nextNodeAddress == 0 ) {
        log_write(SEVERE, "Address pool exhaustion! No more addresses!");
        exit( EXIT_FAILURE ); // Note: this is a really, really bad way to handle this, but it'll save us from bad internal state mangling the output, kinda :|
    }
    return nextNodeAddress;
}

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

/*void renderAddressesToFILE( void * inContext, void * inFile ) {
    context_t * context = (context_t *)inContext;
    FILE * stream = (FILE *)inFile;

    //if( context->state != GNW_STATE_ZOMBIE ) {
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
        case GNW_POLICY_BROADCAST:
            sprintf(policy_str, "BROADCAST");
            break;
        case GNW_POLICY_ANYCAST:
            sprintf(policy_str, "ANYCAST");
            break;
        case GNW_POLICY_ROUNDROBIN:
            sprintf(policy_str, "ROUNDROBIN");
            break;
        default:
            sprintf(policy_str, "???");
    }

    char *out_suffix = NULL;
    double scaled_bytes_out = fmt_iec_size(context->bytes_out, &out_suffix);

    char *in_suffix = NULL;
    double scaled_bytes_in = fmt_iec_size(context->bytes_in, &in_suffix);

    fprintf(stream, "%08lx |%10s |%10.2f %3s |%10.2f %3s |%10s | ",
            context->address,
            state_str,
            scaled_bytes_in,
            in_suffix,
            scaled_bytes_out,
            out_suffix,
            policy_str);

    sink_t * iter = context->links;
    while( iter != NULL ) {
        context_t * remote = iter->target_context;
        if( remote != NULL )
            fprintf( stream, "%08x, ", remote->address );
        else
            fprintf( stream, "NULL, " );
        iter = iter->next;
    }

    fprintf(stream, "\n");
}*/

void printDetails( gnw_address_t address, void * data ) {
    context_t * context = (context_t *)data;

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

    forward_t * forward = forward_table_find( address );

    char policy_str[32] = {0};
    if( forward != NULL ) {
        switch (forward->forward_policy) {
            case GNW_POLICY_BROADCAST:
                sprintf(policy_str, "BROADCAST");
                break;
            case GNW_POLICY_ANYCAST:
                sprintf(policy_str, "ANYCAST");
                break;
            case GNW_POLICY_ROUNDROBIN:
                sprintf(policy_str, "ROUNDROBIN");
                break;
            default:
                sprintf(policy_str, "???");
        }
    }
    else
        sprintf( policy_str, "∅" );

    char *out_suffix = NULL;
    double scaled_bytes_out = fmt_iec_size(context->bytes_out, &out_suffix);

    char *in_suffix = NULL;
    double scaled_bytes_in = fmt_iec_size(context->bytes_in, &in_suffix);

    printf( "%08x |%10s |%10.2f %3s |%10.2f %3s |%10s | ",
            address,
            state_str,
            scaled_bytes_in,
            in_suffix,
            scaled_bytes_out,
            out_suffix,
            policy_str);

    if( forward != NULL && forward->edgeList != NULL ) {
        printf( "FORWARD: %p\n", forward );
        edge_t *iter = forward->edgeList;
        while ( iter != NULL) {
            printf("%p, ", iter );
            iter = iter->next;
        }
    }
    else
        printf( "∅" );

    printf( "\n" );
}

void emitStatistics( FILE * stream ) {
    fprintf( stream, "UID      | State     | In            | Out           | Policy    | Links\n" );

    node_table_walk( printDetails );

    fprintf( stream, "\n" );
}

void context_cleanup( context_t * context ) {
    context->state = GNW_STATE_CLOSE;
    close( context->socket_fd );
    ringbuffer_destroy( context->rx_buffer );

    // Become zombie, in case cleanup fails
    context->state = GNW_STATE_ZOMBIE;

    free( context );
}

void * clientProcess( void * _context ) {
    context_t * context = (context_t *)_context;
    context->state = GNW_STATE_OPEN;

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
            log_error( "IO Error, dropping client.\n" );
            context_cleanup( context );
            return NULL;
        }

        // Timeout...
        if( rv == 0 ) {
            if( context->state == GNW_STATE_OPEN ) {
                if (logout_timeout-- < 0) {
                    log_warn( "Client timed out, dropping them.\n" );
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
        }

        if( bytes == -1 ) {
            log_warn( "Client connection looks dropped\n" );
            continue;
        }

        // Attempt (or re-attempt) to push to the ring, backing off as required.
        read_back_off = false;
        if( ringbuffer_write( context->rx_buffer, latchBuffer, bytes ) != bytes ) {
            log_warn( "Buffer overflow! Read backoff enabled, trying to drain the buffer...\n" );
            read_back_off = true;
        }
        else
        {
            context->bytes_in += bytes;
            context->packets_in++;
        }

        while( gnw_nextPacket( context->rx_buffer, &parser_context, iBuffer ) ) {
            gnw_header_t *  packet_header  = (gnw_header_t *)iBuffer;
            uint32_t        packet_length  = sizeof( gnw_header_t ) + packet_header->length;
            unsigned char * packet_payload = (unsigned char *)( iBuffer + sizeof(gnw_header_t) );

            switch( packet_header->type ) {
                case GNW_DATA: {
                    // Check out any edges for this source...
                    forward_t *forward = forward_table_find(packet_header->source);

                    // Fast exit, if there are no links associated with this node
                    if (forward == NULL || forward->edgeList == NULL)
                        break;

                    switch (forward->forward_policy) {
                        case GNW_POLICY_BROADCAST: {
                            pthread_mutex_lock(&forward->listLock);
                            edge_t *iter = forward->edgeList;
                            while (iter != NULL) {
                                context_t *remote = iter->context;
                                if (remote == NULL) {
                                    remote = node_table_find(iter->target);
                                    iter->context = remote;
                                    log_info(
                                            "Populated [%08x]'s context reference, subsequent lookups will be faster :)",
                                            iter->target);
                                }

                                if (remote == NULL) {
                                    log_write(SEVERE,
                                              "Somehow, target [%08x] managed to get into [%08x]'s forward list! Something is very wrong!",
                                              iter->target, packet_header->source);
                                    log_write(SEVERE, "Giving up on this broadcast, to save the rest of the graph!");
                                    pthread_mutex_unlock(&forward->listLock);
                                    break;
                                }

                                gnw_emitDataPacket(remote->socket_fd, packet_header->source, packet_payload,
                                                   packet_header->length);
                                context->bytes_out += packet_header->length;
                                context->packets_out++;
                                remote->bytes_in += packet_header->length;
                                remote->packets_in++;

                                iter = iter->next;
                            }
                            pthread_mutex_unlock(&forward->listLock);
                            break;
                        }

                        case GNW_POLICY_ANYCAST: {
                            pthread_mutex_lock(&forward->listLock);

                            edge_t *target = forward->edgeList;
                            edge_t *iter = forward->edgeList;
                            for (int probability = 2; iter != NULL; probability++) {
                                if (rand() % probability == 0)
                                    target = iter;
                                iter = iter->next;
                            }

                            context_t *remote = target->context;
                            if (remote == NULL) {
                                remote = node_table_find(iter->target);
                                iter->context = remote;
                                log_info("Populated [%08x]'s context reference, subsequent lookups will be faster :)",
                                         iter->target);
                            }

                            // Guard against
                            if (remote != NULL) {
                                gnw_emitDataPacket(remote->socket_fd, packet_header->source, packet_payload,
                                                   packet_header->length);
                                context->bytes_out += packet_header->length;
                                context->packets_out++;
                                remote->bytes_in += packet_header->length;
                                remote->packets_in++;
                            }

                            pthread_mutex_unlock(&forward->listLock);
                            break;
                        }

                        case GNW_POLICY_ROUNDROBIN:
                            pthread_mutex_lock(&forward->listLock);
                            if (forward->round_robin_ref == NULL)
                                forward->round_robin_ref = forward->edgeList;

                            // Just bail if we don't have anything to work with, just in case!
                            if (forward->round_robin_ref == NULL) {
                                pthread_mutex_unlock(&forward->listLock);
                                break;
                            }

                            // Find the context for this address, by lookup or by fast reference
                            context_t *remote = forward->round_robin_ref->context;
                            if (remote == NULL) {
                                remote = node_table_find(forward->round_robin_ref->target);
                                forward->round_robin_ref->context = remote;
                                log_info("Populated [%08x]'s context reference, subsequent lookups will be faster :)",
                                         forward->round_robin_ref->target);
                            }

                            gnw_emitDataPacket(remote->socket_fd, packet_header->source, packet_payload,
                                               packet_header->length);
                            context->bytes_out += packet_header->length;
                            context->packets_out++;
                            remote->bytes_in += packet_header->length;
                            remote->packets_in++;

                            forward->round_robin_ref = forward->round_robin_ref->next;
                            pthread_mutex_unlock(&forward->listLock);
                            break;
                    }

                    break;
                }

                case GNW_COMMAND:
                    if( packet_header->length < 1 ) {
                        log_warn( "Client sent a command with no operator, skipping." );
                        break;
                    }

                    // Select based on the first byte -> The operator
                    switch( *packet_payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            log_info( "New address request" );

                            gnw_address_t fresh_address = genNextValidAddress();

                            if( packet_header->length != 1 ) {
                                gnw_address_t * reqAddress = (gnw_address_t *)(packet_payload+1);
                                gnw_address_t nodeMask = (~(unsigned)0) ^ (unsigned)0xFF;

                                if( (*reqAddress & nodeMask) == *reqAddress ) {
                                    log_info( "Context requested a specific address [%08x], checking for availability\n", *reqAddress );

                                    if( node_table_find( *reqAddress ) != NULL ) {
                                        log_warn( "Address already in use, refusing the claim on [%08x]", *reqAddress );
                                    }
                                    else
                                        fresh_address = *reqAddress;
                                }
                                else {
                                    log_warn( "Bad request address - node addresses cannot be non-zero in the last octet! Using generated address instead." );
                                }
                            }

                            // Actually become a node at this point - up to now, this could have just been a context_t reference to a thread...
                            node_table_add( fresh_address, context );

                            unsigned char * replyBuffer = malloc( sizeof(gnw_address_t) + 1 );
                            *replyBuffer = GNW_CMD_NEW_ADDRESS;
                            gnw_address_t * payload = (gnw_address_t *)(replyBuffer + 1);
                            *payload = fresh_address;
                            gnw_emitCommandPacket( context->socket_fd, GNW_COMMAND | GNW_REPLY, replyBuffer, sizeof(gnw_address_t)+1 );
                            free( replyBuffer );

                            // Going live (run state)
                            if( context->state == GNW_STATE_OPEN ) {
                                context->state = GNW_STATE_RUN;
                                log_info( "Client going to RUN state, will not time out." );
                                tick_rate = 10000; // Now tick at 10 second intervals, just so we don't spin unnecessarily
                            }
                            break;

                        case GNW_CMD_STATUS:
                            log_info( "Status request.\n" );

                            emitStatistics( stdout );

                            break;

                        case GNW_CMD_POLICY: {
                            log_info( "Node policy change." );

                            unsigned char newPolicy = *(packet_payload + 1);
                            gnw_address_t target = *(gnw_address_t *) (packet_payload + 2);

                            if( target < 1 ) {
                                log_warn( "No endpoint can ever be address zero!" );
                                break;
                            }

                            forward_t * forward = forward_table_find( target );
                            if( forward == NULL ) {
                                log_error( "No such valid address [%08x], ignored request", target );
                                break;
                            }

                            log_info( "[%08x] policy changed from %d to %d", target, forward->forward_policy, newPolicy );
                            forward->forward_policy = newPolicy;

                            break;
                        }


                        case GNW_CMD_CONNECT:
                            log_info( "Node connect request." );

                            gnw_address_t source_address = *(gnw_address_t *)(packet_payload+1);
                            gnw_address_t target_address = *(gnw_address_t *)(packet_payload+1+sizeof(gnw_address_t));

                            fprintf( stdout, "%x -> %x\n", source_address, target_address );

                            // Try to grab the source context, if any
                            context_t * source = node_table_find( source_address );
                            if( source == NULL ) {
                                fprintf( stderr, "No such valid source address -> %lx\n", source_address );
                                break;
                            }

                            // Try to grab the target context, if any
                            context_t * target = node_table_find( target_address );
                            if( target == NULL ) {
                                fprintf( stderr, "No such valid target address -> %lx\n", target_address );
                                break;
                            }

                            forward_table_add_edge( source_address, target_address );
                            break;

                        default:
                            log_warn( "Unknown command, skipped" );
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

    log_info( "State: CLOSE" );
    context_cleanup( context );
    return NULL;
}

// Just emit some stats ever 5 seconds
void * status_process( void * notused ) {
    while( config.system_state ) {
        emitStatistics( stdout );

        sleep( 1 );
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

    // Set up the address and forward tables
    node_table_init();
    forward_table_init();

    context_list  = ll_create();

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

        context_t *new_context = malloc(sizeof(context_t));
        memset(new_context, 0, sizeof(context_t));
        new_context->thread_state = malloc(sizeof(pthread_t));
        new_context->socket_fd = remote_fd;

        ll_append( context_list, new_context );

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

    log_setLevel( DEBUG );

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

        struct option longOptions[7] = {
                [ARG_STATUS] =     { .name="status",     .has_arg=no_argument,       .flag=NULL },
                [ARG_POLICY] =     { .name="policy",     .has_arg=required_argument, .flag=NULL },
                [ARG_CONNECT] =    { .name="connect",    .has_arg=no_argument,       .flag=NULL },
                [ARG_DISCONNECT] = { .name="disconnect", .has_arg=no_argument,       .flag=NULL },
                [ARG_SOURCE] =     { .name="source",     .has_arg=required_argument, .flag=NULL },
                [ARG_TARGET] =     { .name="target",     .has_arg=required_argument, .flag=NULL },
                0
        };

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
                        *(buffer+1) = GNW_POLICY_BROADCAST;
                    else if( strncmp(optarg, "roundrobin", 10 ) == 0 )
                        *(buffer+1) = GNW_POLICY_ROUNDROBIN;
                    else if( strncmp(optarg, "anycast", 7 ) == 0 )
                        *(buffer+1) = GNW_POLICY_ANYCAST;

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