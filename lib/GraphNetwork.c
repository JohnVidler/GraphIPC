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

volatile gnw_stats_t link_stats;

void gnw_format_address( char * buffer, uint64_t address ) {
    uint8_t * tmp = (uint8_t *) &address;
    for( int i=7; i>-1; i-- ) {
        buffer = buffer + sprintf( buffer, "%02x", *(tmp+i) );
        if( i > 0 )
            buffer = buffer + sprintf( buffer, ":" );
    }
}

void gnw_dumpPacket( FILE * fd, char * buffer, ssize_t length ) {
    gnw_header_t * header = (gnw_header_t *)buffer;
    char * payload = buffer + sizeof( gnw_header_t );
    ssize_t payload_length = length - sizeof( gnw_header_t );

    fprintf( fd, "Version:\t%x\n", header->version );
    fprintf( fd, "Type:\t%x\t(" );
    switch( header->type ) {
        case GNW_COMMAND: fprintf( fd, "COMMAND" ); break;
        case GNW_DATA:    fprintf( fd, "DATA" );    break;
        case GNW_ACK:     fprintf( fd, "ACK" );     break;
        case GNW_NACK:    fprintf( fd, "NACK" );    break;
        case GNW_INVALID: fprintf( fd, "INVALID" ); break;
        default:
            fprintf( fd, "???" );
    }
    fprintf( fd, ")\n" );

    for( int i=0; i<payload_length; i++ ) {
        fprintf( fd, "%02x ", payload[i] );
        if( i % 8 == 0 )
            fprintf( fd, "\n" );
    }
}

void gnw_emitPacket( int fd, char * buffer, ssize_t length ) {
    ssize_t written = write( fd, buffer, length );
    link_stats.bytesWritten += written;
}

void gnw_emitDataPacket( int fd, char * buffer, ssize_t length ) {
    link_stats.dataPackets++;
    char * packet = (char *)malloc( length + sizeof(gnw_header_t) );
    ((gnw_header_t *)packet)->magic    = GNW_MAGIC;
    ((gnw_header_t *)packet)->version  = GNW_VERSION;
    ((gnw_header_t *)packet)->type     = GNW_DATA;
    ((gnw_header_t *)packet)->length   = (uint16_t )length;

    if( buffer != NULL )
        memcpy( packet + sizeof(gnw_header_t), buffer, (size_t) length);

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

void gnw_emitCommandPacket( int fd, uint8_t type, char * buffer, ssize_t length ) {
    link_stats.commandPackets++;
    char * packet = (char *)malloc( length + sizeof(gnw_header_t) );
    ((gnw_header_t *)packet)->magic    = GNW_MAGIC;
    ((gnw_header_t *)packet)->version  = GNW_VERSION;
    ((gnw_header_t *)packet)->type     = type;
    ((gnw_header_t *)packet)->length   = (uint16_t)length;

    if( buffer != NULL )
        memcpy( packet + sizeof(gnw_header_t), buffer, (size_t) length);

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

void gnw_sendCommand( int fd, uint8_t command ) {
    char buffer[1] = { command };
    gnw_emitCommandPacket( fd, GNW_COMMAND, buffer, 1 );
}


bool gnw_nextHeader( RingBuffer_t * buffer, gnw_header_t * header ) {

    // Try to find a magic byte...
    uint8_t discard = 0;
    while (ringbuffer_length(buffer) > 0 && ringbuffer_peek(buffer, 0) != GNW_MAGIC) {
        ringbuffer_read(buffer, &discard, 1);
    }

    // Bail if we can't fit an entire header into the remaining buffer
    if (ringbuffer_length(buffer) < sizeof(gnw_header_t))
        return false;

    // Otherwise, check the header length field, check if its' sane
    uint16_t packet_length = (uint16_t) ((((ringbuffer_peek(buffer, 2 ) << 8) & 0xFF00) | (ringbuffer_peek(buffer, 3 ) & 0xFF)) & 0xFFFF);

    // If it fits, grab the header properly and return
    if( packet_length <= ringbuffer_length(buffer) ) {
        memset( header, 0, sizeof(gnw_header_t) );
        ringbuffer_read( buffer, header, sizeof(gnw_header_t) );

        return true;
    }

    if( ringbuffer_peek(buffer, 0) != GNW_MAGIC )
        return false;

    if(ringbuffer_length(buffer) < sizeof(gnw_header_t) )
        return false;

    memset( header, 0, sizeof(gnw_header_t) );
    ringbuffer_read( buffer, header, sizeof(gnw_header_t) );

    return true;
}