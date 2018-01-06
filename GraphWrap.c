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
#include <wait.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <syslog.h>
#include <netdb.h>
#include <poll.h>
#include "lib/GraphNetwork.h"
#include "lib/Assert.h"
#include "common.h"

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0
#define SOCKET_IN   1

extern char **environ;

// Configurable Options //
struct _configuration {
    int           network_mtu;                // default = 1500
    uint16_t      router_port;   // default = 19000
    unsigned int  read_timeout;  // default = 5
    bool          daemonize;     // default = false
    gnw_address_t graph_address;     // default = 0
    gnw_address_t link_upstream;
    gnw_address_t link_downstream;

    bool mode_input;                // default = false
    bool mode_output;               // default = false
};
struct _configuration config;

pid_t childPID = -1;

void external_shutdown() {
    fprintf( stderr, "A shutdown event occurred\n" );
    // Shut down the child, if the parent is closing.
    if( childPID != -1 ) {
        fprintf(stderr, "Shutting down process %d\n", childPID);

        printf( "KILL\n" );

        // Forward the signal to the child
        kill(childPID, SIGKILL);

        sleep( 1 );

        // Wait for the child to shut down
        waitpid(childPID, NULL, 0);

        fprintf( stderr, "Child process exited\n" );

        // Reset the child PID so we don't wait forever for an already dead process
        childPID = -1;
    }
}

void sigint_handler( int signum ) {
    external_shutdown();
    exit( EXIT_SUCCESS );
}

void fail( const char * err ) {
    if( config.daemonize )
        syslog( LOG_ERR, "%s\n", err );
    else
        fprintf( stderr, "%s\n", err );

    external_shutdown();
    exit(EXIT_FAILURE);
}

void daemonize() {
    // Become a child of the current terminal
    pid_t pid = fork();
    if (pid < 0)
        exit( EXIT_FAILURE );

    if( pid > 0 )
        exit( EXIT_SUCCESS );

    if( setsid() < 0 )
        exit( EXIT_FAILURE );

    // ToDo: Implement working signal handler
    signal( SIGCHLD, SIG_IGN );
    signal( SIGHUP, SIG_IGN );

    // Become a daemon proper...
    pid = fork();
    if(pid < 0)
        exit( EXIT_FAILURE );

    if(pid > 0)
        exit( EXIT_SUCCESS );

    // New file permissions time
    umask(0);

    // Move to a sane working directory
    // root for now...
    //chdir("/");

    // Close all open FDs
    for( long fd = sysconf(_SC_OPEN_MAX); fd>=0; fd-- ) {
        close( fd );
    }

    // Daemon is online
    openlog( LOG_NAME, LOG_PID, LOG_DAEMON );
}

/**
 * Attempts to find a particular file from the available PATH environment variable along with
 * the current working directory.
 *
 * If no PATH-prefixed path exists, the target will be filled with the original binary path.
 *
 * <b>Usage</b>
 * <pre>
 * char path[128] = { 0 };
 * findRealPath( path, "binary_file" );
 * </pre>
 *
 * @param target Character pointer to be filled with the path. Must be large enough to handle the result!
 * @param binary The file to search for.
 */
void findRealPath( char * target, const char * binary ) {
    char pathBuffer[2048] = { 0 };
    size_t env_path_length = strlen( getenv( "PATH" ) );
    char * path = (char *)malloc( env_path_length + 1 );
    memcpy( path, getenv( "PATH" ), env_path_length );

    char * parent = strtok( path, ":" );
    while( parent != NULL ) {
        memset( pathBuffer, 0, 2048 );
        sprintf( pathBuffer, "%s/%s", parent, binary );

        if( access(pathBuffer, F_OK) != -1 ) {
            sprintf( target, "%s/%s", parent, binary );
            free( path );
            return;
        }

        parent = strtok( NULL, ":" );
    }

    // Guess we must have an actual path already...
    sprintf( target, "%s", binary );
    free( path );
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

    if( config.daemonize )
        syslog( LOG_INFO, "Process started\n" );
    else
        fprintf( stderr, "Process started\n" );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        if( config.daemonize )
            syslog( LOG_INFO, "Process terminated with error: %s\n", strerror(errno) );
        else
            fprintf( stderr, "Process terminated with error: %s\n", strerror(errno) );

    return -1;
}

int main(int argc, char ** argv ) {
    // Configuration defaults
    config.network_mtu     = 1500;
    config.router_port     = 19000;
    config.daemonize       = false;
    config.read_timeout    = 5;
    config.graph_address   = 0;
    config.link_upstream   = 0; // Inherently invalid
    config.link_downstream = 0; // Inherently invalid

    config.mode_input    = false;
    config.mode_output   = false;

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;

    sigaction( SIGINT, &sa, NULL );
    sigaction( SIGHUP, &sa, NULL );
    sigaction( SIGKILL, &sa, NULL );

    // Check the system MTU and match it - this assumes local operation, for now.
    // Note: Possibly add an override for this in the flags...
    config.network_mtu = getIFaceMTU( "lo" );

    if( config.network_mtu > 4096 ) // Excessive copy op.
        config.network_mtu = 1024;

    // Argument Parsing //
    char arg;
    while( (arg = getopt(argc, argv, "u:d:p:t:bio")) != -1 ) {
        switch( arg ) {
            case 'u':
                config.link_upstream = (gnw_address_t)strtoul( optarg, NULL, 16 );
                break;

            case 'd':
                config.link_downstream = (gnw_address_t)strtoul( optarg, NULL, 16 );
                break;

            case 'p':
                config.router_port = (uint16_t)strtoul( optarg, NULL, 10 );
                break;

            case 't':
                config.read_timeout = (unsigned int) strtoul( optarg, NULL, 10 );
                break;

            case 'b':
                config.daemonize = true;
                break;

            case 'i': // Pipe input mode
                printf( "Input mode\n" );
                if( config.mode_output )
                    fail( "Cannot be both an input *and* an output node! STOP." );
                config.mode_input = true;
                break;

            case 'o': // Pipe output mode
                printf( "Output mode\n" );
                if( config.mode_input )
                    fail( "Cannot be both an input *and* an output node! STOP." );
                config.mode_output = true;
                break;

            case '?':
                printf( "Opt: %s, Arg: %s\n", optopt, optarg );
                break;

            default:
                break;
        }
    }

    // Connect to the local router.
    int data_fd = socket_connect("127.0.0.1", ROUTER_PORT);
    if( data_fd < 0 )
        fail( "Unable to get a connection to the graph router. STOP." );

    printf( "INFO\tUsing %d B MTU\n", config.network_mtu );

    char iBuffer[config.network_mtu];
    gnw_state_t parser_context = { .state = 0 };
    RingBuffer_t * rx_buffer = ringbuffer_init( config.network_mtu * 20 ); // 20 packets...ish...

    // While we haven't been assigned an address, keep listening.
    // Note - for future versions, this means that the address should be the
    // last parameter sent to the client, unless the protocol changes vastly. -John.
    while( config.graph_address == 0 ) {
        // Ask for an address
        gnw_sendCommand( data_fd, GNW_CMD_NEW_ADDRESS );

        ssize_t bytesRead = read( data_fd, iBuffer, config.network_mtu );
        if( bytesRead <= 0 ) {
            fprintf( stderr, "IO Error, halting!\n" );
            external_shutdown();
            return EXIT_FAILURE;
        }
        if( ringbuffer_write( rx_buffer, iBuffer, bytesRead ) != bytesRead )
            fprintf( stderr, "Buffer overflow! Data loss occurred!" );

        while( gnw_nextPacket( rx_buffer, &parser_context, iBuffer ) ) {
            gnw_header_t * header = (gnw_header_t *)iBuffer;
            unsigned char * payload = (unsigned char *)( iBuffer + sizeof(gnw_header_t) );

            if( header->version != GNW_VERSION )
                fprintf( stderr, "Warning! Router/Client version mismatch!\n" );

            switch( header->type ) {
                case GNW_COMMAND | GNW_REPLY: // Command response
                    if( header->length < 1 ) {
                        fprintf( stderr, "Router sent a command with no operator, no idea what to do!\n" );
                        break;
                    }

                    switch( *payload ) {
                        case GNW_CMD_NEW_ADDRESS:
                            if( config.graph_address == 0 ) {
                                config.graph_address = *((gnw_address_t *) (payload + 1));

                                config.graph_address = 0xFFFFFFFF & config.graph_address;
                            }
                            break;

                        default:
                            fprintf( stderr, "Unknown command response?\n" );
                            break;
                    }

                    break;

                default:
                    fprintf( stderr, "Unknown response from router, skipping. (%x)\n", header->type );
            }
        }
    }

    // Become a background daemon //
    // Probably should become a child of the resident router, but this will work for now... -John.
    if( config.daemonize ) {
        printf( "%lx\n", config.graph_address );
        daemonize();
    } else {
        printf( "Address = %lx\n", config.graph_address );
    }

    // Get the pipes together
    int wrap_stdin[2];
    int wrap_stdout[2];

    if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");

    if( pipe( wrap_stdout ) == -1 )
        fail( "Could not create wrapper for stdout" );

    // Magic autoconnecting! :)
    if( config.link_downstream != 0 )
        gnw_request_connect( data_fd, config.graph_address, config.link_downstream );
    if( config.link_upstream != 0 )
        gnw_request_connect( data_fd, config.link_upstream, config.graph_address );

    // Are we in input mode?
    if( config.mode_input ) {

        struct pollfd watch_fd[1];
        memset( watch_fd, 0, sizeof( struct pollfd ) );

        watch_fd[0].fd = STDIN_FILENO;
        watch_fd[0].events = POLLIN;

        char buffer[config.network_mtu];

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 1000)) != -1 ) {
            if( rv > 0 ) {
                memset( buffer, 0, config.network_mtu );
                ssize_t readBytes = read( watch_fd[0].fd, buffer, config.network_mtu );

                if( readBytes == 0 ) {
                    fprintf( stderr, "Input shut down, stopping." );
                    break;
                }

                gnw_emitDataPacket( data_fd, buffer, readBytes );
            }
        }

        // Done, close the FD
        close( data_fd );
        external_shutdown();
        return EXIT_SUCCESS;
    }

    // Are we in output mode?
    if( config.mode_output ) {
        struct pollfd watch_fd[1];
        memset( watch_fd, 0, sizeof( struct pollfd ) );

        watch_fd[0].fd = data_fd;
        watch_fd[0].events = POLLIN;

        char buffer[config.network_mtu];

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 1000)) != -1 ) {
            if( rv > 0 ) {
                ssize_t bytesRead = read( data_fd, buffer, config.network_mtu );
                if( bytesRead <= 0 ) {
                    fprintf( stderr, "IO Error, halting!\n" );
                    external_shutdown();
                    return EXIT_FAILURE;
                }
                if( ringbuffer_write( rx_buffer, buffer, bytesRead ) != bytesRead )
                    fprintf( stderr, "Buffer overflow! Data loss occurred!" );

                while( gnw_nextPacket( rx_buffer, &parser_context, buffer ) ) {
                    gnw_header_t *header = (gnw_header_t *) buffer;
                    unsigned char *payload = (unsigned char *) (buffer + sizeof(gnw_header_t));
                    *(payload + header->length) = '\0'; // Kludge?

                    if (header->version != GNW_VERSION)
                        fprintf(stderr, "Warning! Router/Client version mismatch!\n");

                    if( header->type != GNW_DATA )
                        fprintf( stderr, "Warning! Router is emitting non-data packets in data mode!\n" );

                    if( header->type == GNW_DATA ) {
                        fprintf( stdout, "%s", payload );
                        fflush( stdout );
                    }
                }
            }
        }

        // Done, close the FD
        close( data_fd );
        external_shutdown();
        return EXIT_SUCCESS;
    }

    const int inner_binary_index = optind;
    const int inner_binary_argc  = argc - optind;
    char inner_binary[2048] = { 0 };

    // Search using the system path for the 'real' binary
    findRealPath( inner_binary, argv[inner_binary_index] );

    char * newArgs[inner_binary_argc];
    for( int i=0; i<inner_binary_argc; i++ ) {
        newArgs[i] = argv[inner_binary_index + i];
    }
    newArgs[inner_binary_argc] = NULL;

    // Become two processes, launch the child.
    childPID = fork();
    if( childPID == 0 ) {
        processRunner(PIPE_READ(wrap_stdin), PIPE_WRITE(wrap_stdout), inner_binary, newArgs);
        return EXIT_FAILURE; // Should never happen...
    }

    printf( "CHILD PID:\t%d\n", childPID );

    struct pollfd watch_fd[2];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 2 );

    watch_fd[PROCESS_OUT].fd = PIPE_READ(wrap_stdout);
    watch_fd[PROCESS_OUT].events = POLLIN;

    watch_fd[SOCKET_IN].fd = data_fd;
    watch_fd[SOCKET_IN].events = POLLIN;

    int status = 0;
    while( status == 0 ) {
        int rv = poll( watch_fd, 2, config.read_timeout * 1000 );

        if( rv == -1 ) {        // Error state while poll'ing
            fprintf( stderr, "Error during poll cycle for input. Shutting down.\n" );
            break;
        } else if( rv == 0 ) {  // Timed out on poll wait

            // Has the wrapped process exited/died?
            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                if (config.daemonize)
                    syslog(LOG_INFO, "Process terminated (exit code = %d)\n", WEXITSTATUS(status));
                else
                    fprintf(stderr, "Process terminated (exit code = %d)\n", WEXITSTATUS(status));
                break;
            }
        } else {  // Data ready, somewhere...

            // Handle any events on the process output
            if( watch_fd[PROCESS_OUT].revents != 0 ) {
                if (watch_fd[PROCESS_OUT].revents & POLLIN == POLLIN) {
                    char buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[PROCESS_OUT].fd, &buffer, config.network_mtu);
                    gnw_emitDataPacket( data_fd, buffer, bytesRead );
                }
                watch_fd[PROCESS_OUT].revents = 0; // Manual reset, not strictly required, but just in case...
            }

            // Handle any events on the input stream from the router
            if( watch_fd[SOCKET_IN].revents != 0 ) {
                if( watch_fd[SOCKET_IN].revents & POLLIN == POLLIN ) {
                    char buffer[config.network_mtu];

                    ssize_t bytesRead = read(watch_fd[SOCKET_IN].fd, buffer, config.network_mtu);

                    if( bytesRead == 0 ) {
                        fail( "Router connection hung up! Crash?" );
                    }

                    gnw_header_t * header = (gnw_header_t *)buffer;
                    ssize_t length = bytesRead - sizeof( gnw_header_t );
                    char * payload = buffer + sizeof( gnw_header_t );
                    if( header->type == GNW_DATA ) {
                        *(payload + header->length) = '\0'; // Zero the end of the buffer

                        ssize_t bytesWritten = write(PIPE_WRITE(wrap_stdin), payload, length);
                        assert(length == bytesWritten, "Could not push all data to the wrapped process!");
                    }
                }
            }
        }
    }

    external_shutdown();

    close( PIPE_READ(wrap_stdin) );
    close( PIPE_WRITE(wrap_stdin) );

    close( PIPE_READ(wrap_stdout) );
    close( PIPE_WRITE(wrap_stdout) );

    // Close the syslog, if we opened a connection to it
    if( config.daemonize )
        closelog();

    return EXIT_SUCCESS;
}