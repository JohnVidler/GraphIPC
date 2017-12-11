//
// Created by john on 11/12/17.
//

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "RingBuffer.h"

void assert( bool state, char * errorMessage ) {
    if( !state ) {
        fprintf( stderr, errorMessage );
        exit( EXIT_FAILURE );
    }
}

void test_ring_buffer() {
    printf( "Running ring buffer tests\n" );

    RingBuffer_t * buffer = ringbuffer_init( 128 );

    for( int i=0; i<20; i++ ) {
        char *testString = "Test string\n";
        ssize_t written = ringbuffer_write(buffer, testString, 12);
        printf( "Wrote: %lu B, Length = %lu B\n", written, ringbuffer_length(buffer) );
    }



}

int main(int argc, char * argv ) {

    test_ring_buffer();

    printf( "Done. Bye\n" );

    return EXIT_SUCCESS;
}