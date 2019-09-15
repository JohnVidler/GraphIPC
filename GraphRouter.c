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
#include <assert.h>
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
#include "lib/packet.h"
#include "IndexTable.h"
#include "NodeTable.h"
#include "ForwardTable.h"
#include "Log.h"
#include "BuildInfo.h"
#include <poll.h>
#include <sys/un.h>
#include <getopt.h>

#define SYSTEM_ACTIVE 1
#define SYSTEM_STOP   0

struct _configuration {
    int network_mtu;
    int system_state;
    int verbosity;

    bool arg_dot;
};

volatile struct _configuration config;

/**
 * Context for a given connection to a running node or subgraph-router.
 */
typedef struct {
    pthread_t * thread_state;
    int socket_fd;
    uint8_t * rx_buffer;
    uint8_t * rx_buffer_tail;
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

    log_debug( "Next addresss = %08x\n", nextNodeAddress );

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
        sprintf( policy_str, "None  " );

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
        edge_t *iter = forward->edgeList;
        while ( iter != NULL) {
            printf("[%08x]", iter->target );
            if( iter->next != NULL )
                printf( ", " );
            iter = iter->next;
        }
    }
    else
        printf( "∅" );

    printf( "\n" );
}

void printAsDOT( gnw_address_t address, void * data ) {
    printf( "\tnode_%08x [label=\"0x%08x\"]\n", address, address );

    forward_t * forward = forward_table_find( address );
    if( forward != NULL ) {
        edge_t *iter = forward->edgeList;
        while ( iter != NULL) {
            printf("\tnode_%08x -> node_%08x\n", address, iter->target );
            iter = iter->next;
        }
    }
}

void emitStatistics( FILE * stream ) {
    fprintf( stream, "UID      | State     | In            | Out           | Policy    | Links\n" );

    node_table_walk( printDetails );

    if( config.arg_dot ) {
        printf("\ndigraph g {\n");
        node_table_walk(printAsDOT);
        printf("}\n\n");
    }

    fprintf( stream, "\n" );
}

void context_cleanup( context_t * context ) {
    context->state = GNW_STATE_CLOSE;
    close( context->socket_fd );
    context->rx_buffer_tail = NULL;
    free( context->rx_buffer );

    // Become zombie, in case cleanup fails
    context->state = GNW_STATE_ZOMBIE;

    free( context );
}

void * clientProcess( void * _context ) {
    context_t * context = (context_t *)_context;
    context->state = GNW_STATE_OPEN;

    // Fire up our ring buffer
    context->rx_buffer = (uint8_t *)malloc( config.network_mtu * 20 ); // 20 packets of MTU-size
    context->rx_buffer_tail = context->rx_buffer;

    log_info( "Client poll loop running..." );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 1 );

    watch_fd[0].fd = context->socket_fd;
    watch_fd[0].events = POLLIN;

    unsigned char latchBuffer[config.network_mtu];

    int tick_rate = 1000;
    int logout_timeout = 10;
    ssize_t bytes = 1;
    while( bytes > 0 ) {
        log_debug( "Poll..." );
        int rv = poll( watch_fd, 1, tick_rate );

        // Wait error, drop back to callee
        if( rv == -1 ) {
            log_error( "IO Error, dropping client.\n" );
            context_cleanup( context );
            return NULL;
        }

        // Timeout...
        if( rv == 0 ) {
            log_debug( "Timeout" );
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

        log_debug( "Data on the wire..." );

        bytes = read( context->socket_fd, latchBuffer, config.network_mtu );

        assert( bytes <= config.network_mtu, "Buffer over-read!" );

        context->rx_buffer_tail = packet_write_u8_buffer( context->rx_buffer_tail, latchBuffer, bytes );
        context->bytes_in += bytes;
        context->packets_in++;

        log_debug( "Parsing..." );
        ssize_t readyBytes = -1;
        while( (readyBytes = gnw_nextPacket( context->rx_buffer, context->rx_buffer_tail - context->rx_buffer )) != 0 ) {

            if( readyBytes < 0 ) {
                printf( "DESYNC...\n" );
                packet_shift( context->rx_buffer, config.network_mtu * 20, NULL, 1 );
                context->rx_buffer_tail--;
            }

            //printf( "{PKT:%ld}\n", readyBytes );

            gnw_header_t packet_header  = { 0 };
            uint8_t * ptr = context->rx_buffer;
            ptr = packet_read_u8( ptr, &packet_header.magic );
            ptr = packet_read_u8( ptr, &packet_header.version );
            ptr = packet_read_u8( ptr, &packet_header.type );
            ptr = packet_read_u32( ptr, &packet_header.source );
            ptr = packet_read_u32( ptr, &packet_header.length );

            uint8_t packet_payload[config.network_mtu]; // Static buffer to avoid malloc'ing :)
            memcpy( packet_payload, ptr, packet_header.length );

            // Shift the buffer along, so we don't have to worry about it later
            packet_shift( context->rx_buffer, config.network_mtu * 20, NULL, 11 + packet_header.length );
            context->rx_buffer_tail -= 11 + packet_header.length;

            log_debug( "RX: Type = %x, Length = %u", packet_header.type, packet_header.length );

            switch( packet_header.type ) {
                case GNW_DATA: {
                    // Check out any edges for this source...
                    forward_t *forward = forward_table_find(packet_header.source);

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
                                              iter->target, packet_header.source);
                                    log_write(SEVERE, "Giving up on this broadcast, to save the rest of the graph!");
                                    pthread_mutex_unlock(&forward->listLock);
                                    break;
                                }

                                gnw_emitDataPacket(remote->socket_fd, packet_header.source, packet_payload,
                                                   packet_header.length);
                                context->bytes_out += packet_header.length;
                                context->packets_out++;
                                remote->bytes_in += packet_header.length;
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
                                gnw_emitDataPacket(remote->socket_fd, packet_header.source, packet_payload,
                                                   packet_header.length);
                                context->bytes_out += packet_header.length;
                                context->packets_out++;
                                remote->bytes_in += packet_header.length;
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

                            gnw_emitDataPacket(remote->socket_fd, packet_header.source, packet_payload,
                                               packet_header.length);
                            context->bytes_out += packet_header.length;
                            context->packets_out++;
                            remote->bytes_in += packet_header.length;
                            remote->packets_in++;

                            forward->round_robin_ref = forward->round_robin_ref->next;
                            pthread_mutex_unlock(&forward->listLock);
                            break;
                    }

                    break;
                }

                case GNW_COMMAND:
                    if( packet_header.length < 1 ) {
                        log_warn( "Client sent a command with no operator, skipping." );
                        break;
                    }

                    // Select based on the first byte -> The operator
                    switch( *packet_payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            log_info( "New address request" );

                            gnw_address_t fresh_address = genNextValidAddress();

                            if( packet_header.length != 1 ) {
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


                        case GNW_CMD_CONNECT: {
                            gnw_address_t source_address = *(gnw_address_t *) (packet_payload + 1);
                            gnw_address_t target_address = *(gnw_address_t *) (packet_payload + 1 + sizeof(gnw_address_t));

                            log_info( "Node connect request [%08x] -> [%08x]", source_address, target_address );

                            // Try to grab the source context, if any
                            context_t *source = node_table_find(source_address);
                            if (source == NULL) {
                                fprintf(stderr, "No such valid source address -> %ux\n", source_address);
                                break;
                            }

                            // Try to grab the target context, if any
                            context_t *target = node_table_find(target_address);
                            if (target == NULL) {
                                fprintf(stderr, "No such valid target address -> %ux\n", target_address);
                                break;
                            }

                            forward_table_add_edge(source_address, target_address);
                            break;
                        }

                        default:
                            log_warn( "Unknown command, skipped" );
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

        sleep( 10 );
    }
    return NULL; // Should never happen...
}

int router_process() {

    // Set up the socket server
    struct addrinfo listen_hints;
    //int ret;

    memset( &listen_hints, 0, sizeof listen_hints );
    listen_hints.ai_family   = AF_INET;
    listen_hints.ai_socktype = SOCK_STREAM;

    int listen_fd = getListenSocket( &listen_hints );

    printf( "SockFD = %d\n", listen_fd );

    if( listen( listen_fd, ROUTER_BACKLOG ) == -1 ) {
        perror( "listen" );
        exit( EXIT_FAILURE );
    }

    // Set up the address and forward tables
    printf( "Node Table: " );
    node_table_init();

    printf( "Forward Table: " );
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

#define ARG_HELP       0
#define ARG_STATUS     1
#define ARG_POLICY     2
#define ARG_CONNECT    3
#define ARG_DISCONNECT 4
#define ARG_SOURCE     5
#define ARG_TARGET     6
#define ARG_MTU        7
#define ARG_DOT        8
#define ARG_VERSION    9

int main(int argc, char ** argv ) {

    setExitOnAssert( true ); // Crash out on assertion failures!

    log_setLevel( ERROR );

    config.network_mtu = getIFaceMTU( "lo" );
    config.system_state = SYSTEM_ACTIVE;
    config.verbosity = 0;

    if( config.network_mtu == -1 ) {
        log_error( "Unable to query the local interface MTU, guessing 1500 bytes\n" );
        config.network_mtu = 1500;
    }
    log_debug( "Network MTU detected as %d B\n", config.network_mtu );

    // If we have any arguments, assume that this is a remote command.
    if( argc > 1 ) {
        int rfd = socket_connect( "127.0.0.1", ROUTER_PORT ); // Assume local, for now.

#pragma GCC diagnostic ignored "-Wmissing-braces" // This is a GCC bug for initializing structures in an array
        struct option longOptions[11] = {
                [ARG_HELP] =       { .name="help",       .has_arg=no_argument,       .flag=NULL },
                [ARG_STATUS] =     { .name="status",     .has_arg=no_argument,       .flag=NULL },
                [ARG_POLICY] =     { .name="policy",     .has_arg=required_argument, .flag=NULL },
                [ARG_CONNECT] =    { .name="connect",    .has_arg=no_argument,       .flag=NULL },
                [ARG_DISCONNECT] = { .name="disconnect", .has_arg=no_argument,       .flag=NULL },
                [ARG_SOURCE] =     { .name="source",     .has_arg=required_argument, .flag=NULL },
                [ARG_TARGET] =     { .name="target",     .has_arg=required_argument, .flag=NULL },
                [ARG_MTU] =        { .name="mtu",        .has_arg=required_argument, .flag=NULL },
                [ARG_DOT] =        { .name="dot",        .has_arg=no_argument,       .flag=NULL },
                [ARG_VERSION] =    { .name="version",    .has_arg=no_argument,       .flag=NULL },
                0
        };
#pragma GCC diagnostic pop

        gnw_address_t arg_source_address = 0;
        gnw_address_t arg_target_address = 0;

        // Argument Parsing //
        int arg;
        int indexPtr = 0;
        while ((arg = getopt_long(argc, argv, "cdhp:s:t:v", longOptions, &indexPtr)) != -1) {

            // If we have a short arg, pass it over to the long arg index.
            // Note: This will work assuming we have less than 65(?) long arguments... I think -John.
            if( arg > 0 )
                indexPtr = arg;

            switch (indexPtr) {
                case 'h':
                case ARG_HELP: {
                    printf("GraphWrap\n");
                    printf(ANSI_COLOR_GREEN "\t\t\"Don't cross the streams\" --Egon Spengler.\n\n" ANSI_COLOR_RESET);
                    printf("The userspace router for GraphIPC messaging\n\n");
                    printf(ANSI_COLOR_CYAN "--help -h\n" ANSI_COLOR_RESET "\tShow this help message\n\n");
                    printf(ANSI_COLOR_CYAN "--status\n" ANSI_COLOR_RESET "\tRequest a status message from a running router instance\n\n");
                    printf(ANSI_COLOR_CYAN "--policy\n" ANSI_COLOR_RESET "\tChange the link policy between --source and --target\n\n");
                    printf(ANSI_COLOR_CYAN "--connect -c\n" ANSI_COLOR_RESET "\tConnect --source to --target, with default (broadcast) policy\n\n");
                    printf(ANSI_COLOR_CYAN "--disconnect -d\n" ANSI_COLOR_RESET "\tDisconnect --source from --target\n\n");
                    printf(ANSI_COLOR_CYAN "--source -s\n" ANSI_COLOR_RESET "\tThe source address of the arc to modify\n\n");
                    printf(ANSI_COLOR_CYAN "--target -t\n" ANSI_COLOR_RESET "\tThe target address of the arc or node to modify\n\n");
                    printf(ANSI_COLOR_CYAN "--mtu\n" ANSI_COLOR_RESET "\tForce a particular MTU - settings this too high may cause excessive packet loss!\n\n");
                    printf(ANSI_COLOR_CYAN "--dot\n" ANSI_COLOR_RESET "\tOutput the connectome in DOT format periodically, rather than status messages or the address table\n\n");
                    printf(ANSI_COLOR_CYAN "-v\n" ANSI_COLOR_RESET "\tIncrease log verbosity, each instance increases the log level (Default: ERROR only). Must be called first to have effect\n\n");
                    //printf(ANSI_COLOR_CYAN "--FLAG\n" ANSI_COLOR_RESET "\tDESCRIPTION\n\n");
                    return EXIT_SUCCESS;
                }

                case ARG_STATUS: {
                    unsigned char buffer[1] = { GNW_CMD_STATUS };
                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 1);

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case 'p':
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

                case 'c':
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

                case 'd':
                case ARG_DISCONNECT: {
                    unsigned char buffer[1] = {GNW_CMD_DISCONNECT};
                    gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 1);

                    close(rfd);
                    return EXIT_SUCCESS;
                }

                case 's':
                case ARG_SOURCE:
                    arg_source_address = (gnw_address_t )strtoul( optarg, NULL, 16 );
                    log_debug( "Source address = %lx", arg_source_address );
                    break;

                case 't':
                case ARG_TARGET:
                    arg_target_address = (gnw_address_t )strtoul( optarg, NULL, 16 );
                    log_debug( "Target address = %lx", arg_target_address );
                    break;

                case ARG_MTU:
                    config.network_mtu = (signed)strtoul( optarg, NULL, 10 );
                    log_warn( "Manually setting the network MTU to %d... this may make everything break!", config.network_mtu );
                    break;

                case ARG_DOT: config.arg_dot = true; break;

                case ARG_VERSION:
                    printf( "Version: %s (%s)\n", GIT_TAG, GIT_HASH );
                    return EXIT_SUCCESS;

                case 'v':
                    config.verbosity++;

                    log_debug( "Log verbosity = %d...", config.verbosity );

                    if (config.verbosity == 1)
                        log_setLevel(INFO);
                    else if (config.verbosity == 2)
                        log_setLevel(DEBUG);
                    break;

                default:
                    fprintf(stderr, "Bad command combination. STOP.");
                    close( rfd );
                    return EXIT_SUCCESS;
            }
        }
    }

    return router_process();
}