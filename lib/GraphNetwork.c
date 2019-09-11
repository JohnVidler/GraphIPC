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
#include "../Log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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
}

void gnw_emitDataPacket( int fd, gnw_address_t source, unsigned char * buffer, ssize_t length ) {
    log_debug( "Data Packet" );
    link_stats.dataPackets++;

    uint8_t * packet = (uint8_t *)malloc( length + 11 );
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

    gnw_emitPacket( fd, packet, length + 11 );

    free( packet );
}

/* Note: This is messy, why do I have two packet types, there should only be one, with a shared type-space!
         This may be because of the initial address-less connection, but surely this can be an exception to
         the rule that we're never address zero, have it reserved for the negotiating phase only...? */
__attribute__((deprecated))
void gnw_emitCommandPacket( int fd, uint8_t type, unsigned char * buffer, ssize_t length ) {
    log_debug( "Command Packet" );
    link_stats.commandPackets++;

    uint8_t * packet = (uint8_t *)malloc( length + 11 );
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

    gnw_emitPacket( fd, packet, length + 11 );

    free( packet );
}

void gnw_sendCommand( int fd, uint8_t command ) {
    unsigned char buffer[1] = { command };
    gnw_emitCommandPacket( fd, GNW_COMMAND, buffer, 1 );
}


void gnw_request_connect( int fd, gnw_address_t _source, gnw_address_t _target ) {
    log_debug( "Connect Packet" );
    // Connect to a process automatically :)
    unsigned char cbuffer[9] = { 0 };

    unsigned char * ptr = cbuffer;
    ptr = packet_write_u8( ptr, GNW_CMD_CONNECT );
    ptr = packet_write_u32( ptr, _source );
    ptr = packet_write_u32( ptr, _target );

    gnw_emitCommandPacket( fd, GNW_COMMAND, cbuffer, 9 );
}


#define GNW_PARSE_SYNC   0
#define GNW_PARSE_BUFFER 1

ssize_t gnw_nextPacket( uint8_t * buffer, size_t buffer_length ) {

    // Is there enough data for a whole valid packet?
    if( buffer_length < 11 )
        return 0;

    // Reject a parse if there is no magic byte up front.
    if( buffer[0] != GNW_MAGIC ) {

        // Debug: print the first few bytes of the buffer.
        printf( "Buffer = [" );
        for(int i=0; i<128; i++) {
            if( i == buffer_length )
                printf( "%02x$", buffer[i] );
            else
                printf( "%02x ", buffer[i] );
        }
        printf( "]\n" );

        return -1;
    }
    
    // Actually parse stuff
    gnw_header_t header = { 0 };
    uint8_t * ptr = buffer;

    ptr = packet_read_u8( ptr, &header.magic );
    ptr = packet_read_u8( ptr, &header.version );
    ptr = packet_read_u8( ptr, &header.type );
    ptr = packet_read_u32( ptr, &header.source );
    ptr = packet_read_u32( ptr, &header.length );

    // Reject if the packet has a bad Version
    if( header.version != GNW_VERSION ) {
        log_error( "Version mismatch! Old/Incorrect router running? (ours = %d, remote = %d)\n", GNW_VERSION, header.version );
        return -2;
    }

    if( 11 + header.length <= buffer_length )
        return 11 + header.length;
    
    return 0;
}