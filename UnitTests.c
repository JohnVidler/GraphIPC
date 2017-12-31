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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <limits.h>
#include "lib/Assert.h"
#include "lib/RingBuffer.h"
#include "lib/GraphNetwork.h"

void test_ring_buffer() {
    RingBuffer_t * buffer = ringbuffer_init( 128 );
    ringbuffer_print( buffer );
    assert( buffer != NULL, "No ring buffer structure was generated" );
    assert( ringbuffer_capacity(buffer) == 127, "Ring buffer was not created with requested size" );
    assert( ringbuffer_length(buffer) == 0, "Ring buffer was not created with zero data" );

    char *testString = "Test string.";
    for( int i=0; i<10; i++ ) {
        ssize_t written = ringbuffer_write(buffer, testString, 12);
        assert( written == 12, "Ring refused to write new data" );
        assertEqual(ringbuffer_length(buffer), (i+1)*12 );
    }

    assert( ringbuffer_write( buffer, testString, 12 ) == 0, "This write should have failed, buffer is incapable of holding the data" );

    printf( "Length: %llu\n", ringbuffer_length(buffer) );
    assert(ringbuffer_length(buffer) == 120, "Buffer should have 120B of data at this point." );

    for( int i=0; i<20; i++ ) {
        char out[7] = { '-', 0 };
        ssize_t read = ringbuffer_read( buffer, out, 6 );
        assert( read == 6, "Ring refused to supply a full buffer" );
    }
    assert(ringbuffer_length(buffer) == 0, "Buffer should be empty at this point." );

    // Read/Write integrity
    char * longInputTestString = "This booklet has been produced for students at the University of New South Wales";
    assert( ringbuffer_write( buffer, longInputTestString, strlen(longInputTestString) ) == strlen(longInputTestString), "Ring refused to write new data" );

    for( int i=0; i<strlen(longInputTestString); i++ ) {
        char tmp = 0;
        assert( ringbuffer_read( buffer, &tmp, 1 ) == 1, "Could not pull 1 byte from the ring buffer" );
        assertEqual( tmp, longInputTestString[i] );
    }

    char overReadBuffer[16] = { 0 };
    assert( ringbuffer_read( buffer, overReadBuffer, 16 ) == 0, "Should not have been able to read any data from the buffer!" );

    assert( ringbuffer_destroy( buffer ), "Was unable to destroy the ring buffer" );
    return;
}

void debugState( char * file, int line ) {
    //
    printf( "State debug\n" );
}

void test_network_sync() {
    RingBuffer_t * rx_buffer = ringbuffer_init( 512 );

    for( int i=0; i<2000; i++ ) {
        unsigned char packet[sizeof( gnw_header_t ) + 10] = { 0 };
        gnw_header_t * packer_header = (gnw_header_t *)packet;
        packer_header->magic = GNW_MAGIC;
        packer_header->length = 10;
        packer_header->type = i;
        packer_header->version = GNW_VERSION;
        *(char *)(packet + sizeof(gnw_header_t)) = (char)(0x01 << (i%8));

        ringbuffer_write( rx_buffer, packet, sizeof( gnw_header_t ) + 10 );

        // Try and find a header
        gnw_header_t header;
        if( gnw_nextHeader( rx_buffer, &header ) ) {
            // Will this packet physically fit in the remaining buffer?
            if( header.length <= ringbuffer_length( rx_buffer ) ) {

                // Use a static array for a fast internal packet buffer
                unsigned char latch_buffer[ 1024 ];
                memset( &latch_buffer, 0, 1024 );
                assert( ringbuffer_read( rx_buffer, &latch_buffer, header.length ) == header.length, "Couldn't pull the payload from the ring buffer" );

                printf( "%02x -> %02x ? %02x -> %d\n", header.type, latch_buffer[0], 0x01 << (header.type % 8), (unsigned)latch_buffer[0] == (unsigned)0x01 << (header.type % 8) );

                assert( 0x01 << (header.type % 8) == latch_buffer[0], "Payload did not match the expected value" );
            }
        }
    }

    ringbuffer_destroy( rx_buffer );
}

int main(int argc, char * argv ) {

    setReportAssert( false );
    setExitOnAssert( true );

    //test_ring_buffer();

    test_network_sync();

    printf( "Done. Bye\n" );

    return EXIT_SUCCESS;
}