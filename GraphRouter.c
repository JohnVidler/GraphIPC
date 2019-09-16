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
#include "lib/klib/khash.h"
#include "lib/klib/kvec.h"

#define MAX_MONITOR_FDS 128

#define SYSTEM_ACTIVE 1
#define SYSTEM_STOP   0

struct _configuration {
    size_t network_mtu;
    int system_state;
    int verbosity;

    bool arg_dot;
};

struct _configuration config;

typedef struct {
    uint8_t * buffer;
    uint8_t * buffer_tail;
} local_buffer_t;

/**
 * Context for a given connection to a running node or subgraph-router.
 */
typedef struct {
    kvec_t( gnw_address_t ) forward;
    int bound_fd;

    int state;
    uint64_t packets_in;
    uint64_t packets_out;
    uint64_t bytes_in;
    uint64_t bytes_out;
} context_t;

KHASH_MAP_INIT_INT( gnw_address_t, context_t );
khash_t( gnw_address_t ) * address_table;

KHASH_MAP_INIT_INT( int, local_buffer_t );
khash_t( int ) * local_buffer;

/*volatile gnw_address_t nextNodeAddress = 0;

gnw_address_t genNextValidAddress() {
    nextNodeAddress += 0x1000;
    while( node_table_find( nextNodeAddress ) != NULL && nextNodeAddress != 0 )
        nextNodeAddress += 0x1000;

    if( nextNodeAddress == 0 ) {
        log_write( SEVERE, "Address pool exhaustion! No more addresses!" );
        exit( EXIT_FAILURE ); // Note: this is a really, really bad way to handle this, but it'll save us from bad internal state mangling the output, kinda :|
    }

    log_debug( "Next addresss = %08x\n", nextNodeAddress );

    return nextNodeAddress;
}*/

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

void reset_context( context_t * context ) {
    context->packets_in = 0;
    context->packets_out = 0;
    context->bytes_in = 0;
    context->bytes_out = 0;

    if( context->state != -1 )
        kv_destroy( context->forward );

    context->state = 0;
}

void setup_context( context_t * context ) {
    reset_context( context );
    kv_init( context->forward );
    context->state = 0;
}

void handle_packet( int fd, uint8_t * buffer, size_t length ) {
    assert( buffer != NULL, "Attempted to parse a null buffer!" );
    assert( length > 0, "Attempted to parse an empty (zero-length) buffer!" );

    gnw_header_t header;
    uint8_t * payload = gnw_parse_header( buffer, &header );
    
    switch( header.type ) {
        case GNW_COMMAND:
            if( header.length == 0 ) {
                log_warn( "Client issued a command with no directive, nothing to do!" );
                return;
            }

            uint8_t directive = 0;
            uint8_t * next = packet_read_u8( payload, &directive );

            switch( directive ) {
                // Handle new address requests - bit of a misnomer, as this actually really 'claims' an address
                // rather than creating a new one. Essentially 'binds' the address to the fd the request came
                // from
                case GNW_CMD_NEW_ADDRESS:
                    log_debug( "New address request" );

                    // Generate a random address, else use the requested one.
                    gnw_address_t address_req = 0xFFFFF000 & rand(); // Random address
                    if( header.length == 5 )
                        packet_read_u32( next, &address_req );

                    int status;
                    khint_t hint = kh_put( gnw_address_t, address_table, address_req, &status );
                    context_t * context = &kh_value( address_table, hint );

                    assert( context != NULL, "Null context!" );

                    memset( context, 0, sizeof(context_t) );
                    setup_context( context );

                    context->bound_fd = fd; // Bind this fd to this address (or visa-versa)

                    // Reply to the client with their assigned address
                    uint8_t reply[5] = { 0 };
                    uint8_t * next = packet_write_u8( reply, GNW_CMD_NEW_ADDRESS );
                    //next = packet_write_u32( next, 0x1000 );
                    next = packet_write_u32( next, address_req ); // Just accept _any_ address from the clients for now...
                    gnw_emitCommandPacket( fd, GNW_COMMAND, reply, 5 );

                    break;

                default:
                    log_warn( "Missing handler for command directive %02x", payload[0] );
            }

            break;

        case GNW_DATA: {
            khint_t hint = kh_get( gnw_address_t, address_table, header.source );

            // Just drop the message, if we don't have a known, bound address for this...
            if( !kh_exist( address_table, hint ) ) {
                log_debug( "Dropped %lu bytes.", length );
                return;
            }

            // Grab this entry
            context_t * entry = &kh_value( address_table, hint );

            // Update the stats
            entry->bytes_in += length;
            entry->packets_in ++;

            // TEST BROADCAST MODE ONLY
            // ToDo: This should look up the routing mode, then forward according to the policy to anything
            //       in the entry->forward vector.
            //       Kludge for now...

            khint_t fwd = kh_get( gnw_address_t, address_table, 0x1000 );
            if( !kh_exist( address_table, fwd ) ) {
                // ToDo: Should delete any missing destinations!
                return;
            }

            context_t * fwdEntry = &kh_value( address_table, fwd );

            assert( fwdEntry != NULL, "Null forwarding entry!" );

            gnw_emitPacket( fwdEntry->bound_fd, buffer, length ); // Forward wholesale

            fwdEntry->bytes_out += length;
            fwdEntry->packets_out ++;
        }
        break;

        default:
            log_warn( "Unknown header type %02x, ignored, may be build mismatch?", header.type );
            gnw_dumpPacket( stdout, buffer, length );
    }

}

void handle_event( int index, struct pollfd * pollStruct, uint8_t * buffer, ssize_t length ) {
    // Is this an error?
    if( (pollStruct->revents & POLLIN) != POLLIN || length < 1 ) {

        // If we have an active buffer, kill it now.
        khint_t iter = kh_get( int, local_buffer, pollStruct->fd );
        if( iter != kh_end( local_buffer ) ) {
            printf( "Killing local buffer for %d\n", pollStruct->fd );
            kh_value( local_buffer, iter ).buffer_tail = NULL;
            if( kh_value( local_buffer, iter ).buffer != NULL )
                free( kh_value( local_buffer, iter ).buffer );
            kh_value( local_buffer, iter ).buffer = NULL;
            khint_t delHint = kh_get( int, local_buffer, pollStruct->fd );
            kh_del( int, local_buffer, delHint );
        }

        // Kill the poll structure data
        close( pollStruct->fd );
        pollStruct->fd = -1;
        pollStruct->events = 0;
        pollStruct->revents = 0;

        log_error( "SOCKET ERROR, dropped client" );
        return;
    }

    // Try and get the local buffer reference
    khint_t iter = kh_get( int, local_buffer, pollStruct->fd );

    // If it's not there, make one!
    if( iter == kh_end( local_buffer ) ) {
        printf( "Creating new buffer space...\n" );

        int state = 0;
        iter = kh_put( int, local_buffer, pollStruct->fd, &state );
        local_buffer_t * newBuffer = &kh_value( local_buffer, iter );

        printf( "State = %d\n", state );
        assert( newBuffer != NULL, "Failed to add a new buffer!" );

        newBuffer->buffer = malloc( config.network_mtu * 20 );
        newBuffer->buffer_tail = kh_value( local_buffer, iter ).buffer;

        assert( newBuffer->buffer != NULL, "NULL buffer reference after malloc" );
        assert( newBuffer->buffer_tail != NULL, "Null tail reference after malloc" );
        assert( newBuffer->buffer == newBuffer->buffer_tail, "Tail/Buffer mismatch after malloc" );
    }

    // Update iter and pull the buffer reference
    iter = kh_get( int, local_buffer, pollStruct->fd );
    local_buffer_t * local = &kh_value( local_buffer, iter );

    assert( local->buffer != NULL, "Buffer reference was null!" );
    assert( local->buffer_tail != NULL, "Buffer tail reference was null!" );

    size_t buffer_size = (local->buffer_tail - local->buffer);
    size_t remaining_buffer = (config.network_mtu * 20) - buffer_size;

    memcpy( local->buffer_tail, buffer, length );
    local->buffer_tail += length;
    buffer_size += length;

    //printf( "Cached: %lu B\n", buffer_size );

    // Attempt to find a packet frame, then pass it on...
    ssize_t ready_bytes = 0;
    while( (ready_bytes = gnw_nextPacket( local->buffer, buffer_size )) < 0 ) {
        packet_shift( local->buffer, buffer_size, NULL, 1 );
        remaining_buffer++;
        buffer_size--;
    }

    //printf( "Bytes: %ld\n", ready_bytes );

    // After that, is there any buffer left?
    if( ready_bytes > 0 ) {
        // Isolate, copy and forward
        uint8_t packet[config.network_mtu];
        packet_shift( local->buffer, buffer_size, packet, ready_bytes );
        local->buffer_tail -= ready_bytes;

        assert( local->buffer_tail >= local->buffer, "Buffer under-run!" );

        handle_packet( pollStruct->fd, packet, ready_bytes );
    }
}

int router_process() {

    // Set up the socket server
    struct addrinfo listen_hints;

    // Set up the (empty) address hashtable
    // Tracks on GNW addresses (uint32s)
    address_table = kh_init( gnw_address_t );

    // Set up a local buffer table, to track each connection
    // Tracks on file descriptors (ints)
    local_buffer = kh_init( int );

    memset( &listen_hints, 0, sizeof listen_hints );
    listen_hints.ai_family   = AF_INET;
    listen_hints.ai_socktype = SOCK_STREAM;

    int listen_fd = getListenSocket( &listen_hints );

    if( listen( listen_fd, ROUTER_BACKLOG ) == -1 ) {
        perror( "listen" );
        exit( EXIT_FAILURE );
    }

    struct pollfd poll_list[MAX_MONITOR_FDS] = { 0 };
    for( int i=0; i<MAX_MONITOR_FDS; i++ )
        poll_list[i].fd = -1; // Mark as non-monitored

    poll_list[0].fd = listen_fd;
    poll_list[0].events = POLLIN;
    poll_list[0].revents = 0;

    uint8_t buffer[config.network_mtu];

    while( config.system_state ) {

        printf( "Known Addresses:\n" );
        khint_t iter = kh_begin( address_table );
        while( iter != kh_end( address_table ) ) {
            if( kh_exist( address_table, iter ) )
                fprintf( stderr, "\t|->\t%08x\n", kh_key( address_table, iter ) );

            iter++;
        }

        printf( "Local Buffers (%d):\n", kh_size( local_buffer ) );
        iter = kh_begin( local_buffer );
        while( iter != kh_end( local_buffer ) ) {
            if( kh_exist( local_buffer, iter ) ) {
                int fd = kh_key( local_buffer, iter );
                local_buffer_t * buffer = &kh_value( local_buffer, iter );
                ssize_t length = (buffer->buffer_tail - buffer->buffer);

                printf( "\t|->\tfd=%d, length=%ld\n", fd, length );
            }

            iter++;
        }
        printf( "\n" );

        int events = -1;
        while( (events = poll( poll_list, MAX_MONITOR_FDS, 1000 )) > 0 ) {

            // Is this an event on the listen socket?
            if( poll_list[0].revents != 0 ) {
                log_info( "New connection." );

                struct sockaddr_storage remote_socket;
                socklen_t newSock_len = sizeof( socklen_t );
                int remote_fd = accept( listen_fd, (struct sockaddr *)&remote_socket, &newSock_len );
                if( remote_fd == -1 ) {
                    perror( "accept" );
                    continue;
                }

                // Push this new socket onto a blank on the poll list
                int entry = 1;
                for( entry=1; entry<MAX_MONITOR_FDS; entry++ ) {
                    if( poll_list[entry].fd == -1 ) {
                        poll_list[entry].fd = remote_fd;
                        poll_list[entry].events = POLLIN;
                        break;
                    }
                }

                if( poll_list[entry].fd != remote_fd ) {
                    log_warn( "No more free sockets, dropping the new one." );
                    close( remote_fd );
                    poll_list[0].revents = 0;
                    continue;
                }

                // Manual reset. Not sure if we actually need this...
                poll_list[0].revents = 0;
                continue;
            }

            // If we're here, it must be one of the other monitored fd's
            for( int i=1; i<MAX_MONITOR_FDS; i++ ) {

                // Skip any empty FDs
                if( poll_list[i].fd == -1 )
                    continue;

                // Handle any events on this fd
                if( poll_list[i].revents != 0 ) {

                    // If the socket is gone, just drop the fd and continue the scan.
                    if( (poll_list[i].revents & POLLHUP) == POLLHUP ) {

                        // If we have an active buffer, kill it now.
                        khint_t iter = kh_get( int, local_buffer, poll_list[i].fd );
                        if( iter != kh_end( local_buffer ) ) {
                            printf( "Killing local buffer for %d\n", poll_list[i].fd );
                            kh_value( local_buffer, iter ).buffer_tail = NULL;
                            if( kh_value( local_buffer, iter ).buffer != NULL )
                                free( kh_value( local_buffer, iter ).buffer );

                            kh_value( local_buffer, iter ).buffer = NULL;
                            khint_t delHint = kh_get( int, local_buffer, poll_list[i].fd );
                            kh_del( int, local_buffer, delHint );
                        }

                        // Kill the poll structure data
                        close( poll_list[i].fd );
                        poll_list[i].fd = -1;
                        poll_list[i].events = 0;
                        poll_list[i].revents = 0;

                        continue;
                    }

                    // Reset the buffer, just in case (drop this for speed, but danger!)
                    memset( buffer, 0, config.network_mtu );

                    // Pull any bytes on the wire
                    ssize_t length = read( poll_list[i].fd, buffer, config.network_mtu );

                    // Push the event to the event handler.
                    handle_event( i, &(poll_list[i]), buffer, length );

                    // Reset the event flags, and continue.
                    poll_list[i].revents = 0;
                }
            }
        }
    }

    kh_destroy( gnw_address_t, address_table );

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