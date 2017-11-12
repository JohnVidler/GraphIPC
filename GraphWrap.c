#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <errno.h>
#include <stdbool.h>
#include "utility.h"
#include <string.h>

#define PIPE_READ  0
#define PIPE_WRITE 1

extern char **environ;

void fail( const char * err ) { fprintf(stderr, "%s\n", err); exit(EXIT_FAILURE); }

int processRunner( int wrap_stdin, int wrap_stdout, const char * cmd, char ** argv ) {

    // Capture stdout to wrap_stdout
    //dup2( wrap_stdin,  STDIN_FILENO  );
    dup2( wrap_stdout, STDOUT_FILENO );

    int result = execve( cmd, argv, environ );

    if( result != 0 )
        printf( "Process terminated with error: %s\n", strerror(errno) );

    printf( "EXIT" );

    return -1;
}

int main(int argc, char ** argv ) {

    // Get the pipes together
    //int wrap_stdin[2];
    int wrap_stdout[2];

    /*if( pipe( wrap_stdin ) == -1 )
        fail("Could not create wrapper for stdin");*/

    if( pipe( wrap_stdout ) == -1 )
        fail( "Could not create wrapper for stdout" );

    // Become two processes, launch the child.
    pid_t childPID = fork();
    if( childPID == 0 )
        return processRunner( 0, wrap_stdout[1], argv[1], argv+2 );

    printf( "Waiting for child process...\n" );
    waitpid( childPID, NULL, 0 );

    char buffer[1024] = { 0 };
    ssize_t bytes = read( wrap_stdout[0], &buffer, 1024 );

    printf( "From child: (%lu)\n%s\n", (unsigned long)bytes, buffer );

    printf( "Closing pipes...\n" );
    //close( wrap_stdin[0] );
    //close( wrap_stdin[1] );

    close( wrap_stdout[0] );
    close( wrap_stdout[1] );

    return 0;
}