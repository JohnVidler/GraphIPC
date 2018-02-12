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
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <poll.h>
#include <getopt.h>
#include <pthread.h>
#include <wait.h>
#include "lib/GraphNetwork.h"
#include "common.h"
#include "lib/LinkedList.h"
#include "Log.h"

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0

extern char **environ;

// Configurable Options //
struct _configuration {
    int            network_mtu;     // default = 1500
    unsigned int   read_timeout;    // default = 5
    RingBuffer_t * rx_buffer;
    gnw_state_t *  parser_context;

    unsigned int   arg_model;

    char *         arg_host;
    char *         arg_port;
    gnw_address_t  arg_address;
    bool           arg_immediate;
    unsigned int   arg_verbosity;
};
struct _configuration config;

typedef struct {
    pthread_t     thread_context;
    gnw_address_t stream_address;
    int wrap_stdin[2];
    int wrap_stdout[2];

    char * binary;
    char ** binary_arguments;
} sink_context_t;
linked_list_t * sink_context_list;
pthread_mutex_t sink_context_list_mutex;

int router_fd = -1;
int getRouterFD() {
    if( router_fd == -1 ) {
        log_info( "Connecting to router at %s:%s...", config.arg_host, config.arg_port );
        router_fd = socket_connect(config.arg_host, config.arg_port);

        // Build the other network-related structures
        config.parser_context = malloc( sizeof(gnw_state_t) );
        config.rx_buffer = ringbuffer_init( config.network_mtu * 20 ); // 20 packet(ish) buffer
    }
    return router_fd;
}

gnw_address_t getNodeAddress( gnw_address_t try_address ) {
    int router = getRouterFD();

    gnw_address_t new_address = 0;

    // Ask for an address
    if( try_address == 0 ) {
        log_info( "Requesting an address from the router..." );
        gnw_sendCommand(router, GNW_CMD_NEW_ADDRESS);
    }
    else {
        // Bit of a hack - send the requested address with the new address request... Might just be ignored ar the router, though!
        log_info( "Requesting address [%08x] from the router...", try_address );
        unsigned char reqBuffer[sizeof(gnw_address_t)+1] = { GNW_CMD_NEW_ADDRESS, 0 };
        memcpy( reqBuffer+1, &try_address, sizeof(gnw_address_t) );
        gnw_emitCommandPacket( router, GNW_COMMAND, reqBuffer, sizeof(gnw_address_t) + 1 );
    }

    unsigned char iBuffer[config.network_mtu];
    while( new_address == 0 ) {
        ssize_t bytesRead = read( router, iBuffer, config.network_mtu );
        if( bytesRead <= 0 ) {
            log_error( "IO Error, halting! (1)" );
            exit( EXIT_FAILURE ); // Hard exit here, as return fails to... well, fail. -John
        }
        if( ringbuffer_write( config.rx_buffer, iBuffer, bytesRead ) != bytesRead )
            log_warn( "Buffer overflow! Data loss occurred!" );

        while( gnw_nextPacket( config.rx_buffer, config.parser_context, iBuffer ) ) {
            gnw_header_t * header = (gnw_header_t *)iBuffer;
            unsigned char * payload = iBuffer + sizeof(gnw_header_t);

            // Uncomment for debug output
            //gnw_dumpPacket( stdout, iBuffer, -1 );

            if( header->version != GNW_VERSION )
                log_warn( "Warning! Router/Client version mismatch!" );

            switch( header->type ) {
                case GNW_COMMAND | GNW_REPLY: // Command response
                    if( header->length < 1 ) {
                        log_warn( "Router sent a command with no operator, no idea what to do! Trying to skip past it..." );
                        break;
                    }

                    switch( *payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            if( new_address == 0 ) {
                                new_address = *(gnw_address_t *)(payload + 1);

                                if( new_address != try_address )
                                    log_warn( "Router refused our address request, actually got %08x", new_address );
                            }
                            break;

                        default:
                            log_warn( "Unknown command response? (%u)", (unsigned char)(*payload) );
                            break;
                    }

                    break;

                case GNW_DATA:
                    if( header->length == 0 )
                        log_warn("Router keepalive received (lossy connection?)");
                    break;

                default:
                    log_warn( "Unknown response from router, skipping. (%x)", header->type );
            }
        }
    }

    log_info( "Address is now [%08x]", new_address );

    return new_address;
}

int dropRouterFD() {
    if( router_fd != -1 ) {
        log_info( "Disconnecting from router..." );
        ringbuffer_destroy(config.rx_buffer);
        free(config.parser_context);
        close(router_fd);
    }
}

int mode_input() {
    int data_fd = getRouterFD();

    // Become a node (aka. get an address)
    gnw_address_t node_address = getNodeAddress( config.arg_address );

    // Reset the request for an address, irrespective if we got it or not, otherwise we'll keep asking for the same one
    config.arg_address = 0;

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) );

    watch_fd[0].fd = STDIN_FILENO;
    watch_fd[0].events = POLLIN;

    char buffer[config.network_mtu];

    int rv = 0;
    while( (rv = poll(watch_fd, 1, 1000)) != -1 ) {
        if( rv > 0 ) {
            watch_fd[0].revents = 0;

            memset( buffer, 0, config.network_mtu );
            ssize_t readBytes = read( watch_fd[0].fd, buffer, config.network_mtu );

            if( readBytes == 0 ) {
                log_error( "Input shut down, stopping." );
                break;
            }

            gnw_emitDataPacket( data_fd, node_address, buffer, readBytes );
        }
    }

    // Done, close the FD
    dropRouterFD();
    return EXIT_SUCCESS;
}

int mode_output() {
    int data_fd = getRouterFD();

    // Become a node (aka. get an address)
    gnw_address_t node_address = getNodeAddress( config.arg_address );

    // Reset the request for an address, irrespective if we got it or not, otherwise we'll keep asking for the same one
    config.arg_address = 0;

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) );

    watch_fd[0].fd = data_fd;
    watch_fd[0].events = POLLIN;

    char buffer[config.network_mtu+1]; // This '+1' is here to always have a null terminator in the line marked 'kludge' below...

    int rv = 0;
    while( (rv = poll(watch_fd, 1, 1000)) != -1 ) {
        if( rv > 0 ) {
            watch_fd[0].revents = 0;

            ssize_t bytesRead = read( watch_fd[0].fd, buffer, config.network_mtu );
            if( bytesRead <= 0 ) {
                perror( "read" );
                log_error( "IO Error, halting! (2)" );
                return EXIT_FAILURE;
            }
            if( ringbuffer_write( config.rx_buffer, buffer, bytesRead ) != bytesRead )
                log_error( "Buffer overflow! Data loss occurred!" );

            while( gnw_nextPacket( config.rx_buffer, config.parser_context, buffer ) ) {
                gnw_header_t *header = (gnw_header_t *) buffer;
                unsigned char *payload = (unsigned char *) (buffer + sizeof(gnw_header_t));
                *(payload + header->length) = '\0'; // Kludge?

                if (header->version != GNW_VERSION)
                    log_warn( "Warning! Router/Client version mismatch!" );

                if( header->type == GNW_DATA ) {
                    fprintf( stdout, "%s", payload );
                    fflush( stdout );
                } else {
                    log_warn( "Warning! Router is emitting non-data packets in data mode!" );
                }
            }
        }
    }

    // Done, close the FD
    dropRouterFD();
    return EXIT_SUCCESS;
}

/**
 * Exec into a new process, binding the stdin and stdout pipes to the graph network.
 *
 * Should never return under normal operation, but may return '-1' if an error occurred
 * during the setup phase. The error text will be reported on stderr for resident
 * processes, or the syslog for daemon ones.
 *
 * @param wrap_stdin The file descriptor to use for this process' stdin stream
 * @param wrap_stdout The file descriptor to use for this process' stdout stream
 * @param cmd The executable to become when exec-ing
 * @param argv Any arguments to pass to the new process
 * @return -1 on an error, or never returns
 */
int processRunner( int wrap_stdin, int wrap_stdout, const char * cmd, char ** argv ) {
    // Capture stdout to wrap_stdout
    dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    log_debug( "Process started" );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        log_error( "Process terminated with error: %s", strerror(errno) );

    return -1;
}

void * sink_thread( void * _context ) {
    sink_context_t * context = (sink_context_t *)_context;

    log_debug( "Sink started!" );

    // Become two actual processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 ) {
        processRunner(PIPE_READ(context->wrap_stdin), PIPE_WRITE(context->wrap_stdout), context->binary, context->binary_arguments);
        return NULL; // Should never happen...
    }

    log_debug( "CHILD PID:\t%d", childPID );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 2 );

    watch_fd[0].fd = PIPE_READ(context->wrap_stdout);
    watch_fd[0].events = POLLIN;

    int rfd = getRouterFD(); // Note: we don't need to close this, the host binary will do this for us!
    context->stream_address = getNodeAddress( 0 );

    int status = 0;
    while( status == 0 ) {
        int rv = poll( watch_fd, 1, config.read_timeout * 1000 );

        if( rv == -1 ) {        // Error state while poll'ing
            log_error( "Error during poll cycle for input. Shutting down." );
            break;
        } else if( rv == 0 ) {  // Timed out on poll wait

            // Has the wrapped process exited/died?
            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                log_error( "Process terminated (exit code = %d)", WEXITSTATUS(status));
                status = 1;
                break;
            }
        } else {  // Data ready from the process itself.

            // Handle any events on the process output
            if( watch_fd[0].revents != 0 ) {
                if (watch_fd[0].revents & POLLIN == POLLIN) {
                    unsigned char buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[0].fd, &buffer, config.network_mtu);

                    if( config.arg_verbosity > 0 )
                        fprintf( stderr, ">>>\t%s\n", buffer );

                    gnw_emitDataPacket( rfd, context->stream_address, buffer, bytesRead ); // Always emit from the node source address, not the stream source address
                }
            }

            watch_fd[0].revents = 0;
        }
    }

    log_warn( "Sink died - no way to handle this right now!" );
    pthread_exit(0); // Just stop
}

/**
 * Tries to find a sink context based on the source address.
 *
 * This is slightly magic in one particular case - if the application is in 'immediate mode' it doesn't know what the
 * source address of the first stream is before it <strong>must</strong> run the inner binary, so it creates a context
 * where the stream address is zero - ie. the invalid stream, then when the first message from <strong>any</strong>
 * stream comes in, this function grabs the context with a zero stream address, fixes the address, then passes it back
 * to the caller, now as a completely valid context.
 *
 * This might be useful later for stream re-use, if there is a sensible way to handle disconnects, which at the time
 * of writing (version 1.0.0, as it were) disconnections are silent for the sink endpoints.
 *
 * Messy, but it seems to be the most sane way to handle this odd edge case for now. -John.
 *
 * @param needle The address to find a context for
 * @return The matching sink_context_t pointer, or NULL if no context matches
 */
sink_context_t * getSinkContext( gnw_address_t needle ) {
    pthread_mutex_lock( &sink_context_list_mutex );

    struct list_element * iter = sink_context_list->head;
    while( iter != NULL ) {
        if( ((sink_context_t *)iter->data)->stream_address == needle || ((sink_context_t *)iter->data)->stream_address == 0 ) {

            // If we have a context with no valid stream address, fix that, and use it as this one.
            if( ((sink_context_t *)iter->data)->stream_address == 0 )
                ((sink_context_t *)iter->data)->stream_address = needle;

            pthread_mutex_unlock( &sink_context_list_mutex ); // Note: This might be thread-unsafe... not sure. -John
            return iter->data;
        }
        iter = iter->next;
    }

    pthread_mutex_unlock( &sink_context_list_mutex );
    return NULL;
}

sink_context_t * createNewSink( char * binary, char ** arguments, gnw_address_t source ) {
    sink_context_t * sink_context = malloc( sizeof(sink_context_t) );
    sink_context->stream_address   = source;
    sink_context->binary           = binary;
    sink_context->binary_arguments = arguments;

    // Build a new set of redirected pipes
    if( pipe( sink_context->wrap_stdin ) == -1 ) {
        log_error( "Could not create wrapper for stdin");
        exit( EXIT_FAILURE );
    }

    if( pipe( sink_context->wrap_stdout ) == -1 ) {
        log_error( "Could not create wrapper for stdout");
        exit( EXIT_FAILURE );
    }

    // Spin up a new instance!
    int result = pthread_create( &(sink_context->thread_context), NULL, sink_thread, sink_context );
    if( result != 0 ) {
        log_error( "Could not start up a new sink thread. Cannot continue.");
        exit( EXIT_FAILURE );
    }
    pthread_detach( sink_context->thread_context );

    // Add it in to the list
    pthread_mutex_lock( &sink_context_list_mutex );
    ll_append( sink_context_list, sink_context );
    pthread_mutex_unlock( &sink_context_list_mutex );

    return sink_context;
}

#define ARG_HELP       0
#define ARG_USAGE      1
#define ARG_HOST       2
#define ARG_PORT       3
#define ARG_ADDRESS    4
#define ARG_UPSTREAM   5
#define ARG_DOWNSTREAM 6
#define ARG_POLICY     7
#define ARG_INPUT      8
#define ARG_OUTPUT     9
#define ARG_IMMEDIATE  10

int main(int argc, char ** argv ) {
    // Configuration defaults
    config.network_mtu     = 1500;
    config.read_timeout    = 5;

    config.arg_host = "127.0.0.1";
    config.arg_port = (char *) ROUTER_PORT;

    config.arg_address = 0;
    config.arg_immediate = false;
    config.arg_verbosity = 0;

    // Check the system MTU and match it - this assumes local operation, for now.
    // Note: Possibly add an override for this in the flags...
    config.network_mtu = getIFaceMTU( "lo" );

    if( config.network_mtu > 4096 ) // Excessive copy op.
        config.network_mtu = 1024;

    struct option longOptions[12] = {
            [ARG_HELP]       = { .name="help",       .has_arg=no_argument,       .flag=NULL },
            [ARG_USAGE]      = { .name="usage",      .has_arg=no_argument,       .flag=NULL },
            [ARG_HOST]       = { .name="host",       .has_arg=required_argument, .flag=NULL },
            [ARG_PORT]       = { .name="port",       .has_arg=required_argument, .flag=NULL },
            [ARG_ADDRESS]    = { .name="address",    .has_arg=required_argument, .flag=NULL },
            [ARG_UPSTREAM]   = { .name="upstream",   .has_arg=required_argument, .flag=NULL },
            [ARG_DOWNSTREAM] = { .name="downstream", .has_arg=required_argument, .flag=NULL },
            [ARG_POLICY]     = { .name="policy",     .has_arg=required_argument, .flag=NULL },
            [ARG_INPUT]      = { .name="input",      .has_arg=no_argument,       .flag=NULL },
            [ARG_OUTPUT]     = { .name="output",     .has_arg=no_argument,       .flag=NULL },
            [ARG_IMMEDIATE]  = { .name="immediate",  .has_arg=no_argument,       .flag=NULL },
            0
    };

    // Argument Parsing //
    int arg;
    int indexPtr = 0;
    while ((arg = getopt_long(argc, argv, "u:d:p:h:a:iov", longOptions, &indexPtr)) != -1) {

        // If we have a short arg, pass it over to the long arg index.
        // Note: This will work assuming we have less than 65(?) long arguments... I think -John.
        if( arg > 0 )
            indexPtr = arg;

        switch (indexPtr) {
            case ARG_HELP:
            case ARG_USAGE: // ToDo: Update the argument list with the new argument options
                printf( "GraphWrap\n" );
                printf( ANSI_COLOR_GREEN "\t\t\"Don't cross the streams\" --Egon Spengler.\n\n" ANSI_COLOR_RESET );
                printf( "Wrap a normal Linux process stdin/stdout pipes with GraphIPC connections to a GraphRouter process\nAllows non-compliant programs to be used in a graph\n\n" );
                printf( ANSI_COLOR_CYAN "--help --usage\n" ANSI_COLOR_RESET "\tShow this help message\n\n" );
                printf( ANSI_COLOR_CYAN "-h --host [host address]\n" ANSI_COLOR_RESET "\tGraphRouter host address\n\n" );
                printf( ANSI_COLOR_CYAN "-p --port [port]\n" ANSI_COLOR_RESET "\tGraphRouter port\n\n" );
                printf( ANSI_COLOR_CYAN "-a --address [hex address]\n" ANSI_COLOR_RESET "\tThe (requested) hexadecimal node address, may not be respected by the router. Cannot be 0\n\n" );
                printf( ANSI_COLOR_CYAN "-u --upstream [graph address]\n" ANSI_COLOR_RESET "\tConnect this node to the upstream address (receive data from this address).\n\tThis argument can be repeated\n\n" );
                printf( ANSI_COLOR_CYAN "-d --downstream [graph address]\n" ANSI_COLOR_RESET "\tConnect this node to the downstream address (send data to this address).\n\tThis argument can be repeated\n\n" );
                printf( ANSI_COLOR_CYAN "--policy [broadcast|anycast|roundrobin]\n" ANSI_COLOR_RESET "\tSet the forward policy for this node\n\n" );
                printf( ANSI_COLOR_CYAN "--immediate\n" ANSI_COLOR_RESET "\tStart running the inner binary immediately. By default wrapped processes are only started on demand when data arrives\n\n" );
                printf( ANSI_COLOR_CYAN "-v\n" ANSI_COLOR_RESET "\tIncrease verbosity, repeat for increasing levels of detail\n\n" );
                printf( ANSI_COLOR_CYAN "--\n" ANSI_COLOR_RESET "\tOptional separator between GraphWrap arguments and the inner binary\n\n" );
                printf( "The first non-flag argument to this binary will be used as the inner process to wrap.\nAny subsequent arguments are then passed to the inner binary.\n\n" );
                printf( "Example:\n\tGraphWrap -d 2 -u 1 -- innerProcess --processArg1 -processArg2\n" );
                break;

            case 'h':
            case ARG_HOST:
                config.arg_host = optarg;
                break;

            case 'p':
            case ARG_PORT:
                config.arg_port = optarg;
                break;

            case 'a':
            case ARG_ADDRESS:
                config.arg_address = (gnw_address_t)strtoul( optarg, NULL, 16 );
                break;

            case 'u':
            case ARG_UPSTREAM: {
                /*int rfd = getRouterFD();
                if (rfd == -1) {
                    log_error( "Could not connect to the router. STOP.");
                    exit(EXIT_FAILURE);
                }
                gnw_address_t target = (gnw_address_t)strtoul( optarg, NULL, 16 );

                log_info( "Connecting %x -> %x", target, config.graph_address );

                gnw_request_connect( rfd, target, config.graph_address );*/
                log_warn( "UNIMPLEMENTED: UPSTREAM" );
                break;
            }

            case 'd':
            case ARG_DOWNSTREAM: {
                /*int rfd = getRouterFD();
                if (rfd == -1) {
                    log_error( "Could not connect to the router. STOP.");
                    exit(EXIT_FAILURE);
                }
                gnw_address_t target = (gnw_address_t)strtoul( optarg, NULL, 16 );

                log_info( "Connecting %x -> %x", config.graph_address, target );

                gnw_request_connect( rfd, config.graph_address, target );*/
                log_warn( "UNIMPLEMENTED: DOWNSTREAM" );
                break;
            }

            case ARG_POLICY: { // Note: This functionality should probably be made in to a call in the gnw_ library -John
                /*int rfd = getRouterFD();
                if (rfd == -1) {
                    log_error( "Could not connect to the router. STOP.");
                    exit(EXIT_FAILURE);
                }
                unsigned char buffer[2 + sizeof(gnw_address_t)];
                *buffer = GNW_CMD_POLICY;

                if (strncmp(optarg, "broadcast", 9) == 0)
                    *(buffer + 1) = GNW_POLICY_BROADCAST;
                else if (strncmp(optarg, "roundrobin", 10) == 0)
                    *(buffer + 1) = GNW_POLICY_ROUNDROBIN;
                else if (strncmp(optarg, "anycast", 7) == 0)
                    *(buffer + 1) = GNW_POLICY_ANYCAST;

                *(gnw_address_t *) (buffer + 2) = config.graph_address;

                gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 2 + sizeof(gnw_address_t));*/
                log_warn( "UNIMPLEMENTED: POLICY" );
                break;
            }

            case 'i':
            case ARG_INPUT:
                return mode_input();

            case 'o':
            case ARG_OUTPUT:
                return mode_output();

            case ARG_IMMEDIATE:
                config.arg_immediate = true;
                break;

            case 'v':
                config.arg_verbosity++;

                if( config.arg_verbosity == 2 )
                    log_setLevel( INFO );
                else if( config.arg_verbosity == 3 )
                    log_setLevel( DEBUG );

                break;

            default:
                log_warn( "Unknown argument: %d, ignored", indexPtr );
        }
    }

    // If we have other arguments to parse, then we can assume we're actually wrapping a process now.
    if( argc - optind > 0 ) {

        // Going live, build any remaining structures
        sink_context_list = ll_create();
        pthread_mutex_init( &sink_context_list_mutex, NULL );

        // Connect now, if we haven't already done so...
        int rfd = getRouterFD();
        if (rfd == -1) {
            log_error( "Could not connect to the router. STOP.");
            exit(EXIT_FAILURE);
        }

        const int inner_binary_index = optind;
        const int inner_binary_argc = argc - optind;
        char inner_binary[2048] = {0};

        // Search using the system path for the 'real' binary
        findRealPath(inner_binary, argv[inner_binary_index]);

        char *newArgs[inner_binary_argc];
        for (int i = 0; i < inner_binary_argc; i++) {
            newArgs[i] = argv[inner_binary_index + i];
        }
        newArgs[inner_binary_argc] = NULL;

        // If we're in "immediate mode" - we need to start the inner binary *now* for some tool-related reason
        // Address zero is invalid, but will be replaced when the first stream comes in.
        // See the documentation for createNewSink( ... ) for details.
        if( config.arg_immediate ) {
            sink_context_t * null_sink = createNewSink(inner_binary, newArgs, 0);

            write( null_sink->wrap_stdin[0], "Demo text\n", 10 ); //<-- What did I do this for? -John
        }

        // ToDO: This should wait on data from the router, and pass it to the relevant stream sink
        // Each sink then is responsible for sending their own packets back up to the router to be
        // independently handled by the thread.

        // Begin pulling data from the router, hand off any new streams
        unsigned char iBuffer[config.network_mtu+1];

        ssize_t bytes_read = -1;
        bool backoff = false;
        int status = 0;
        while( status == 0 ) {
            if( !backoff ) {
                bytes_read = read(rfd, iBuffer, config.network_mtu);
                if (bytes_read < 0) {
                    log_error( "IO Error, did the router crash?" );
                    dropRouterFD();
                    exit(EXIT_FAILURE);
                }
            }

            // Note: This may explode if for some reason we're in backoff state before we start reading!
            backoff = false;
            if( ringbuffer_write( config.rx_buffer, iBuffer, (unsigned)bytes_read ) != bytes_read )
                backoff = true;

            while( gnw_nextPacket( config.rx_buffer, config.parser_context, iBuffer ) ) {
                gnw_header_t * header = (gnw_header_t *)iBuffer;
                unsigned char * payload = (unsigned char *)( iBuffer + sizeof(gnw_header_t) );

                if( header->version != GNW_VERSION )
                    log_warn( "Router/Client version mismatch, things may break horribly." );

                switch( header->type ) {
                    case GNW_DATA: {
                        sink_context_t *sink_context = getSinkContext(header->source);

                        // If we don't have a context, its time to spin up a new instance! (or steal a null-address context)
                        // See the documentation for createNewSink( ... ) for details.
                        if (sink_context == NULL)
                            sink_context = createNewSink( inner_binary, newArgs, header->source );

                        *(payload + header->length) = '\0'; // Kludge?

                        if( config.arg_verbosity > 0 )
                            fprintf( stderr, "<<<\t%d B\t%s\n", header->length, payload );

                        // By now, we should always have a valid sink context to work with.
                        if( write( PIPE_WRITE(sink_context->wrap_stdin), payload, header->length ) != header->length )
                            log_warn( "Unable to push the entire payload to the sink, data has been lost - internal error?" );

                        break;
                    }

                    default:
                        log_warn( "Unknown or unexpected message type (%d), skipped.", header->type );
                }
            }
        }

    }

    dropRouterFD();

    return EXIT_SUCCESS;
}