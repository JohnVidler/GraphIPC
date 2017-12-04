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
#include "utility.h"
#include "GraphNetwork.h"
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <syslog.h>
#include <netdb.h>
#include <poll.h>

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

#define PROCESS_OUT 0
#define SOCKET_IN   1

extern char **environ;

// Configurable Options //
uint16_t     arg_router_port  = 19000;
unsigned int arg_read_timeout = 5;
bool         arg_daemonize    = false;
uint64_t     graph_address    = 0;

bool mode_input = false;
bool mode_output = false;

// Utility Stuff //
void assert( bool state, char * warning ) {
    if( state )
        return;

    if( arg_daemonize )
        syslog( LOG_WARNING, "ASSERT FAILURE -> %s\n", warning );
    else
        fprintf( stderr, "ASSERT FAILURE -> %s\n", warning );
}

void fail( const char * err ) {
    if( arg_daemonize )
        syslog( LOG_ERR, "%s\n", err );
    else
        fprintf( stderr, "%s\n", err );
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
    if ((retval = getaddrinfo("127.0.0.1", ROUTER_PORT, &hints, &clientinfo)) != 0) {
        //fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retval));
        fail( "Unable to get a valid local socket to use... no more FDs available?" );
    }

    // loop through all the results and connect to the first we can
    int sockfd = -1;
    struct addrinfo *p;
    for(p = clientinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
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

void findRealPath( char * target, const char * binary ) {
    char pathBuffer[2048] = { 0 };
    char * path = (char *)malloc( 2048 );
    memcpy( path, getenv( "PATH" ), strlen(getenv( "PATH" )) );

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

int processRunner( int wrap_stdin, int wrap_stdout, const char * cmd, char ** argv ) {
    // Capture stdout to wrap_stdout
    dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    if( arg_daemonize )
        syslog( LOG_INFO, "Process started\n" );
    else
        fprintf( stderr, "Process started\n" );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        if( arg_daemonize )
            syslog( LOG_INFO, "Process terminated with error: %s\n", strerror(errno) );
        else
            fprintf( stderr, "Process terminated with error: %s\n", strerror(errno) );

    return -1;
}

int queryMode( int argc, char ** argv, int index ) {
    // Connect to the local router.
    int data_fd = routerConnect();
    if( data_fd < 0 )
        fail( "Unable to get a connection to the graph router. STOP." );

    do {
        //argv[index]
        if( strncasecmp( "list", argv[index], 4 ) == 0 ) {
            gnw_sendCommand( data_fd, GNW_CMD_ADDRESS_LIST );

            char buffer[2048] = { 0 };
            ssize_t length = 0;
            if( (length = gnw_wait( data_fd, GNW_ACK, buffer, 2048 )) > 0 ) {
                printf( "Got ACK\n" );
            }
        }

    } while( ++index < argc );

    return EXIT_SUCCESS;
}

int main(int argc, char ** argv ) {
    // Argument Parsing //
    char arg;
    while( (arg = getopt(argc, argv, "p:t:dqio")) != -1 ) {
        switch( arg ) {
            case 'p':
                arg_router_port = (uint16_t)strtoul( optarg, NULL, 10 );
                break;

            case 't':
                arg_read_timeout = (unsigned int) strtoul( optarg, NULL, 10 );
                break;

            case 'd':
                arg_daemonize = true;
                break;

            case 'i': // Pipe input mode
                printf( "Input mode\n" );
                if( mode_output )
                    fail( "Cannot be both an input *and* an output node! STOP." );
                mode_input = true;
                break;

            case 'o': // Pipe output mode
                printf( "Output mode\n" );
                if( mode_input )
                    fail( "Cannot be both an input *and* an output node! STOP." );
                mode_output = true;
                break;

            case 'q':
                return queryMode( argc, argv, optind );

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

    char buffer[128] = { 0 };
    if( gnw_wait(data_fd, GNW_ACK, buffer, 128) < 0 )
        fail( "No response from router. STOP." );

    gnw_sendCommand( data_fd, GNW_CMD_NEW_ADDRESS );

    int client_state = GNW_STATE_OPEN;
    gnw_header_t * header = (gnw_header_t *)buffer;
    while( client_state != GNW_STATE_RUN ) {
        memset( buffer, 0, 128 );

        ssize_t length = gnw_wait( data_fd, GNW_ACK, buffer, 128 );
        if( length < 0) {
            fprintf(stderr, "Error while waiting for router ACK response. STOP.\n");
            close( data_fd );
            exit( EXIT_FAILURE );
        }

        switch ( client_state ) {
            case GNW_STATE_OPEN:
                if( header->type == GNW_ACK && length == sizeof(gnw_header_t) + 8 ) {
                    graph_address = *(uint64_t *)( buffer + sizeof(gnw_header_t) );
                    client_state = GNW_STATE_RUN;
                }
                break;
        }
    }

    // Become a background daemon //
    // Probably should become a child of the resident router, but this will work for now... -John.
    if( arg_daemonize ) {
        printf( "%llu\n", graph_address );
        daemonize();
    } else {
        printf( "Address = %llu\n", graph_address );
    }

    // Get the pipes together
    int wrap_stdin[2];
    int wrap_stdout[2];

    if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");

    if( pipe( wrap_stdout ) == -1 )
        fail( "Could not create wrapper for stdout" );

    // Are we in input mode?
    if( mode_input ) {
        struct pollfd watch_fd[1];
        memset( watch_fd, 0, sizeof( struct pollfd ) );

        watch_fd[0].fd = STDIN_FILENO;
        watch_fd[0].events = POLLIN;

        char buffer[1024] = { 0 };

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 0)) != -1 ) {
            if( rv > 0 ) {
                memset( buffer, 0, 1024 );
                ssize_t readBytes = read( watch_fd[0].fd, buffer, 1024 );

                if( readBytes == 0 ) {
                    fprintf( stderr, "Input shut down, stopping." );
                    break;
                }

                gnw_emitDataPacket( data_fd, buffer, readBytes );
            }
        }

        // Done, close the FD
        close( data_fd );
        return EXIT_SUCCESS;
    }

    // Are we in output mode?
    if( mode_output ) {
        struct pollfd watch_fd[1];
        memset( watch_fd, 0, sizeof( struct pollfd ) );

        watch_fd[0].fd = data_fd;
        watch_fd[0].events = POLLIN;

        char buffer[1024] = { 0 };

        int rv = 0;
        while( (rv = poll(watch_fd, 1, 0)) != -1 ) {
            if( rv > 0 ) {
                memset( buffer, 0, 1024 );
                ssize_t readBytes = read( watch_fd[0].fd, buffer, 1024 );

                if( readBytes == 0 ) {
                    fprintf( stderr, "NULL read, lost connection to router?\n" );
                    break;
                }
                ssize_t writeBytes = write( STDOUT_FILENO, buffer + sizeof(gnw_header_t), readBytes - sizeof(gnw_header_t) );

                assert( readBytes - sizeof(gnw_header_t) == writeBytes, "Could not push all received bytes to the terminal. Data has been lost." );
            }
        }

        // Done, close the FD
        close( data_fd );
        return EXIT_SUCCESS;
    }

    const int inner_binary_index = optind;
    const int inner_binary_argc  = argc - optind;
    char inner_binary[2048] = { 0 };

    // Search using the system path for the 'real' binary
    findRealPath( inner_binary, argv[inner_binary_index] );

    fprintf( stdout, "Found @ %s\n", inner_binary );
    fflush( stdout );

    char * newArgs[inner_binary_argc];
    for( int i=0; i<inner_binary_argc; i++ ) {
        newArgs[i] = argv[inner_binary_index + i];
    }
    newArgs[inner_binary_argc] = NULL;

    // Become two processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 ) {
        processRunner(PIPE_READ(wrap_stdin), PIPE_WRITE(wrap_stdout), inner_binary, newArgs);
        return EXIT_FAILURE; // Should never happen...
    }

    struct pollfd watch_fd[2];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 2 );

    watch_fd[PROCESS_OUT].fd = PIPE_READ(wrap_stdout);
    watch_fd[PROCESS_OUT].events = POLLIN;

    watch_fd[SOCKET_IN].fd = data_fd;
    watch_fd[SOCKET_IN].events = POLLIN;

    int status = 0;
    while( status == 0 ) {
        int rv = poll( watch_fd, 2, arg_read_timeout * 1000 );

        if( rv == -1 ) {        // Error state while poll'ing
            perror( "select" );
            break;
        } else if( rv == 0 ) {  // Timed out on poll wait

            // Has the wrapped process exited/died?
            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                if (arg_daemonize)
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

    waitpid( -1, NULL, 0 );

    close( PIPE_READ(wrap_stdin) );
    close( PIPE_WRITE(wrap_stdin) );

    close( PIPE_READ(wrap_stdout) );
    close( PIPE_WRITE(wrap_stdout) );

    // Close the syslog, if we opened a connection to it
    if( arg_daemonize )
        closelog();

    return EXIT_SUCCESS;
}