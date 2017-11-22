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
#include <unistd.h>
#include <wait.h>
#include <errno.h>
#include "utility.h"
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <syslog.h>

#define LOG_NAME "GraphWrap"

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

extern char **environ;

// Configurable Options //
uint16_t     arg_router_port  = 19000;
unsigned int arg_read_timeout = 5;
bool         arg_daemonize    = false;

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
    chdir("/");

    // Close all open FDs
    for( long fd = sysconf(_SC_OPEN_MAX); fd>=0; fd-- ) {
        close( fd );
    }

    // Daemon is online
    openlog( LOG_NAME, LOG_PID, LOG_DAEMON );
}

int processRunner( int wrap_stdin, int wrap_stdout, const char * cmd, char ** argv ) {
    // Capture stdout to wrap_stdout
    dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    if( arg_daemonize )
        syslog( LOG_INFO, "Starting inner process %s\n", cmd );
    else
        fprintf( stderr, "Starting inner process %s\n", cmd );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        if( arg_daemonize )
            syslog( LOG_INFO, "Process terminated with error: %s\n", strerror(errno) );
        else
            fprintf( stderr, "Process terminated with error: %s\n", strerror(errno) );

    return -1;
}

int main(int argc, char ** argv ) {
    // Argument Parsing //
    char arg;
    while( (arg = getopt(argc, argv, "p:t:d")) != -1 ) {
        printf( "Index: %d\n", optind );
        switch( arg ) {
            case 'p':
                arg_router_port = (uint16_t)strtoul( optarg, NULL, 10 );
                break;

            case 't':
                arg_read_timeout = (unsigned int) strtoul( optarg, NULL, 10 );
                break;

            case 'd':
                arg_daemonize = true;
                printf( "Becoming a background process..." );
                break;

            case '?':
                printf( "Opt: %s, Arg: %s\n", optopt, optarg );
                break;

            default:
                break;
        }
    }
    const int inner_binary_index = optind;
    const char * inner_binary = argv[inner_binary_index];
    const int inner_binary_argc  = argc - optind;

    // Become a background daemon //
    // Probably should become a child of the resident router, but this will work for now... -John.
    if( arg_daemonize )
        daemonize();

    // Get the pipes together
    int wrap_stdin[2];
    int wrap_stdout[2];

    if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");

    if( pipe( wrap_stdout ) == -1 )
        fail( "Could not create wrapper for stdout" );

    char * newArgs[inner_binary_argc];
    for( int i=0; i<inner_binary_argc; i++ ) {
        newArgs[i] = argv[inner_binary_index + i];
    }
    newArgs[inner_binary_argc] = NULL;

    // Become two processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 )
        return processRunner( PIPE_READ(wrap_stdin), PIPE_WRITE(wrap_stdout), inner_binary, newArgs );

    FILE * writePipe = fdopen( PIPE_WRITE(wrap_stdin), "w" );

    //ToDo: Remove - add socket connection for input to forward to the child stdin pipe
    fprintf( writePipe, "As he crossed toward the pharmacy at the corner he involuntarily turned his head because of a burst of light that had ricocheted from his temple, and saw, with that quick smile with which we greet a rainbow or a rose, a blindingly white parallelogram of sky being unloaded from the van—a dresser with mirrors across which, as across a cinema screen, passed a flawlessly clear reflection of boughs sliding and swaying not arboreally, but with a human vacillation, produced by the nature of those who were carrying this sky, these boughs, this gliding façade.\n" );
    fflush( writePipe );


    struct timeval timeout;

    fd_set pipe_io;
    FD_ZERO( &pipe_io );
    FD_SET( PIPE_READ(wrap_stdout), &pipe_io );

    int status = 0;
    while( status == 0 ) {
        timeout.tv_sec = arg_read_timeout;
        timeout.tv_usec = 0;
        int rv = select(PIPE_READ(wrap_stdout) + 1, &pipe_io, NULL, NULL, &timeout);

        if( rv == -1 ) {
            perror( "select" );
            break;
        } else if( rv == 0 ) {

            int status = 0;
            int ret = waitpid( childPID, &status, WNOHANG );
            if( ret == -1 && WIFEXITED(status) ) {
                if( arg_daemonize )
                    syslog( LOG_INFO, "Process terminated (exit code = %d)\n", WEXITSTATUS(status) );
                else
                    fprintf( stderr, "Process terminated (exit code = %d)\n", WEXITSTATUS(status) );
                break;
            }
        } else {
            if( FD_ISSET(PIPE_READ(wrap_stdout), &pipe_io) ) {
                char buffer[1024] = { 0 };
                ssize_t bytesRead = read( PIPE_READ(wrap_stdout), &buffer, 1024 );

                if( arg_daemonize )
                    syslog( LOG_INFO, "%s", buffer );
                else
                    fprintf( stderr, "%s", buffer );
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

    return 0;
}