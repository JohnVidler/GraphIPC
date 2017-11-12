#include <stdio.h>

/**
 * A semi-pointless program to just emit the arguments the program is provided with
 * to test the wrapping functions.
 */
int main(int argc, char ** argv ) {
    printf( "%d arguments\n", argc );
    for( int i=0; i<argc; i++ )
        printf( "  [%d] %s\n", i, argv[i] );

    return 0;
}