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
#include "lib/Assert.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <arpa/inet.h>
#include "lib/klib/khash.h"
#include <signal.h>
#include <termios.h>
#include <netinet/tcp.h>

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0

#define MAX_INPUT_STREAMS 256
#define IGNORE_FD (-1)

#define MUX_NOOP  0
#define MUX_MERGE 1

#define DEMUX_NOOP       0
#define DEMUX_SPAWN      1
#define DEMUX_MERGE      2

extern char **environ;

// Configurable Options //
struct _configuration {
    int            network_mtu;     // default = 1500
    unsigned int   read_timeout;    // default = 5

    char *         arg_host;
    char *         arg_port;
    gnw_address_t  arg_address;
    gnw_address_t  gnw_local_mask;
    bool           arg_immediate;
    unsigned int   arg_verbosity;
    bool           arg_echo;
    char           arg_delimiter;
};

struct _mux_config {
    unsigned int mode;
};

struct _demux_config{
    unsigned int mode;
    uint32_t round_robin_index;
};

typedef struct {
    pid_t pid;
    gnw_address_t address;
    
    int wrap_stdin[2];
    int wrap_stdout[2];

    uint64_t packets_in;
    uint64_t packets_out;

    uint64_t bytes_in;
    uint64_t bytes_out;
} sink_context_t;

struct _binary_config {
    char *  path;
    int     argc;
    char *  argv[20];
};

// Configuration structures, for namespace sanity
struct _configuration config;
struct _mux_config    config_mux;
struct _demux_config  config_demux;
struct _binary_config config_binary;

// This can only ever grow, due to internal constraints, which is unfortunate...
struct pollfd    stream_fd[MAX_INPUT_STREAMS];

gnw_address_t nextLocalAddress = 0;

// Hashtable of substreams for faster-ish lookups
KHASH_MAP_INIT_INT( gnw_address_t, sink_context_t );
khash_t(gnw_address_t) * sinkTable;

// The 'big buffer in the sky' for the node as a whole
typedef struct {
    uint8_t * head;
    uint8_t * tail;
} local_buffer_t;

KHASH_MAP_INIT_INT( int, local_buffer_t );
khash_t(int) * input_buffer;

struct termios saved_attributes;

void reset_input_mode (void) {
    tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}

void set_input_mode (void) {
    struct termios tattr;
    char *name;

    /* Make sure stdin is a terminal. */
    if (!isatty (STDIN_FILENO)) {
        fprintf (stderr, "Not a terminal.\n");
        exit (EXIT_FAILURE);
    }

    /* Save the terminal attributes so we can restore them later. */
    tcgetattr (STDIN_FILENO, &saved_attributes);
    atexit (reset_input_mode);

    /* Set the funny terminal modes. */
    tcgetattr (STDIN_FILENO, &tattr);
    tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}


local_buffer_t * getLocalBuffer( int fd ) {
    khint_t hint = kh_get( int, input_buffer, fd );

    // Does this key exist?
    if( hint == kh_end(input_buffer) ) {
        log_debug( "Auto-creating buffers for fd = %d\n", fd );

        // Create a new buffer set!
        int status;
        hint = kh_put( int, input_buffer, fd, &status );
        assert( status != 0, "Another bucket was deleted in the input_buffer table!" );

        //fprintf( stderr, "STATUS = %d\n", status );

        local_buffer_t * local = &kh_value( input_buffer, hint );
        local->head = (uint8_t *)malloc( config.network_mtu * 20 );
        local->tail = local->head;
    }

    return &kh_value( input_buffer, hint );
}

void destroyLocalBuffer( int fd ) {
    khint_t hint = kh_get( int, input_buffer, fd );

    // Does this key exist?
    if( hint == kh_end(input_buffer) )
        return;
    
    log_debug( "Freeing local buffers for fd = %d\n", fd );
    local_buffer_t * buffer = &kh_value( input_buffer, hint );
    free( buffer->head );
    buffer->head = NULL;
    buffer->tail = NULL;
}

int router_fd = -1;
int getRouterFD() {
    if( router_fd == -1 ) {
        log_info( "Connecting to router at %s:%s...", config.arg_host, config.arg_port );
        router_fd = socket_connect( config.arg_host, config.arg_port );

        int flag = 1;
        int result = setsockopt( router_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int) );
        if (result < 0)
            log_warn( "Unable to disable Nagle algorithm on the router socket, expect packet delays!" );
    }
    return router_fd;
}

gnw_address_t getNextLocalAddress() {
    nextLocalAddress = (nextLocalAddress+1) & config.gnw_local_mask;

    gnw_address_t realAddress = (nextLocalAddress & config.gnw_local_mask) | (config.arg_address & (~config.gnw_local_mask));

    // Claim this address for our FD on the router
    log_info( "Requesting address [%08x] from the router...", realAddress );
    unsigned char reqBuffer[5] = { 0 };
    uint8_t * ptr = packet_write_u8( reqBuffer, GNW_CMD_NEW_ADDRESS );
    ptr = packet_write_u32( ptr, realAddress );
    gnw_emitCommandPacket( getRouterFD(), GNW_COMMAND, reqBuffer, 5 );
    
    return realAddress;
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

    log_info( "Launching subprocess..." );

    // Capture stdout to wrap_stdout
    dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        log_error( "Process terminated with error: %s", strerror(errno) );

    return -1;
}

gnw_address_t applyMuxPolicy( gnw_address_t address ) {
    switch( config_mux.mode ) {
        // Do nothing
        case MUX_NOOP:
            return address;

        // Rewrite addresses to the local node address
        case MUX_MERGE:
            return config.arg_address;
        
        default:
            log_error( "BAD MUX MODE - Mux state corrupted!" );
    }
    return address;
}

sink_context_t * autoSpawn( gnw_address_t address ) {
    // Become two actual processes, launch the child.
    log_info( "Starting new subprocess..." );
    int status;
    khint_t hint = kh_put( gnw_address_t, sinkTable, address, &status );
    sink_context_t * context = &kh_value( sinkTable, hint );
    context->bytes_in     = 0;
    context->bytes_out    = 0;
    context->packets_in   = 0;
    context->packets_out  = 0;
    
    // Build a new set of redirected pipes
    if( pipe( context->wrap_stdin ) == -1 ) {
        log_error( "Could not create wrapper for subprocess stdin" );
        exit( EXIT_FAILURE );
    }

    if( pipe( context->wrap_stdout ) == -1 ) {
        log_error( "Could not create wrapper for subprocess stdout" );
        exit( EXIT_FAILURE );
    }

    log_warn( "Init: %s", config_binary.path );

    pid_t childPID = fork();
    if( childPID == 0 ) {
        // Close the invalid ends of the pipes (for the child)
        close( PIPE_WRITE(context->wrap_stdin) );
        close( PIPE_READ(context->wrap_stdout) );

        processRunner(
            PIPE_READ(context->wrap_stdin),
            PIPE_WRITE(context->wrap_stdout),
            config_binary.path,
            config_binary.argv );

        log_error( "Process failed to spawn and returned to the parent after fork() - Cannot continue! HALT." );
        exit( EXIT_FAILURE );
    }

    // Close the invalid ends of the pipes (for the parent)
    close( PIPE_READ(context->wrap_stdin) );
    close( PIPE_WRITE(context->wrap_stdout) );
    
    // If we're here, we're the parent, and should have process ID
    context->pid = childPID;
    log_debug( "Child PID = %ld\n", context->pid );

    // Go live, become a node on the router and connect our new output FD
    addNewWatch( PIPE_READ(context->wrap_stdout) );

    return context;
}

gnw_address_t applyDeMuxPolicy( gnw_address_t address ) {
    if( config_demux.mode == DEMUX_MERGE )
        address = config.arg_address;

    // Try to find an existing address match 
    khint_t hint = kh_get( gnw_address_t, sinkTable, address );

    // If we have a non-end match, there must already be a sink for this. Do nothing.
    if( hint != kh_end( sinkTable ) )
        return address;
    
    // If we're here, we have no configured subprocess... So fire one up
    if( config_demux.mode == DEMUX_SPAWN )
        address = getNextLocalAddress();

    fprintf( stderr, "AUTOSPAWN\n" );
    autoSpawn( address );

    return address; // Return the mangled address :)
}

int dropRouterFD() {
    if( router_fd != -1 ) {
        log_info( "Disconnecting from router..." );
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

    return index;
}

int fd_is_valid(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

void handleDataPacket( gnw_header_t * header, uint8_t * payload ) {
    // Node: IN -> DEMUX -> PROCESS -> MUX -> OUT

    // Are we in 'output mode' (or otherwise echo'ing)?
    if( config.arg_echo ) {
        for (int i = 0; i < header->length; i++) {
            // Note: using putc here to avoid issues with null terminators and non-printable streams
            putc(*(payload + i), stdout);
        }
        fflush( stdout );
        return;
    }

    // Mangle the source address if we're configured to do so.
    header->source = applyDeMuxPolicy( header->source );

    // Look up whoever this is.
    khint_t hint = kh_get( gnw_address_t, sinkTable, header->source );
    if( hint == kh_end( sinkTable ) ) {
        // Note: Should actually spawn stuff here
        log_error( "No such address %08x, but the router seems to think we are it? Dropped packet.", header->source );
        return;
    }

    // At this point, we'd expect that the hint points to actual data.
    sink_context_t * context = &kh_value( sinkTable, hint );
    write( PIPE_WRITE(context->wrap_stdin), payload, header->length );

    // Update the stats!
    context->packets_in++;
    context->bytes_in += header->length;
}

void handleCommandPacket( gnw_header_t * header, uint8_t * payload ) {
    uint8_t directive = 0xff;
    uint8_t * next = packet_read_u8( payload, &directive );

    if( header->length < 1 ) {
        log_warn( "Router sent a command with no operator, no idea what to do! Trying to skip past it..." );
        return;
    }

    switch( directive ) {
        case GNW_CMD_NEW_ADDRESS:
            if( config.arg_address == 0 ) {
                next = packet_read_u32( next, &config.arg_address );
                log_info( "Router issued us address: %08x", config.arg_address );
            }
            else {
                gnw_address_t subAddress = 0;
                next = packet_read_u32( next, &subAddress );
                log_info( "Router issued us inner address: %08x", subAddress );
            }
            break;

        default:
            log_warn( "Unknown command response? (%u)", (unsigned char)(*payload) );
            break;
    }
}

void handlePacket( gnw_header_t * header, uint8_t * payload ) {
    uint8_t * ptr = payload;

    switch( header->type ) {

        // Data stuff
        case GNW_DATA: handleDataPacket( header, payload ); break;
        
        // Command stuff
        case GNW_COMMAND:
        case GNW_REPLY:
            handleCommandPacket( header, payload );
            break;
        
        default:
            log_error( "BAD PACKET TYPE (%u) - Unknown packet type! Line corruption or version mismatch?", header->type );
    }
}

void handleRemoteData( int * fd, int * shutdown, unsigned int events ) {
    local_buffer_t * buffer = getLocalBuffer( *fd );

    size_t capacity = (config.network_mtu * 20) - (buffer->tail - buffer->head);
    ssize_t actualRead = read( *fd, buffer->tail, capacity );

    // Did the read fail?
    if( actualRead < 1 ) {
        log_warn( "Remote stream %d closed unexpectedly", *fd );

        if( *fd == getRouterFD() ) {
            log_error( "Router closed our connection! Also closing down..." );
            *shutdown = 1; // Mark us for shutdown.
        }

        destroyLocalBuffer( *fd );
        *fd = -1; // Clear the FD reference so poll doesn't use it later
        return;
    }

    // Otherwise, the read should have worked, update the tail pointer
    assert( actualRead > 0, "Read worked but total bytes was negaive! This should never happen!" );
    buffer->tail += actualRead;
    
    // While we have data on the buffer, try and parse it!
    ssize_t readyBytes = 0;
    while( (readyBytes = gnw_nextPacket( buffer->head, buffer->tail - buffer->head )) != 0 ) {
        // Are we in some invalid buffer state?
        if( readyBytes < 0 ) {
            log_warn( "Network desync, attempting to resync... (Err = %d)", readyBytes );
            // Dump a byte, try to clear the buffer and re-try.
            packet_shift( buffer->head, config.network_mtu * 20, NULL, 1 );
            buffer->tail--;
            continue;
        }

        gnw_header_t header = { 0 };
        uint8_t * ptr = gnw_parse_header( buffer->head, &header );

        /*fprintf( stderr, "PKT>>>" );
        gnw_dumpPacket( stderr, buffer->head, readyBytes ); // DEBUG*/

        // Safety buffer, so we can't accidentally trash the main one
        uint8_t payload[11 + header.length];
        memcpy( payload, ptr, header.length );

        // Shift (read) out, and update the pointers.
        packet_shift( buffer->head, config.network_mtu * 20, NULL, 11 + header.length );
        buffer->tail -= 11 + header.length;

        // Pass on to the packet handler
        handlePacket( &header, payload );
    }
}

void handleLocalData( int * fd, int * shutdown, unsigned int events ) {
    local_buffer_t * buffer = getLocalBuffer( *fd );

    size_t capacity = (config.network_mtu * 20) - (buffer->tail - buffer->head);
    ssize_t actualRead = read( *fd, buffer->tail, capacity );

    // Did the read fail?
    if( actualRead < 1 ) {
        if( (events & POLLNVAL) == POLLNVAL || (events & POLLHUP) == POLLHUP || !fd_is_valid(*fd) ) {
            log_warn( "Local stream %d closed unexpectedly", *fd );

            if( *fd == STDIN_FILENO ) {
                // Stdin closed, pipe must have gone, OK, shut stuff down.
                log_error( "STDIN went offline, pipe must have died. Closing down in turn..." );
                *shutdown = 1;
            }

            khint_t iter = kh_begin( sinkTable );
            while( iter != kh_end( sinkTable ) ) {
                if( kh_exist( sinkTable, iter ) == 1 ) {
                    sink_context_t * context = &kh_value( sinkTable, iter );
                    if( *fd == PIPE_READ(context->wrap_stdout) ) {
                        kh_del( gnw_address_t, sinkTable, iter );
                        break;
                    }
                }
                iter++;
            }

            destroyLocalBuffer( *fd );
            *fd = -1; // Clear the FD reference so poll doesn't use it later
        }

        return;
    }

    buffer->tail += actualRead;
    size_t remainingCapacity = (config.network_mtu * 20) - (buffer->tail - buffer->head);

    // Are we essentially full?
    if( remainingCapacity < config.network_mtu ) {
        // Emergency send! About to blow off the end of the buffer!
        gnw_emitDataPacket( router_fd, applyMuxPolicy(config.arg_address), buffer->head, buffer->tail );
        buffer->tail = buffer->head;
        return;
    }

    uint8_t * cursor = buffer->head;
    while( cursor < buffer->tail ) {
        if( *cursor == config.arg_delimiter ) {
            gnw_emitDataPacket( router_fd, applyMuxPolicy(config.arg_address), buffer->head, cursor - buffer->head );
            assert( cursor - buffer->head <= config.network_mtu * 20, "Cursor blew off the end of the local buffer!" );
            packet_shift( buffer->head, config.network_mtu * 20, NULL, cursor - buffer->head );
            buffer->tail -= cursor - buffer->head;
            cursor = buffer->head;

        }
        cursor++;
    }
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
#define ARG_LOCALMASK  12
#define ARG_QUIET      13

int main(int argc, char ** argv ) {
    // Prevent the kernel from hanging on to our child processes later on
    signal( SIGCHLD, SIG_IGN );

    // Initial states
    for( int i=0; i<256; i++ ) {
        stream_fd[i].fd = IGNORE_FD; // Ignore every FD, for now
    }

    // Configuration defaults
    config.network_mtu  = 1500;
    config.read_timeout = 5;

    config.arg_host = "127.0.0.1";
    config.arg_port = (char *) ROUTER_PORT;

    config.gnw_local_mask   = 0x00000FFF; // Bottom 3 octets for local addressing. Bit overkill, but hey.

    config.arg_address   = 0;
    config.arg_immediate = false;
    config.arg_verbosity = 0;

    config.arg_echo = false;

    config.arg_delimiter = '\n';

    // Check the system MTU and match it - this assumes local operation, for now.
    // Note: Possibly add an override for this in the flags...
    config.network_mtu = getIFaceMTU("lo");

    // Configure local structures...
    sinkTable = kh_init( gnw_address_t );
    input_buffer = kh_init( int );


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
            [ARG_MUX]        = { .name="mux",       .has_arg=required_argument, .flag=NULL },
            [ARG_DEMUX]      = { .name="demux",     .has_arg=required_argument, .flag=NULL },
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
        [ARG_MUX]       = { .arg=NULL, .description="Configure the output multiplexer policy, determines how addresses are handled from sub-process streams [noop|merge]" },
        [ARG_DEMUX]     = { .arg=NULL, .description="Configure the input multiplexer policy, determines how addresses are handled from the local router. [noop|spawn|merge]" },
        [ARG_IMMEDIATE] = { .arg=NULL, .description="Start running the inner binary immediately. By default wrapped processes are only started on demand when data arrives." },
        [ARG_VERSION]   = { .arg=NULL, .description="Report which version this program is, then exit." },
        [ARG_DELIMITER] = { .arg="d",  .description="Configure the packet delimiter, if unspecified, will default to the unix string newline '\\n'." },
        0
    };
#pragma GCC diagnostic pop

    // Argument Parsing //
    int arg;
    int indexPtr = 0;
    while ((arg = getopt_long(argc, argv, "h:a:p:iovd:q", longOptions, &indexPtr)) != -1) {

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

                if( (config.arg_address & ~config.gnw_local_mask) != config.arg_address ) {
                    log_warn( "Node address does not conform to the local mask policy!" );
                    log_warn( "This may cause problems if this node also has a multiplexing policy which spawns additional sub-processes." );
                    log_warn( "Mask = %08x, Address = %08x", config.gnw_local_mask, config.arg_address );
                }
                break;

            case 'i':
            case ARG_INPUT:
                addNewWatch( STDIN_FILENO );
                break;

            case 'o':
            case ARG_OUTPUT:
                config.arg_echo = true;
                if( config_demux.mode != DEMUX_MERGE ) {
                    log_warn( "Output mode implies a demux policy of 'merge' otherwise the stream will be nonesense. Forcing merge mode." );
                    config_demux.mode = DEMUX_MERGE;
                }
                break;
            
            case ARG_MUX:
                if( strcmp( optarg, "noop" ) )
                    config_mux.mode = MUX_NOOP;
                else if( strcmp(optarg, "merge") )
                    config_mux.mode = MUX_MERGE;
                else
                    log_warn( "Unrecognised mux mode '%s', ignored. Using default (NOOP)", optarg );
                
                break;
            
            case ARG_DEMUX:
                fprintf( stderr, "ARG = '%s'\n", optarg );

                if( config.arg_echo && strcmp( optarg, "merge" ) != 0 ) {
                    log_warn( "Output mode implies a demux policy of 'merge' otherwise the stream will be nonesense. Forcing merge mode." );
                    config_demux.mode = DEMUX_MERGE;
                    break;
                }
                if( strcmp( optarg, "noop" ) == 0 ) {
                    config_demux.mode = DEMUX_NOOP;

                } else if( strcmp( optarg, "spawn" ) == 0 ) {
                    config_demux.mode = DEMUX_SPAWN;
                    log_debug( "Working in DEMUX_SPAWN mode, new streams will spawn new processes." );

                } else if( strcmp( optarg, "merge" ) == 0 ) {
                    config_demux.mode = DEMUX_MERGE;
                    log_debug( "Working in DEMUX_MERGE mode, stream addresses with masquerade as the local address." );

                } else
                    log_warn( "Unrecognised demux mode '%s', ignored. Using default (NOOP)", optarg );
                
                break;

            case ARG_IMMEDIATE:
                log_warn( "Immediate mode!" );
                config.arg_immediate = true;
                break;

            case ARG_VERSION:
                printf( "Version: #%s\n", GIT_HASH );
                return EXIT_SUCCESS;
            
            case ARG_DELIMITER:
            case 'd':
                if( optarg[0] == '\\') {
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
                    config.arg_delimiter = optarg[0];
                printf( "Delimiter = '%c'\n", config.arg_delimiter );
                break;

            case 'v':
                config.arg_verbosity++;

                if (config.arg_verbosity == 2)
                    log_setLevel(INFO);
                else if (config.arg_verbosity == 3)
                    log_setLevel(DEBUG);
                
                fprintf( stderr, "Verbosity = %d\n", config.arg_verbosity );

                break;
            
            case 'q':
                fprintf( stderr, "Verbosity = %d\n", config.arg_verbosity );

                break;

            default:
                // Note - This probably won't ever happen, as unknown arguments are assumed to be for the binary
                //        target, rather than ourselves :/
                log_warn("Unknown argument: %d, ignored", indexPtr);
        }
    }

    // Read beyond the normal arguments to get the inner binary arguments
    if( argc - optind > 0 ) {
        log_info( "Additional command line arguments, configuring inner binary..." );

        const int inner_binary_index = optind;

        config_binary.path = (char *)malloc( 2048 );
        memset( config_binary.path, 0, 2048 );

        config_binary.argc = argc - optind;

        // Search using the system path for the 'real' binary
        findRealPath( config_binary.path, argv[inner_binary_index] );

        for( int i = 0; i < config_binary.argc; i++ ){
            config_binary.argv[i] = argv[inner_binary_index + i];
        }
        config_binary.argv[config_binary.argc] = NULL;

        if( strlen(config_binary.path) == 0 ) {
            log_error( "Could not find %s anywhere on your PATH. STOP.", argv[inner_binary_index] );
            exit( 0 );
        }
    }

    //set_input_mode();

    log_debug( "Going live!" );
    int rfd = getRouterFD();
    assert( rfd != -1, "Unable to connect to the router, STOP." );
    addNewWatch( rfd );
    
    // Ask for an address, note that the router may re-issue the reply, if it needed to.
    log_debug( "Asking router for an address..." );
    if( config.arg_address == 0 ) {
        log_info( "Requesting an address from the router..." );
        gnw_sendCommand(rfd, GNW_CMD_NEW_ADDRESS);
    }
    else {
        // Bit of a hack - send the requested address with the new address request... Might just be ignored at the router, though!
        log_info( "Requesting address [%08x] from the router...", config.arg_address );
        unsigned char reqBuffer[5] = { 0 };
        uint8_t * ptr = packet_write_u8( reqBuffer, GNW_CMD_NEW_ADDRESS );
        ptr = packet_write_u32( ptr, config.arg_address );
        gnw_emitCommandPacket( rfd, GNW_COMMAND, reqBuffer, 5 );
    }

    // Do we need to pre-load the inner binary?
    if( config.arg_immediate == true ) {
        log_info( "Immediate flag set, starting new binary as this node's address (%08x)", config.arg_address );
        assert( config.arg_address != 0, "Tried to start a new node in immediate mode without a valid address! STOP." );
        autoSpawn( config.arg_address );
    }

    int autoprint = 0;
    int status = 0;
    while( status == 0 ) {

        // Debug sink dump
        fprintf( stderr, "Sinks:\n" );
        khint_t iter = kh_begin( sinkTable );
        while( iter != kh_end( sinkTable ) ) {
            if( kh_exist( sinkTable, iter ) == 1 ) {
                sink_context_t * tmp = &kh_value( sinkTable, iter );
                fprintf(
                    stderr,
                    "\t%d -> %08x (%ld/%ld) [%ld/%ld]\n",
                    iter,
                    kh_key( sinkTable, iter ),
                    tmp->bytes_in,
                    tmp->bytes_out,
                    tmp->packets_in,
                    tmp->packets_out );
            }
            iter++;
        }
        fprintf( stderr, "\n" );

        autoprint = 0;
        int rv = -1;
        while( (rv = poll(stream_fd, MAX_INPUT_STREAMS, 100)) > -1 && status == 0 && ++autoprint < 1000 ) {

            /*for( int i=0; i<32; i++ )
                fprintf( stderr, "%02x ", rx_buffer[i] );
            fprintf( stderr, "\n" );*/

            for( int index = 0; index < MAX_INPUT_STREAMS; index++ ) {
                if( stream_fd[index].revents != 0 ) {

                    // Is this from the router?
                    if( stream_fd[index].fd == getRouterFD() ) {
                        handleRemoteData( &(stream_fd[index].fd), &status, stream_fd[index].revents );
                        stream_fd[index].revents = 0;
                    }
                    else { // Otherwise, this must be going TO the router, forward!
                        if( config.arg_address != 0 ) {
                            handleLocalData( &(stream_fd[index].fd), &status, stream_fd[index].revents );
                            stream_fd[index].revents = 0;
                        }
                    }

                    break;
                }

            }
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