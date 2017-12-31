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

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0
#define SOCKET_IN   1

extern char **environ;

// Configurable Options //
struct _configuration {
    int network_mtu;                // default = 1500
    uint16_t     router_port;   // default = 19000
    unsigned int read_timeout;  // default = 5
    bool         daemonize;     // default = false
    uint64_t     graph_address;     // default = 0

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

// Process and Network Connections //
int routerConnect() {
    struct addrinfo hints;
    struct addrinfo *clientinfo;
    int retval;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Hard coded loopback, for now... -John
    if ((retval = getaddrinfo("127.0.0.1", ROUTER_PORT, &hints, &clientinfo)) != 0)
        fail( "Unable to get a valid local socket to use... no more FDs available?" );

    // loop through all the results and connect to the first we can
    int sockfd = -1;
    struct addrinfo *p;
    for(p = clientinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL)
        fail( "Unable to connect to the graph router... is one running?" );

    freeaddrinfo(clientinfo); // all done with this structure

    return sockfd;
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
    config.network_mtu      = 1500;
    config.router_port  = 19000;
    config.daemonize    = false;
    config.read_timeout = 5;
    config.graph_address    = 0;

    config.mode_input       = false;
    config.mode_output      = false;

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;

    sigaction( SIGINT, &sa, NULL );
    sigaction( SIGHUP, &sa, NULL );
    sigaction( SIGKILL, &sa, NULL );

    // Argument Parsing //
    char arg;
    while( (arg = getopt(argc, argv, "p:t:dio")) != -1 ) {
        switch( arg ) {
            case 'p':
                config.router_port = (uint16_t)strtoul( optarg, NULL, 10 );
                break;

            case 't':
                config.read_timeout = (unsigned int) strtoul( optarg, NULL, 10 );
                break;

            case 'd':
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
    int data_fd = routerConnect();
    if( data_fd < 0 )
        fail( "Unable to get a connection to the graph router. STOP." );

    char iBuffer[config.network_mtu];
    RingBuffer_t * rx_buffer = ringbuffer_init( config.network_mtu );

    gnw_sendCommand( data_fd, GNW_CMD_NEW_ADDRESS );

    int client_state = GNW_STATE_OPEN;
    while( client_state != GNW_STATE_RUN ) {

        memset( iBuffer, 0, config.network_mtu );

        ssize_t bytesRead = read( data_fd, iBuffer, config.network_mtu );
        if( bytesRead <= 0 ) {
            fprintf( stderr, "IO Error, halting!\n" );
            external_shutdown();
            return EXIT_FAILURE;
        }
        if( ringbuffer_write( rx_buffer, iBuffer, bytesRead ) != bytesRead )
            fprintf( stderr, "Buffer overflow! Data loss occurred!" );

        gnw_header_t header;
        if( gnw_nextHeader( rx_buffer, &header ) ) {
            switch ( client_state ) {
                case GNW_STATE_OPEN:
                    ringbuffer_read( rx_buffer, &config.graph_address, 8 );

                    printf( "Address registered as %llu\n", config.graph_address );

                    client_state = GNW_STATE_RUN;
                    break;
            }
        }
    }

    // Become a background daemon //
    // Probably should become a child of the resident router, but this will work for now... -John.
    if( config.daemonize ) {
        printf( "%llu\n", config.graph_address );
        daemonize();
    } else {
        printf( "Address = %llu\n", config.graph_address );
    }

    // Get the pipes together
    int wrap_stdin[2];
    int wrap_stdout[2];

    if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");

    if( pipe( wrap_stdout ) == -1 )
        fail( "Could not create wrapper for stdout" );

    // Are we in input mode?
    if( config.mode_input ) {
        struct pollfd watch_fd[1];
        memset( watch_fd, 0, sizeof( struct pollfd ) );

        watch_fd[0].fd = STDIN_FILENO;
        watch_fd[0].events = POLLIN;

        char buffer[config.network_mtu];

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 0)) != -1 ) {
            if( rv > 0 ) {
                memset( buffer, 0, config.network_mtu );
                ssize_t readBytes = read( watch_fd[0].fd, buffer, config.network_mtu );

                //fprintf( stderr, "Read: %llu B\n", readBytes );

                if( readBytes == 0 ) {
                    fprintf( stderr, "Input shut down, stopping." );
                    break;
                }

                gnw_emitDataPacket( data_fd, buffer, readBytes );

                usleep( 100 );
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

        char buffer[4096] = { 0 };

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 0)) != -1 ) {
            if( rv > 0 ) {
                memset( buffer, 0, 4096 );
                ssize_t readBytes = read( watch_fd[0].fd, buffer, 4096 );

                if( readBytes == 0 ) {
                    fprintf( stderr, "NULL read, lost connection to router?\n" );
                    break;
                }

                char * payload = buffer + sizeof( gnw_header_t );
                ssize_t writeBytes = write( STDOUT_FILENO, payload, readBytes - sizeof(gnw_header_t) );

                fprintf( stderr, "Wrote: (%llu) %llu B\n", readBytes - sizeof(gnw_header_t), writeBytes );

                assert( readBytes - sizeof(gnw_header_t) == writeBytes, "Could not push all received bytes to the terminal. Data has been lost." );
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
                    char buffer[1024] = {0};

                    ssize_t bytesRead = read(watch_fd[PROCESS_OUT].fd, &buffer, 1024);
                    gnw_emitDataPacket( data_fd, buffer, bytesRead );
                }
                watch_fd[PROCESS_OUT].revents = 0; // Manual reset, not strictly required, but just in case...
            }

            // Handle any events on the input stream from the router
            if( watch_fd[SOCKET_IN].revents != 0 ) {
                if( watch_fd[SOCKET_IN].revents & POLLIN == POLLIN ) {
                    char buffer[1024] = {0};

                    ssize_t bytesRead = read(watch_fd[SOCKET_IN].fd, buffer, 1024);

                    gnw_dumpPacket( stdout, buffer, bytesRead );

                    gnw_header_t * header = (gnw_header_t *)buffer;
                    ssize_t length = bytesRead - sizeof( gnw_header_t );
                    char * payload = buffer + sizeof( gnw_header_t );
                    assert( header->type == GNW_DATA, "Non-Data packet?" );

                    ssize_t bytesWritten = write( PIPE_WRITE(wrap_stdin), payload, length );
                    assert( length == bytesWritten, "Could not push all data to the wrapped process!" );

                    if( bytesRead == 0 ) {
                        fail( "Router connection hung up! Crash?" );
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