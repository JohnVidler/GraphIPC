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
#include "lib/Assert.h"
#include "lib/avl.h"
#include "lib/GraphNetwork.h"
#include "lib/packet.h"
#include "lib/RingBuffer.h"
#include "lib/utility.h"
#include "Log.h"
#include <arpa/inet.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "lib/klib/khash.h"
#include "lib/klib/kvec.h"

void test_linear_buffer() {
    uint8_t staticBuffer[64] = { 0 };
    uint8_t * ptr = staticBuffer;

    log_debug( "Writing to buffer...\n" );
    for( uint8_t i=0; i<32; i++ )
        ptr = packet_write_u8( ptr, i % 25 );
    
    log_debug( "Reading back from buffer...\n" );
    uint8_t out = 0;
    ptr = staticBuffer;
    for( uint8_t i=0; i<32; i++ ) {
        ptr = packet_read_u8( ptr, &out );
        assertEqual( out, i % 25 );
    }


    log_debug( "Resetting buffer...\n" );
    ptr = staticBuffer;
    for( uint8_t i=0; i<32; i++ )
        ptr = packet_write_u8( ptr, i % 25 );
    
    log_debug( "Reading back via shift...\n" );
    for( uint8_t i=0; i<32; i++ ) {
        packet_shift( staticBuffer, 64, &out, 1 );
        assertEqual( out, i % 25 );
    }

    return;
}

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

    log_debug( "Length: %lu\n", ringbuffer_length(buffer) );
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
        assertEqual( ringbuffer_peek( buffer, 0 ), longInputTestString[i] );
        assert( ringbuffer_read( buffer, &tmp, 1 ) == 1, "Could not pull 1 byte from the ring buffer" );
        assertEqual( tmp, longInputTestString[i] );
    }

    char overReadBuffer[16] = { 0 };
    assert( ringbuffer_read( buffer, overReadBuffer, 16 ) == 0, "Should not have been able to read any data from the buffer!" );

    assert( ringbuffer_destroy( buffer ), "Was unable to destroy the ring buffer" );
    return;
}

void dump_buffer( uint8_t * buffer, size_t length ) {
    for( size_t i=0; i<length; i++ )
        printf( "%02x", buffer[i] );
    printf( "\n" );
}

void test_network_sync() {
    uint8_t rx_buffer[512] = { 0 };
    uint8_t * rx_buffer_tail = rx_buffer;

    uint32_t next = 0;
    uint32_t expect = 0;

    for( int i=0; i<10; i++ ) {
        for( int b=0; b<3; b++ ) {
            rx_buffer_tail = packet_write_u8( rx_buffer_tail, GNW_MAGIC );
            rx_buffer_tail = packet_write_u8( rx_buffer_tail, GNW_VERSION );
            rx_buffer_tail = packet_write_u8( rx_buffer_tail, 0xBB );
            rx_buffer_tail = packet_write_u32( rx_buffer_tail, next++ );
            rx_buffer_tail = packet_write_u32( rx_buffer_tail, 0 );
        }
        //dump_buffer( rx_buffer, rx_buffer_tail - rx_buffer );

        ssize_t readyBytes = 0;
        while( (readyBytes = gnw_nextPacket( rx_buffer, rx_buffer_tail - rx_buffer )) != 0 ) {
            log_debug( "Bytes = %ld\n", readyBytes );

            // Are we in some invalid buffer state?
            if( readyBytes < 0 ) {
                log_warn( "Network desync, attempting to resync... (Err = %d)", readyBytes );
                // Dump a byte, try to clear the buffer and re-try.
                packet_shift( rx_buffer, 512, NULL, 1 );
                rx_buffer_tail--;
                //dump_buffer( rx_buffer, rx_buffer_tail - rx_buffer );
                continue;
            }

            gnw_header_t header = { 0 };
            
            uint8_t * ptr = rx_buffer;
            ptr = packet_read_u8( ptr, &header.magic );
            ptr = packet_read_u8( ptr, &header.version );
            ptr = packet_read_u8( ptr, &header.type );
            ptr = packet_read_u32( ptr, &header.source );
            ptr = packet_read_u32( ptr, &header.length );

            assertEqual( header.magic, GNW_MAGIC );
            assertEqual( header.version, GNW_VERSION );
            assertEqual( header.type, 0xBB );
            assertEqual( header.length, 0 );

            assertEqual( expect, header.source );
            //log_debug( "Addr: %ld\n", header.source );
            
            expect++;
            packet_shift( rx_buffer, 512, NULL, readyBytes );
            rx_buffer_tail -= readyBytes;

            log_debug( "Packet OK\n" );
            //dump_buffer( rx_buffer, rx_buffer_tail - rx_buffer );
        }

        
    }
}

void test_utility_functions() {

    uint64_t test_size = 1;
    for( uint64_t i=0; i<10; i++ ) {
        test_size = test_size * 100;

        log_debug( "%lu B =>", test_size );

        char * iec_suffix = "?";
        double iec_formatted_size = fmt_iec_size( test_size, &iec_suffix );
        log_debug( "\t%.2f %s =>", iec_formatted_size, iec_suffix );

        char * si_suffix = "?";
        double si_formatted_size = fmt_si_size( test_size, &si_suffix );
        log_debug( "\t%.2f %s\n", si_formatted_size, si_suffix );
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

KHASH_MAP_INIT_INT( uint32_t, uint32_t );
void test_hash_table() {
    khash_t(uint32_t) * table = kh_init(uint32_t);

    for( uint32_t i=0; i<1000; i++ ) {
        int state;
        khint_t k = kh_put( uint32_t, table, i, &state );
        kh_value( table, k ) = i * 2;
    }

    for( uint32_t i=0; i<1000; i++ ) {
        khint_t k = kh_get( uint32_t, table, i );
        assert( k != kh_end(table), "Could not read from index in table, missing entries?" );
        assert( kh_value( table, k ) == i*2, "Value mismatch in hash table" );
    }

    khint_t hint = kh_get( uint32_t, table, 30 );
    assert( kh_value( table, hint ) == 60, "Value lookup incorrect" );

    kh_destroy( uint32_t, table );
    table = kh_init(uint32_t);

    assert( kh_size(table) == 0, "Table was not cleared after reset loop!" );

    for( int i=0; i<1000; i++ ) {
        int state;
        khint_t k = kh_put( uint32_t, table, i, &state );
        assert( kh_size( table ) == 1, "Non-one table size after insert" );
        kh_value( table, k ) = (uint32_t)rand();
        kh_del( uint32_t, table, k );
        assert( kh_size( table ) == 0, "Non-zero table size after insert/remove cycle." );
    }

    kh_destroy( uint32_t, table );
}

void test_vector() {
    kvec_t( uint32_t ) vector;
    kv_init( vector );

    for( uint32_t i=0; i<1000; i++ ) {
        kv_push( uint32_t, vector, (i * 3) ^ 0xE5E5 );
        assert( kv_A( vector, i ) == ((i*3) ^ 0xE5E5), "Incorrect read! Data mismatch!");
    }

    assert( kv_size( vector ) == 1000, "Vector length mismatch" );

    for( uint32_t i=0; i<1000; i++ ) {
        kv_pop( vector );
    }

    assert( kv_size( vector ) == 0, "Vector length mismatch" );
    
    kv_destroy( vector );
}

typedef struct {
    uint32_t temp;
    uint8_t * ptr;
} test_t;

KHASH_MAP_INIT_INT( int, test_t );
void test_complex_structures() {
    khash_t( int ) * table = kh_init( int );

    int status;
    khint_t hint = kh_put( int, table, 4, &status );
    
    test_t * value = &kh_value( table, hint );
    assert( value != NULL, "Value from hashtable was null after a put operation!" );

    value->temp = 10;
    assert( value->temp == 10, "Direct write failed?" );

    value->ptr = malloc( 10 );
    assert( value->ptr != NULL, "Could not allocate 10 bytes for ptr test" );
    memset( value->ptr, 5, 10 );
    assert( value->ptr[2] == 5, "Memset values did not take!" );

    assert( kh_size(table) == 1, "Invalid number of elements in the table" );

    khint_t tmp = kh_get( int, table, 4 );
    test_t * farRef = &kh_value( table, tmp );
    assert( farRef != NULL, "Second lookup returned NULL" );

    assert( farRef == value, "Second lookup was not the same pointer as the first!" );
    assert( farRef->temp == value->temp, "Direct read/write values did not match!" );
    assert( farRef->ptr == value->ptr, "Structure pointers did not match" );
    assert( farRef->ptr[2] == 5, "Memset values in second lookup did not match set values" );

    free( farRef->ptr );
    farRef->ptr = NULL;

    khint_t delhint = kh_get( int, table, 4 );
    kh_del( int, table, delhint );

    assert( !kh_exist( table, 4 ), "After deletion, bucket still held data!" );

    assert( kh_size(table) == 0, "Table size was non-zero after delete" );

    khint_t k;
    for (k = kh_begin(table); k != kh_end(table); ++k) {
        if (kh_exist(table, k)) {
            int index = kh_key( table, k );
			test_t * val = &kh_value(table, k);

            printf( "%d -> %d, %p\n", index, val->temp, val->ptr );
        }
    }

    //assert( kh_size(table) == 0, "Table with no keys has a non-zero size!" );

    khint_t test = kh_get( int, table, 4 );
    printf( ">>> %d\n", test );

    kh_destroy( int, table );
}

int main(int argc, char ** argv ) {
    setReportAssert( false );
    setExitOnAssert( true );

    log_setLevel( INFO );
    log_info( "Just testing the log output - INFO" );
    log_warn( "Just testing the log output - WARNING" );
    log_error( "Just testing the log output - ERROR" );
    log_write( 2, "Log level 2" );
    log_write( 3, "Log level 3" );
    log_write( 4, "Log level 4" );

    // Buffer Tests
    log_info( "Running buffer tests..." );
    log_info( "  Linear Buffer..." );
    test_linear_buffer();

    log_info( "  Ring Buffer..." );
    test_ring_buffer();

    // Internals Tests
    log_info( "Testing Network Functions..." );
    test_network_sync();

    log_info( "Testing Utility Functions..." );
    test_utility_functions();

    // KLIB Tests
    log_info( "Running klib tests..." );

    log_info( "  Hash Table..." );
    test_hash_table();

    log_info( "  Vector..." );
    test_vector();

    log_info( "  Table with Complex Structures..." );
    test_complex_structures();

    log_info( "\nAll tests complete successfully.\n" );

    return EXIT_SUCCESS;
}