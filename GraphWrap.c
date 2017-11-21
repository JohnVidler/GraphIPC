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

#define PIPE_READ(PIPE) PIPE[0]
#define PIPE_WRITE(PIPE) PIPE[1]

extern char **environ;

void fail( const char * err ) { fprintf(stderr, "%s\n", err); exit(EXIT_FAILURE); }

int processRunner( int wrap_stdin, int wrap_stdout, const char * cmd, char ** argv ) {
    // Capture stdout to wrap_stdout
    //dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        fprintf( stderr, "Process terminated with error: %s\n", strerror(errno) );

    return -1;
}

int main(int argc, char ** argv ) {

    // Configurable Options //
    uint16_t     arg_router_port  = 19000;
    unsigned int arg_read_timeout = 5;

    // Argument Parsing //
    char arg;
    while( (arg = getopt(argc, argv, "p:t:")) != -1 ) {
        printf( "Index: %d\n", optind );
        switch( arg ) {
            case 'p':
                arg_router_port = (uint16_t)strtoul( optarg, NULL, 10 );
                break;

            case 't':
                arg_read_timeout = (unsigned int) strtoul( optarg, NULL, 10 );
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

    // Get the pipes together
    //int wrap_stdin[2];
    int wrap_stdout[2];

    /*if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");*/

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
        return processRunner( 0, PIPE_WRITE(wrap_stdout), inner_binary, newArgs );

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
                fprintf( stderr, "Process terminated (exit code = %d)\n", WEXITSTATUS(status) );
                break;
            }
        } else {
            if( FD_ISSET(PIPE_READ(wrap_stdout), &pipe_io) ) {
                char buffer[1024] = { 0 };
                ssize_t bytesRead = read( PIPE_READ(wrap_stdout), &buffer, 1024 );
                printf( "[%s]", buffer );
            }
        }
    }

    waitpid( -1, NULL, 0 );

    close( PIPE_READ(wrap_stdout) );
    close( PIPE_WRITE(wrap_stdout) );

    return 0;
}