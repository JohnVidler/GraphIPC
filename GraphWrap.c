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

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0

extern char **environ;

// Configurable Options //
struct _configuration {
    int            network_mtu;     // default = 1500
    unsigned int   read_timeout;    // default = 5
    gnw_address_t  graph_address;   // default = 0
    RingBuffer_t * rx_buffer;
    gnw_state_t *  parser_context;

    char *        arg_host;
    char *        arg_port;
    bool          arg_immediate;
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
        fprintf( stderr, "Connecting to router at %s:%s...\n", config.arg_host, config.arg_port );
        router_fd = socket_connect(config.arg_host, config.arg_port);

        // Build the other network-related structures
        config.parser_context = malloc( sizeof(gnw_state_t) );
        config.rx_buffer = ringbuffer_init( config.network_mtu * 20 ); // 20 packet(ish) buffer

        // Get an address from the router, too
        unsigned char iBuffer[config.network_mtu];
        while( config.graph_address == 0 ) {
            // Ask for an address
            fprintf( stderr, "Requesting an address from the router...\n" );
            gnw_sendCommand( router_fd, GNW_CMD_NEW_ADDRESS );

            ssize_t bytesRead = read( router_fd, iBuffer, config.network_mtu );
            if( bytesRead <= 0 ) {
                fprintf( stderr, "IO Error, halting!\n" );
                return EXIT_FAILURE;
            }
            if( ringbuffer_write( config.rx_buffer, iBuffer, bytesRead ) != bytesRead )
                fprintf( stderr, "Buffer overflow! Data loss occurred!" );

            while( gnw_nextPacket( config.rx_buffer, config.parser_context, iBuffer ) ) {
                gnw_header_t * header = (gnw_header_t *)iBuffer;
                unsigned char * payload = iBuffer + sizeof(gnw_header_t);

                // Uncomment for debug output
                //gnw_dumpPacket( stdout, iBuffer, -1 );

                if( header->version != GNW_VERSION )
                    fprintf( stderr, "Warning! Router/Client version mismatch!\n" );

                switch( header->type ) {
                    case GNW_COMMAND | GNW_REPLY: // Command response
                        if( header->length < 1 ) {
                            fprintf( stderr, "Router sent a command with no operator, no idea what to do! Trying to skip past it...\n" );
                            break;
                        }

                        switch( *payload ) {
                            case GNW_CMD_NEW_ADDRESS:
                                if( config.graph_address == 0 ) {
                                    config.graph_address = *(gnw_address_t *)(payload + 1);

                                    printf( "?> %x\n", config.graph_address );

                                    //config.graph_address = 0xFFFFFFFF & config.graph_address;
                                }
                                break;

                            default:
                                fprintf( stderr, "Unknown command response?\n" );
                                break;
                        }

                        break;

                    case GNW_DATA:
                        if( header->length == 0 )
                            fprintf( stderr, "Router keepalive received (lossy connection?)\n" );
                        break;

                    default:
                        fprintf( stderr, "Unknown response from router, skipping. (%x)\n", header->type );
                }
            }
        }

        fprintf( stderr, "Address is %x\n", config.graph_address );
    }
    return router_fd;
}

int dropRouterFD() {
    if( router_fd != -1 ) {
        fprintf( stderr, "Disconnecting from router...\n" );
        ringbuffer_destroy(config.rx_buffer);
        free(config.parser_context);
        close(router_fd);
    }
}

int mode_input() {
    int data_fd = getRouterFD();

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
                fprintf( stderr, "Input shut down, stopping." );
                break;
            }

            gnw_emitDataPacket( data_fd, config.graph_address, buffer, readBytes );
        }
    }

    // Done, close the FD
    dropRouterFD();
    return EXIT_SUCCESS;
}

int mode_output() {
    int data_fd = getRouterFD();

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
                fprintf( stderr, "IO Error, halting!\n" );
                return EXIT_FAILURE;
            }
            if( ringbuffer_write( config.rx_buffer, buffer, bytesRead ) != bytesRead )
                fprintf( stderr, "Buffer overflow! Data loss occurred!" );

            while( gnw_nextPacket( config.rx_buffer, config.parser_context, buffer ) ) {
                gnw_header_t *header = (gnw_header_t *) buffer;
                unsigned char *payload = (unsigned char *) (buffer + sizeof(gnw_header_t));
                *(payload + header->length) = '\0'; // Kludge?

                if (header->version != GNW_VERSION)
                    fprintf(stderr, "Warning! Router/Client version mismatch!\n");

                if( header->type == GNW_DATA ) {
                    fprintf( stdout, "%s", payload );
                    fflush( stdout );
                } else {
                    fprintf(stderr, "Warning! Router is emitting non-data packets in data mode!\n");
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

    fprintf( stderr, "Process started\n" );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        fprintf( stderr, "Process terminated with error: %s\n", strerror(errno) );

    return -1;
}

void * sink_thread( void * _context ) {
    sink_context_t * context = (sink_context_t *)_context;

    printf( "Sink started!\n" );

    // Become two actual processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 ) {
        processRunner(PIPE_READ(context->wrap_stdin), PIPE_WRITE(context->wrap_stdout), context->binary, context->binary_arguments);
        return NULL; // Should never happen...
    }

    printf( "CHILD PID:\t%d\n", childPID );

    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 2 );

    watch_fd[0].fd = PIPE_READ(context->wrap_stdout);
    watch_fd[0].events = POLLIN;

    int rfd = getRouterFD(); // Note: we don't need to close this, the host binary will do this for us!

    int status = 0;
    while( status == 0 ) {
        int rv = poll( watch_fd, 1, config.read_timeout * 1000 );

        if( rv == -1 ) {        // Error state while poll'ing
            fprintf( stderr, "Error during poll cycle for input. Shutting down.\n" );
            break;
        } else if( rv == 0 ) {  // Timed out on poll wait

            // Has the wrapped process exited/died?
            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                fprintf(stderr, "Process terminated (exit code = %d)\n", WEXITSTATUS(status));
                status = 1;
                break;
            }
        } else {  // Data ready from the process itself.

            // Handle any events on the process output
            if( watch_fd[0].revents != 0 ) {
                if (watch_fd[0].revents & POLLIN == POLLIN) {
                    unsigned char buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[0].fd, &buffer, config.network_mtu);

                    fprintf( stderr, ">>>\t%s\n", buffer );

                    gnw_emitDataPacket( rfd, config.graph_address, buffer, bytesRead ); // Always emit from the node source address, not the stream source address
                }
            }

            watch_fd[0].revents = 0;
        }
    }

    fprintf( stderr, "WARNING: Sink died - no way to handle this right now!\n" );
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
        fprintf(stderr, "Could not create wrapper for stdin\n");
        exit( EXIT_FAILURE );
    }

    if( pipe( sink_context->wrap_stdout ) == -1 ) {
        fprintf(stderr, "Could not create wrapper for stdout\n");
        exit( EXIT_FAILURE );
    }

    // Spin up a new instance!
    int result = pthread_create( &(sink_context->thread_context), NULL, sink_thread, sink_context );
    if( result != 0 ) {
        fprintf(stderr, "Could not start up a new sink thread. Cannot continue.\n");
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
#define ARG_UPSTREAM   4
#define ARG_DOWNSTREAM 5
#define ARG_POLICY     6
#define ARG_INPUT      7
#define ARG_OUTPUT     8
#define ARG_IMMEDIATE  9

int main(int argc, char ** argv ) {
    // Configuration defaults
    config.network_mtu     = 1500;
    config.read_timeout    = 5;
    config.graph_address   = 0;

    config.arg_host = "127.0.0.1";
    config.arg_port = (char *) ROUTER_PORT;

    config.arg_immediate = false;

    // Check the system MTU and match it - this assumes local operation, for now.
    // Note: Possibly add an override for this in the flags...
    config.network_mtu = getIFaceMTU( "lo" );

    if( config.network_mtu > 4096 ) // Excessive copy op.
        config.network_mtu = 1024;

    struct option longOptions[11] = {
            [ARG_HELP]       = { .name="help",       .has_arg=no_argument,       .flag=NULL },
            [ARG_USAGE]      = { .name="usage",      .has_arg=no_argument,       .flag=NULL },
            [ARG_HOST]       = { .name="host",       .has_arg=required_argument, .flag=NULL },
            [ARG_PORT]       = { .name="port",       .has_arg=required_argument, .flag=NULL },
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
    while ((arg = getopt_long(argc, argv, "u:d:p:h:io", longOptions, &indexPtr)) != -1) {

        // If we have a short arg, pass it over to the long arg index.
        // Note: This will work assuming we have less than 65(?) long arguments... I think -John.
        if( arg > 0 )
            indexPtr = arg;

        switch (indexPtr) {
            case ARG_HELP:
            case ARG_USAGE:
                printf( "GraphWrap\n" );
                printf( ANSI_COLOR_GREEN "\t\t\"Don't cross the streams\" --Egon Spengler.\n\n" ANSI_COLOR_RESET );
                printf( "Wrap a normal Linux process stdin/stdout pipes with GraphIPC connections to a GraphRouter process\nAllows non-compliant programs to be used in a graph\n\n" );
                printf( ANSI_COLOR_CYAN "--help --usage\n" ANSI_COLOR_RESET "\tShow this help message\n\n" );
                printf( ANSI_COLOR_CYAN "-h --host [host address]\n" ANSI_COLOR_RESET "\tGraphRouter host address\n\n" );
                printf( ANSI_COLOR_CYAN "-p --port [port]\n" ANSI_COLOR_RESET "\tGraphRouter port\n\n" );
                printf( ANSI_COLOR_CYAN "-u --upstream [graph address]\n" ANSI_COLOR_RESET "\tConnect this node to the upstream address (receive data from this address).\n\tThis argument can be repeated\n\n" );
                printf( ANSI_COLOR_CYAN "-d --downstream [graph address]\n" ANSI_COLOR_RESET "\tConnect this node to the downstream address (send data to this address).\n\tThis argument can be repeated\n\n" );
                printf( ANSI_COLOR_CYAN "--policy [broadcast|anycast|roundrobin]\n" ANSI_COLOR_RESET "\tSet the forward policy for this node\n\n" );
                printf( ANSI_COLOR_CYAN "--immediate\n" ANSI_COLOR_RESET "\tStart running the inner binary immediately. By default wrapped processes are only started on demand when data arrives\n\n" );
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

            case 'u':
            case ARG_UPSTREAM: {
                int rfd = getRouterFD();
                if (rfd == -1) {
                    fprintf(stderr, "Could not connect to the router. STOP.\n");
                    exit(EXIT_FAILURE);
                }
                gnw_address_t target = (gnw_address_t)strtoul( optarg, NULL, 16 );

                fprintf( stderr, "Connecting %x -> %x\n", target, config.graph_address );

                gnw_request_connect( rfd, target, config.graph_address );
                break;
            }

            case 'd':
            case ARG_DOWNSTREAM: {
                int rfd = getRouterFD();
                if (rfd == -1) {
                    fprintf(stderr, "Could not connect to the router. STOP.\n");
                    exit(EXIT_FAILURE);
                }
                gnw_address_t target = (gnw_address_t)strtoul( optarg, NULL, 16 );

                fprintf( stderr, "Connecting %x -> %x\n", config.graph_address, target );

                gnw_request_connect( rfd, config.graph_address, target );
                break;
            }

            case ARG_POLICY: { // Note: This functionality should probably be made in to a call in the gnw_ library -John
                int rfd = getRouterFD();
                if (rfd == -1) {
                    fprintf(stderr, "Could not connect to the router. STOP.\n");
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

                gnw_emitCommandPacket(rfd, GNW_COMMAND, buffer, 2 + sizeof(gnw_address_t));
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

            default:
                fprintf( stderr, "Unknown argument: %d\n", indexPtr );
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
            fprintf(stderr, "Could not connect to the router. STOP.\n");
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

            write( null_sink->wrap_stdin[0], "Demo text\n", 10 );
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
                    fprintf(stderr, "IO Error, did the router crash?\n");
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
                    fprintf( stderr, "WARNING: Router/Client version mismatch, things may break horribly.\n" );

                switch( header->type ) {
                    case GNW_DATA: {
                        sink_context_t *sink_context = getSinkContext(header->source);

                        // If we don't have a context, its time to spin up a new instance! (or steal a null-address context)
                        // See the documentation for createNewSink( ... ) for details.
                        if (sink_context == NULL)
                            sink_context = createNewSink( inner_binary, newArgs, header->source );

                        *(payload + header->length) = '\0'; // Kludge?

                        fprintf( stderr, "<<<\t%d B\t%s\n", header->length, payload );

                        // By now, we should always have a valid sink context to work with.
                        if( write( PIPE_WRITE(sink_context->wrap_stdin), payload, header->length ) != header->length )
                            fprintf( stderr, "WARNING: Unable to push the entire payload to the sink, data has been lost - internal error?\n" );

                        break;
                    }

                    default:
                        fprintf( stderr, "WARNING: Unknown or unexpected message type (%d), skipped.\n", header->type );
                }
            }
        }

    }

    dropRouterFD();

    return EXIT_SUCCESS;
}