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
#include "BuildInfo.h"
#include "common.h"
#include "lib/GraphNetwork.h"
#include "lib/LinkedList.h"
#include "lib/packet.h"
#include "Log.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <assert.h>

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0

#define MAX_INPUT_STREAMS 256
#define IGNORE_FD (-1)

#define MUX_AUTO_SPAWN 0
#define MUX_MERGE 1

#define DEMUX_NOOP 0
#define DEMUX_DROP 1

extern char **environ;

// Configurable Options //
struct _configuration {
    int            network_mtu;     // default = 1500
    unsigned int   read_timeout;    // default = 5
    uint8_t     *  rx_buffer;
    uint8_t     *  rx_buffer_tail;

    unsigned char  arg_mux_policy;
    unsigned char  arg_demux_policy;

    char *         arg_host;
    char *         arg_port;
    gnw_address_t  arg_address;
    bool           arg_immediate;
    unsigned int   arg_verbosity;
    bool           arg_echo;
    char           arg_delimiter;
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

typedef struct {
    sink_context_t ** forward;
    uint8_t round_robin_index;
    bool ignore_sub_stream;
} mux_t;

typedef struct {
    sink_context_t ** collect;
    bool ignore_sub_stream;
} demux_t;

sink_context_t * substream_table[MAX_INPUT_STREAMS] = { NULL };

// This can only ever grow, due to internal constraints, which is unfortunate...
struct pollfd    stream_fd[MAX_INPUT_STREAMS];
int stream_fd_count = 0;

volatile unsigned char nextLocalAddress = 0;

linked_list_t * sink_context_list;
pthread_mutex_t sink_context_list_mutex;

int router_fd = -1;
int getRouterFD() {
    if( router_fd == -1 ) {
        log_info( "Connecting to router at %s:%s...", config.arg_host, config.arg_port );
        router_fd = socket_connect( config.arg_host, config.arg_port );

        //printf( "Router FD = %d\n", router_fd );

        // Build the other network-related structures
        config.rx_buffer = (uint8_t *)malloc( config.network_mtu * 20 ); // 20 packet(ish) buffer
        config.rx_buffer_tail = config.rx_buffer;
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
        // Bit of a hack - send the requested address with the new address request... Might just be ignored at the router, though!
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

        config.rx_buffer_tail = packet_write_u8_buffer( config.rx_buffer_tail, iBuffer, bytesRead );

        ssize_t readyBytes = 0;
        while( (readyBytes = gnw_nextPacket( config.rx_buffer, config.rx_buffer_tail - config.rx_buffer )) != 0 ) {

            // Are we in some invalid buffer state?
            if( readyBytes < 0 ) {
                log_warn( "Network desync, attempting to resync... (Err = %d)", readyBytes );
                // Dump a byte, try to clear the buffer and re-try.
                packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, 1 );
                config.rx_buffer_tail--;
                continue;
            }

            gnw_header_t header = { 0 };
            
            uint8_t * ptr = config.rx_buffer;
            ptr = packet_read_u8( ptr, &header.magic );
            ptr = packet_read_u8( ptr, &header.version );
            ptr = packet_read_u8( ptr, &header.type );
            ptr = packet_read_u32( ptr, &header.source );
            ptr = packet_read_u32( ptr, &header.length );

            uint8_t payload[config.network_mtu];
            memcpy( payload, ptr, header.length );

            packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, 11 + header.length );
            config.rx_buffer_tail -= 11 + header.length;

            if( header.version != GNW_VERSION )
                log_warn( "Warning! Router/Client version mismatch!" );

            switch( header.type ) {
                case GNW_COMMAND | GNW_REPLY: // Command response
                    //printf( "{CMD/RPLY}\n" );
                    if( header.length < 1 ) {
                        log_warn( "Router sent a command with no operator, no idea what to do! Trying to skip past it..." );
                        break;
                    }

                    switch( *payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            //printf( "{New Address}\n" );
                            if( new_address == 0 ) {
                                new_address = *(gnw_address_t *)(payload + 1);

                                if( new_address != try_address && try_address != 0 )
                                    log_warn( "Router refused our address request for %08x, actually got %08x", try_address, new_address );
                            }
                            break;

                        default:
                            log_warn( "Unknown command response? (%u)", (unsigned char)(*payload) );
                            break;
                    }

                    break;

                case GNW_DATA:
                    if( header.length == 0 )
                        log_warn("Router keepalive received (lossy connection?)");
                    break;

                default:
                    log_warn( "Unknown response from router, skipping. (%x)", header.type );
            }
        }
    }

    log_info( "Address is now [%08x]", new_address );

    return new_address;
}

int dropRouterFD() {
    if( router_fd != -1 ) {
        log_info( "Disconnecting from router..." );
        free( config.rx_buffer );
        close( router_fd );
    }
    return -1;
}

int addNewWatch( int fd ) {
    int index = 0;
    while( stream_fd[index].fd != IGNORE_FD ) {
        index++;
    }
    stream_fd[index].fd = fd;
    stream_fd[index].events = POLLIN;

    stream_fd_count++;

    return index;
}

void configure_stdin_input() {
    int watchIndex = addNewWatch( STDIN_FILENO ); // Add STDIN to the input watch list

    log_info( "Now watching for input from STDIN on %d\n", watchIndex );
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

    // 2x MTU, so we always have room for one entire transmission
    uint8_t outputBuffer[ config.network_mtu * 2 ];
    memset( outputBuffer, 0, config.network_mtu * 2 );
    uint8_t * outputTail = outputBuffer;

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
                if ((watch_fd[0].revents & POLLIN) == POLLIN) {
                    uint8_t buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[0].fd, buffer, config.network_mtu);

                    if( config.arg_verbosity > 0 )
                        fprintf( stderr, ">>>\t%s\n", buffer );

                    if( bytesRead > 0 ) {
                        // Do we have a delim at any point in this new block?
                        for( size_t offset=0; offset<bytesRead; offset++ ) {
                            outputTail = packet_write_u8( outputTail, buffer[offset] );

                            if( buffer[offset] == config.arg_delimiter ) {
                                uint8_t txBuffer[ outputTail-outputBuffer ];
                                size_t txLength = outputTail-outputBuffer;
                                outputTail = packet_shift( outputBuffer, config.network_mtu * 2, txBuffer, txLength );
                                gnw_emitDataPacket( rfd, context->stream_address, txBuffer, txLength ); // Always emit from the node source address, not the stream source address
                            }
                        }
                    }
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

void handlePacket( int index, gnw_header_t * header, unsigned char * payload ) {

    // Are we in 'output mode' (or otherwise echo'ing)?
    if( config.arg_echo ) {
        for (int i = 0; i < header->length; i++) {
            // Note: using putc here to avoid issues with null terminators and non-printable streams
            putc(*(payload + i), stdout);
        }
        fflush( stdout );
        return;
    }

    // Virtual MUX Operation
    switch( config.arg_mux_policy ) {
        case MUX_AUTO_SPAWN: /* No-op */ break;
        case MUX_MERGE:
            header->source = config.arg_address;
            break;
        default:
            log_error( "Bad internal mux policy! [%x]", config.arg_mux_policy );
    }

    sink_context_t * context = getSinkContext( header->source );
    if( context == NULL ) {

        if( config.arg_mux_policy == MUX_AUTO_SPAWN ) {
            log_warn( "Unimplemented mux spawn stage!" );
            //
        }

        log_warn( "No sink creation policy, and no such address [%x], so dropping the packet!", header->source );
        log_warn( "Data has been lost" );
        return;
    }

    FILE * file = fdopen( PIPE_WRITE(context->wrap_stdin), "w" );
    size_t written = fwrite( payload, sizeof(unsigned char), header->length, file );
    if( written != header->length ) {
        log_warn( "Could not write buffered data to the child process, OS buffer overflow?" );
        log_warn( "Data has been lost" );
    }
    fflush( file );
}

typedef struct {
    const char * description;
    const char * arg;
} short_args_description_t;

#define ARG_HELP       0
#define ARG_USAGE      1
#define ARG_HOST       2
#define ARG_PORT       3
#define ARG_ADDRESS    4
#define ARG_INPUT      5
#define ARG_OUTPUT     6
#define ARG_MUX        7
#define ARG_DEMUX      8
#define ARG_IMMEDIATE  9
#define ARG_VERSION    10
#define ARG_DELIMITER  11

int main(int argc, char ** argv ) {
    // Initial states
    for( int i=0; i<256; i++ ) {
        stream_fd[i].fd = IGNORE_FD; // Ignore every FD, for now
    }

    // Configuration defaults
    config.network_mtu  = 1500;
    config.read_timeout = 5;

    config.arg_host = "127.0.0.1";
    config.arg_port = (char *) ROUTER_PORT;

    config.arg_mux_policy   = MUX_AUTO_SPAWN;
    config.arg_demux_policy = DEMUX_NOOP;

    config.arg_address   = 0;
    config.arg_immediate = false;
    config.arg_verbosity = 0;

    config.arg_echo = false;

    config.arg_delimiter = '\0';

    // Check the system MTU and match it - this assumes local operation, for now.
    // Note: Possibly add an override for this in the flags...
    config.network_mtu = getIFaceMTU("lo");

    // Build any remaining structures
    sink_context_list = ll_create();
    pthread_mutex_init( &sink_context_list_mutex, NULL );

    // Following pragma block is just to prevent gcc complaining about mismatched braces in this structure
    // apparently, this is a compiler bug! (gcc v7.3.1)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
    struct option longOptions[] = {
            [ARG_HELP]       = { .name="help",      .has_arg=no_argument,       .flag=NULL },
            [ARG_USAGE]      = { .name="usage",     .has_arg=no_argument,       .flag=NULL },
            [ARG_HOST]       = { .name="host",      .has_arg=required_argument, .flag=NULL },
            [ARG_PORT]       = { .name="port",      .has_arg=required_argument, .flag=NULL },
            [ARG_ADDRESS]    = { .name="address",   .has_arg=required_argument, .flag=NULL },
            [ARG_INPUT]      = { .name="input",     .has_arg=no_argument,       .flag=NULL },
            [ARG_OUTPUT]     = { .name="output",    .has_arg=no_argument,       .flag=NULL },
            [ARG_MUX]        = { .name="output",    .has_arg=no_argument,       .flag=NULL },
            [ARG_DEMUX]      = { .name="output",    .has_arg=no_argument,       .flag=NULL },
            [ARG_IMMEDIATE]  = { .name="immediate", .has_arg=no_argument,       .flag=NULL },
            [ARG_VERSION]    = { .name="version",   .has_arg=no_argument,       .flag=NULL },
            [ARG_DELIMITER]  = { .name="delim",     .has_arg=required_argument, .flag=NULL },
            0
    };
    // Purely so descriptions and arguments are managed together in the same block - this could be done purely in the --help/--usage
    // argument block, but this keeps it sane-ish, even if the syntax is a little odd.
    short_args_description_t shortOptions[] = {
        [ARG_HELP]      = { .arg=NULL, .description="Show this help message." },
        [ARG_USAGE]     = { .arg=NULL, .description="Show this help message." },
        [ARG_HOST]      = { .arg="h",  .description="Specify a GraphRouter host address (defaults to localhost)." },
        [ARG_PORT]      = { .arg="p",  .description="Specify a GraphRouter port." },
        [ARG_ADDRESS]   = { .arg="a",  .description="The (requested) hexadecimal node address, may not be respected by the router. Cannot be 0." },
        [ARG_INPUT]     = { .arg="i",  .description="Run in input bridge mode; take and input on stdin and forward to the GraphRouter." },
        [ARG_OUTPUT]    = { .arg="o",  .description="Rung in output bridge mode; take any messages from the GraphRouter and emit them on stdout." },
        [ARG_IMMEDIATE] = { .arg=NULL, .description="Start running the inner binary immediately. By default wrapped processes are only started on demand when data arrives." },
        [ARG_VERSION]   = { .arg=NULL, .description="Report which version this program is, then exit." },
        [ARG_DELIMITER] = { .arg="d",  .description="Configure the packet delimiter, if unspecified, will default to the string null terminator '\0'." },
        0
    };
#pragma GCC diagnostic pop

    // Argument Parsing //
    int arg;
    int indexPtr = 0;
    while ((arg = getopt_long(argc, argv, "h:a:p:iovd", longOptions, &indexPtr)) != -1) {

        // If we have a short arg, pass it over to the long arg index.
        // Note: This will work assuming we have less than 65(?) long arguments... I think -John.
        if (arg > 0)
            indexPtr = arg;

        switch (indexPtr) {
            case ARG_HELP:
            case ARG_USAGE: // ToDo: Update the argument list with the new argument options
                printf("GraphWrap\n");
                printf(ANSI_COLOR_GREEN "\t\t\"Don't cross the streams\" --Egon Spengler.\n\n" ANSI_COLOR_RESET);
                printf("Wrap a normal Linux process stdin/stdout pipes with GraphIPC connections to a GraphRouter process\nAllows non-compliant programs to be used in a graph\n\n");

                int offset = 0;
                while( longOptions[offset].name != 0 ) {
                    printf( ANSI_COLOR_CYAN );

                    if( shortOptions[offset].arg != NULL )
                        printf( "-%s ", shortOptions[offset].arg );

                    if( longOptions[offset].name != NULL )
                        printf( "--%s ", longOptions[offset].name );
                    
                    if( longOptions[offset].has_arg == required_argument )
                        printf( "[required-value]" );
                    else if( longOptions[offset].has_arg == optional_argument )
                        printf( "[optoinal-value]" );
                    
                    printf( "\n" );

                    if( shortOptions[offset].description != NULL )
                        printf( ANSI_COLOR_RESET "\t%s\n\n", shortOptions[offset].description );
                    
                    offset++;
                }
                printf(ANSI_COLOR_CYAN "-v\n" ANSI_COLOR_RESET "\tIncrease verbosity, repeat for increasing levels of detail\n\n");
                printf(ANSI_COLOR_CYAN "--\n" ANSI_COLOR_RESET "\tOptional separator between GraphWrap arguments and the inner binary\n");

                printf( ANSI_COLOR_RESET );
                
                return EXIT_SUCCESS;

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
                config.arg_address = (gnw_address_t) strtoul(optarg, NULL, 16);
                break;

            case 'i':
            case ARG_INPUT:
                configure_stdin_input();
                break;

            case 'o':
            case ARG_OUTPUT:
                config.arg_echo = true;
                break;

            case ARG_IMMEDIATE:
                config.arg_immediate = true;
                break;

            case ARG_VERSION:
                printf( "Version: #%s\n", GIT_HASH );
                return EXIT_SUCCESS;
            
            case ARG_DELIMITER:
            case 'd':
                /*if( optarg[0] == '\\') {
                    switch( optarg[1] ) {
                        case '\\': config.arg_delimiter = '\\'; break;
                        case '0':  config.arg_delimiter = '\0'; break;
                        case 'r':  config.arg_delimiter = '\r'; break;
                        case 'n':  config.arg_delimiter = '\n'; break;
                        case 't':  config.arg_delimiter = '\t'; break;

                        default:
                            log_warn( "Unrecognised escape character '%c', using it directly instead.", optarg[1] );
                            config.arg_delimiter = optarg[1];
                    }
                }
                else
                    config.arg_delimiter = optarg[0];*/
                printf( "Delimiter = '%s'\n", optarg );
                break;

            case 'v':
                config.arg_verbosity++;

                if (config.arg_verbosity == 2)
                    log_setLevel(INFO);
                else if (config.arg_verbosity == 3)
                    log_setLevel(DEBUG);

                break;

            default:
                // Note - This probably won't ever happen, as unknown arguments are assumed to be for the binary
                //        target, rather than ourselves :/
                log_warn("Unknown argument: %d, ignored", indexPtr);
        }
    }

    log_debug( "Going live!" );
    int rfd = getRouterFD();
    addNewWatch( rfd );

    gnw_address_t assigned_address = getNodeAddress( config.arg_address );
    config.arg_address = assigned_address;

    log_debug( "Beginning poll of all known FDs..." );

    int status = 0;
    while( status == 0 ) {
        int rv = -1;
        while( status == 0 && (rv = poll(stream_fd, stream_fd_count, 100)) != -1 ) {

            // Sanity check
            if( config.rx_buffer > config.rx_buffer_tail ) {
                log_error( "Buffer in a corrupted state! About to segfault (probably)!\n" );
            }

            int index = 0;
            while( rv > 0 && index < MAX_INPUT_STREAMS ) {
                if( stream_fd[index].revents != 0 ) {

                    unsigned char iBuffer[config.network_mtu];
                    memset( iBuffer, 0, config.network_mtu ); // This will cause slowdown, but is safer this way.

                    ssize_t readBytes = read(stream_fd[index].fd, iBuffer, config.network_mtu);
                    if( readBytes < 1 ) {
                        log_warn( "Stream %d closed unexpectedly", index );

                        if( stream_fd[index].fd == rfd ) {
                            log_error( "Router closed our connection! Also closing down..." );
                            status = 1; // Mark us for shutdown.
                        }

                        if( stream_fd[index].fd == STDIN_FILENO ) {
                            // Stdin closed, pipe must have gone, OK, shut stuff down.
                            log_error( "STDIN went offline, pipe must have died. Closing down in turn..." );
                            status = 1;
                        }

                        stream_fd[index].fd = -1;
                    }

                    else if( readBytes > 0 ) {
                        // Is this the router FD?
                        if( stream_fd[index].fd == rfd ) {
                            //printf( "Read %ldB from Router on index = %d\n", readBytes, index );

                            assert( config.rx_buffer_tail >= config.rx_buffer );
                            assert( config.rx_buffer_tail - config.rx_buffer <= config.network_mtu * 20 ); 

                            packet_write_u8_buffer( config.rx_buffer_tail, iBuffer, readBytes );
                            config.rx_buffer_tail += readBytes;

                            uint8_t packet_buffer[config.network_mtu + 1];
                            memset( packet_buffer, 0, config.network_mtu + 1 );
                            ssize_t readyBytes = 0;
                            if( (readyBytes = gnw_nextPacket( config.rx_buffer, config.rx_buffer_tail - config.rx_buffer )) != 0 ) {

                                if( readyBytes < 0 ) {
                                    log_warn( "Network desync, attempting to resync... (Err = %d)", readyBytes );
                                    while( config.rx_buffer[0] != GNW_MAGIC && config.rx_buffer < config.rx_buffer_tail ) {
                                        printf( "Dumped 1 byte\n" );
                                        packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, 1 );
                                        config.rx_buffer_tail--;
                                    }
                                    continue;
                                }

                                gnw_header_t header = { 0 };

                                uint8_t * ptr = config.rx_buffer;
                                ptr = packet_read_u8( ptr, &header.magic );
                                ptr = packet_read_u8( ptr, &header.version );
                                ptr = packet_read_u8( ptr, &header.type );
                                ptr = packet_read_u32( ptr, &header.source );
                                ptr = packet_read_u32( ptr, &header.length );

                                handlePacket( index, &header, ptr );

                                // Shift the buffer and sync up the tail pointer
                                packet_shift( config.rx_buffer, config.network_mtu * 20, NULL, readyBytes );
                                config.rx_buffer_tail = config.rx_buffer_tail - (size_t)readyBytes;
                            }

                        }

                        // Is this stdin?
                        else if( stream_fd[index].fd == STDIN_FILENO ) {
                            //printf("Read %ldB from STDIN\n", readBytes);

                            gnw_emitDataPacket( rfd, config.arg_address, iBuffer, readBytes );
                        }
                    }

                    stream_fd[index].revents = 0;
                    rv--; // Deduct one remaining fd
                }

                index++; // Walk to the next available fd
            }

            if( rv > 0 )
                log_warn( "Notified of stream events, but could not find a corresponding FD? Something is going very wrong!" );
        }

    }

    // Close our router connection, if we still have one...
    log_info( "Closing router connection..." );
    if( fcntl( rfd, F_GETFD ) != -1 ) {
        close(rfd);
    }

    log_info( "Shutting down any open FDs..." );
    for( int i=0; i<MAX_INPUT_STREAMS; i++ ) {
        if( fcntl( stream_fd[i].fd, F_GETFD ) != -1 ) {
            log_debug( "Closing %d...", stream_fd[i].fd );
            close(stream_fd[i].fd);
        }
    }

    return EXIT_SUCCESS;
}

/*    // If we have other arguments to parse, then we can assume we're actually wrapping a process now.
    if( argc - optind > 0 ) {

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
}*/