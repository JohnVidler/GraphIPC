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
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "GraphNetwork.h"
#include "RingBuffer.h"
#include "packet.h"

volatile gnw_stats_t link_stats;

/**
 * Puts a formatted graph address in the character buffer supplied.
 *
 * Buffer must have at least 24 characters remaining, no length checks are made.
 *
 * @param buffer The buffer to write in to
 * @param address The 64-bit address to write
 */
void gnw_format_address( char * buffer, uint64_t address ) {
    uint8_t * tmp = (uint8_t *) &address;
    for( int i=7; i>-1; i-- ) {
        buffer = buffer + sprintf( buffer, "%02x", *(tmp+i) );
        if( i > 0 )
            buffer = buffer + sprintf( buffer, ":" );
    }
}

/**
 * Dump a formatted packet to the file descriptor, identifying known fields.
 *
 * @param fd Which file descriptor to write to
 * @param buffer A pointer to the first byte of the header to a packet
 * @param length The length of the buffer to print.
 */
void gnw_dumpPacket( FILE * fd, unsigned char * buffer, ssize_t length ) {
    gnw_header_t header = { 0 };

    unsigned char * ptr = buffer;
    ptr = packet_read_u8( ptr, &header.magic );
    ptr = packet_read_u8( ptr, &header.version );
    ptr = packet_read_u8( ptr, &header.type );
    ptr = packet_read_u32( ptr, &header.source );
    ptr = packet_read_u32( ptr, &header.length );

    // Optionally use the header length
    if( length == -1 )
        length = header.length + sizeof(gnw_header_t);

    unsigned char * payload = buffer + sizeof( gnw_header_t );
    ssize_t payload_length = length - sizeof( gnw_header_t );

    fprintf( fd, "Has correct magic? %s\n", ( header.magic == GNW_MAGIC ? "Yes" : "No" ) );
    fprintf( fd, "Version:\t%x\n", header.version );
    fprintf( fd, "Type:\t%x\t(", header.type );
    switch( header.type ) {
        case GNW_COMMAND: fprintf( fd, "COMMAND" ); break;
        case GNW_DATA:    fprintf( fd, "DATA" );    break;
        case GNW_INVALID: fprintf( fd, "INVALID" ); break;
        default:
            fprintf( fd, "???" );
    }
    fprintf( fd, ")\n" );
    fprintf( fd, "Length:\t%dB\n", header.length );

    for( int i=0; i<payload_length; i++ ) {
        fprintf( fd, "%02x ", *(payload+i) );
        if( i % 8 == 0 && i != 0 )
            fprintf( fd, "\n" );
    }
}

void gnw_emitPacket( int fd, unsigned char * buffer, size_t length ) {
    ssize_t written = write( fd, buffer, length );
    link_stats.bytesWritten += written;

    printf( "\n>>> [ " );
    for( size_t i = 0; i < length; i++ )
        printf( "%02x ", buffer[i] );
    printf( "]\n" );
}

void gnw_emitDataPacket( int fd, gnw_address_t source, unsigned char * buffer, ssize_t length ) {
    link_stats.dataPackets++;

    uint8_t * packet = (uint8_t *)malloc( length + sizeof(gnw_address_t) );
    uint8_t * ptr = packet;

    ptr = packet_write_u8( ptr, GNW_MAGIC );
    ptr = packet_write_u8( ptr, GNW_VERSION );
    ptr = packet_write_u8( ptr, GNW_DATA );
    ptr = packet_write_u32( ptr, source );
    ptr = packet_write_u32( ptr, length );

    if( buffer != NULL ) {
        memcpy( ptr, buffer, length );
        ptr += length;
    }

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

/* Note: This is messy, why do I have two packet types, there should only be one, with a shared type-space!
         This may be because of the initial address-less connection, but surely this can be an exception to
         the rule that we're never address zero, have it reserved for the negotiating phase only...? */
__attribute__((deprecated))
void gnw_emitCommandPacket( int fd, uint8_t type, unsigned char * buffer, ssize_t length ) {
    link_stats.commandPackets++;

    uint8_t * packet = (uint8_t *)malloc( length + sizeof(gnw_header_t) );
    uint8_t * ptr = packet;

    ptr = packet_write_u8( ptr, GNW_MAGIC );
    ptr = packet_write_u8( ptr, GNW_VERSION );
    ptr = packet_write_u8( ptr, type );
    ptr = packet_write_u32( ptr, 0xFFFFFFFF ); // Fudge, so all packets are a minimum of 9 bytes.
    ptr = packet_write_u32( ptr, length );

    if( buffer != NULL ) {
        memcpy( ptr, buffer, length );
        ptr += length;
    }

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

void gnw_sendCommand( int fd, uint8_t command ) {
    unsigned char buffer[1] = { command };
    gnw_emitCommandPacket( fd, GNW_COMMAND, buffer, 1 );
}


void gnw_request_connect( int fd, gnw_address_t _source, gnw_address_t _target ) {
    // Connect to a process automatically :)
    unsigned char cbuffer[1 + (sizeof(gnw_address_t)*2) ];

    unsigned char * ptr = cbuffer;
    ptr = packet_write_u8( ptr, GNW_CMD_CONNECT );
    ptr = packet_write_u32( ptr, _source );
    ptr = packet_write_u32( ptr, _target );

    gnw_emitCommandPacket( fd, GNW_COMMAND, cbuffer, 1+ (sizeof(gnw_address_t)*2) );
}


#define GNW_PARSE_SYNC   0
#define GNW_PARSE_BUFFER 1

bool gnw_nextPacket( RingBuffer_t * buffer, gnw_state_t * context, void * packetBuffer ) {
    unsigned char discard = 0;

    switch( context->state ) {
        case GNW_PARSE_SYNC:
            while( ringbuffer_length(buffer) > 0 && ringbuffer_peek( buffer, 0 ) != GNW_MAGIC )
                ringbuffer_read(buffer, &discard, 1);

            if( ringbuffer_peek(buffer, 0) != GNW_MAGIC )
                break;

            // Fall through if we already have a syncronised packet frame, otherwise we end up 1-frame behind the stream!
            // This is mostly to prevent poll() from blocking if/when we return and the buffer is sync'd but hasn't
            // actually read the packet yet, this way, it can do both at once!
            context->state = GNW_PARSE_BUFFER;

        case GNW_PARSE_BUFFER:
            if( ringbuffer_peek( buffer, 0 ) != GNW_MAGIC ) {
                fprintf( stderr, "Missing packet framing, skipping!\n" );
                ringbuffer_read( buffer, &discard, 1 ); // Shift the ring by 1, hopefully becoming un-stuck
                context->state = GNW_PARSE_SYNC;
                break;
            }

            // Magic must be correct here...
            if( ringbuffer_peek( buffer, 1 ) != GNW_VERSION ) {
                fprintf( stderr, "GNW Version mismatch! Old software?\n" );
                ringbuffer_read( buffer, &discard, 1 ); // Shift the ring by 1, hopefully becoming un-stuck
                context->state = GNW_PARSE_SYNC;
                break;
            }

            // Magic & Version must be correct here, is there enough buffer left over?
            size_t packet_length = 9 + ringbuffer_peek32( buffer, 7 );
            if( ringbuffer_length( buffer ) >= packet_length ) {
                // The buffer has enough data to actually read this packet, so pull the packet and return true
                if( ringbuffer_read( buffer, packetBuffer, packet_length ) != packet_length ) {
                    fprintf( stderr, "Err! Could not pull the requested length data (%lu B) from the ring buffer, even though it had enough recorded!\n", packet_length );
                    break;
                }

                printf( "\n<<< [ " );
                for( size_t i = 0; i < packet_length; i++ )
                    printf( "%02x ", ((unsigned char *)packetBuffer)[i] );
                printf( "]\n" );

                // Packet copied to the client buffer
                return true;
            }

            // If we reach here, we have a valid packet, but its still being reconstructured, leave it be for now.
            break;

        default:
            fprintf( stderr, "ERR: GNW packet parser was in an unknown state, reset to SYNC state\n" );
            context->state = GNW_PARSE_SYNC;
    }

    return false;
}