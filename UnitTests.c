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
#include <time.h>
#include "lib/Assert.h"
#include "lib/RingBuffer.h"
#include "lib/GraphNetwork.h"
#include "lib/utility.h"
#include "lib/avl.h"
#include "Log.h"
#include <arpa/inet.h>

void test_ring_buffer() {
    RingBuffer_t * buffer = ringbuffer_init( 128 );
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

void test_network_sync() {
    RingBuffer_t * rx_buffer = ringbuffer_init( 512 );

    unsigned int next = 0;
    gnw_state_t context = { .state=0 };

    for( int i=0; i<200000; i++ ) {
        unsigned char packet[sizeof(gnw_header_t) + 10] = {0};
        do {
            gnw_header_t *packer_header = (gnw_header_t *) packet;
            packer_header->magic = GNW_MAGIC;
            packer_header->length = 10;
            packer_header->type = next;
            packer_header->version = GNW_VERSION;
            *(char *) (packet + sizeof(gnw_header_t)) = (unsigned char) (next++);

        } while( ringbuffer_write(rx_buffer, packet, sizeof(gnw_header_t) + 10) == sizeof(gnw_header_t) + 10 );

        unsigned char inPacket[sizeof(gnw_header_t) + 30] = { 0 };

        // Try and read a packet!
        while( gnw_nextPacket( rx_buffer, &context, &inPacket ) ) {
            gnw_header_t * hdr = (gnw_header_t *)inPacket;
            assert( hdr->magic == GNW_MAGIC, "Bad packet magic" );
            assert( hdr->version == GNW_VERSION, "Bad packet version" );
            assert( hdr->length == 10, "Bad packet length" );
            assert( hdr->type == *(inPacket + sizeof(gnw_header_t)), "Type/Content mismatch" );
        }

    }

    ringbuffer_destroy( rx_buffer );
}

void test_utility_functions() {

    uint64_t test_size = 1;
    for( uint64_t i=0; i<10; i++ ) {
        test_size = test_size * 100;

        printf( "%lu B =>", test_size );

        char * iec_suffix = "?";
        double iec_formatted_size = fmt_iec_size( test_size, &iec_suffix );
        printf( "\t%.2f %s =>", iec_formatted_size, iec_suffix );

        char * si_suffix = "?";
        double si_formatted_size = fmt_si_size( test_size, &si_suffix );
        printf( "\t%.2f %s\n", si_formatted_size, si_suffix );
    }
}

void printBits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = (b[i] >> j) & 1;
            printf("%u", byte);
        }
    }
    puts("");
}

void printIndent( unsigned int depth, const char * indent ) {
    while( depth-- > 0 )
        printf( "%s", indent );
}

typedef struct dummy {
    uint32_t address;
} dummy_t;

int avl_index_comparitor ( const void *avl_a, const void *avl_b, void *avl_param ) {
    dummy_t * a = (dummy_t *)avl_a;
    dummy_t * b = (dummy_t *)avl_b;
    return a->address - b->address;
}

void test_btree() {
    struct avl_table * table = avl_create( &avl_index_comparitor, NULL, &avl_allocator_default );

    dummy_t *a = malloc(sizeof(dummy_t));
    for( int i=0; i<10; i++ ) {
        a->address = (uint32_t) i;
        avl_insert(table, a);
    }

    dummy_t query = { .address = 5 };
    dummy_t * result = avl_find( table, &query );

    assert( result == a, "Return data mismatch" );

}

int main(int argc, char * argv ) {
    setReportAssert( false );
    setExitOnAssert( true );

    log_setLevel( WARNING );
    log_info( "Just testing the log output - INFO" );
    log_warn( "Just testing the log output - WARNING" );
    log_error( "Just testing the log output - ERROR" );
    log_write( 2, "Log level 2" );
    log_write( 3, "Log level 3" );
    log_write( 4, "Log level 4" );

    //test_ring_buffer();
    test_btree();

    //test_network_sync();

    //test_utility_functions();

    return EXIT_SUCCESS;
}