//
// Created by john on 11/12/17.
//

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include "RingBuffer.h"

unsigned int testsPassed = 0;

void _assert( bool state, char * errorMessage, int line ) {
    if( !state ) {
        fprintf( stderr, "%s:%u\tASSERT: %s\n", __FILE__, line, errorMessage );
        exit( EXIT_FAILURE );
    }
    testsPassed++;
}

#define assert(state, message) _assert( state, message, __LINE__ )

void test_ring_buffer() {
    RingBuffer_t * buffer = ringbuffer_init( 128 );
    assert( buffer != NULL, "No ring buffer structure was generated" );
    assert( ringbuffer_length(buffer) == 0, "Ring buffer was not initially empty" );

    for( int i=0; i<10; i++ ) {
        char *testString = "Test string.";
        ssize_t written = ringbuffer_write(buffer, testString, 12);
        assert( written == 12, "Ring refused to write new data" );
        assert( ringbuffer_length(buffer) == (i+1)*12, "Buffer length did not match input length" );
    }
    assert( ringbuffer_length(buffer) == 120, "Buffer should have 120B of data at this point." );

    for( int i=0; i<20; i++ ) {
        char out[7] = { '-', 0 };
        ssize_t read = ringbuffer_read( buffer, out, 6 );
        assert( read == 6, "Ring refused to supply a full buffer" );
    }
    assert( ringbuffer_length(buffer) == 0, "Buffer should be empty at this point." );

    // Read/Write integrity

    char *longInputTestString = "This booklet has been produced for students at the University of New South Wales";
    assert( ringbuffer_write( buffer, longInputTestString, strlen(longInputTestString) ) == strlen(longInputTestString), "Ring refused to write new data" );

    assert( ringbuffer_peek( buffer ) == 'T', "Peek'd value is not as expected" );

    char longOutputTestString[ 128 ] = { 0 };
    assert( ringbuffer_read( buffer, longOutputTestString, 128 ) == 80, "Did not completely drain the buffer! (Or over-read!)" );
}

int main(int argc, char * argv ) {

    test_ring_buffer();

    printf( "%u Tests passed.\n", testsPassed );
    printf( "Done. Bye\n" );

    return EXIT_SUCCESS;
}