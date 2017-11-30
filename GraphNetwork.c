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
#include <poll.h>
#include "GraphNetwork.h"

volatile gnw_stats_t link_stats;

void gnw_format_address( char * buffer, uint64_t address ) {
    uint8_t * tmp = (uint8_t *) &address;
    for( int i=7; i>-1; i-- ) {
        buffer = buffer + sprintf( buffer, "%02x", *(tmp+i) );
        if( i > 0 )
            buffer = buffer + sprintf( buffer, ":" );
    }
}

void gnw_emitPacket( int fd, char * buffer, ssize_t length ) {
    ssize_t written = write( fd, buffer, length );
    link_stats.bytesWritten += written;
}

void gnw_emitDataPacket( int fd, char * buffer, ssize_t length ) {
    link_stats.dataPackets++;
    char * packet = (char *)malloc( length + sizeof(gnw_header_t) );
    ((gnw_header_t *)packet)->flags    = 0;
    ((gnw_header_t *)packet)->reserved = 0;
    ((gnw_header_t *)packet)->version  = GNW_VERSION;
    ((gnw_header_t *)packet)->type     = GNW_DATA;

    if( buffer != NULL )
        memcpy( packet + sizeof(gnw_header_t), buffer, (size_t) length);

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

void gnw_emitCommandPacket( int fd, uint8_t type, char * buffer, ssize_t length ) {
    link_stats.commandPackets++;
    char * packet = (char *)malloc( length + sizeof(gnw_header_t) );
    ((gnw_header_t *)packet)->flags    = 0;
    ((gnw_header_t *)packet)->reserved = 0;
    ((gnw_header_t *)packet)->version  = GNW_VERSION;
    ((gnw_header_t *)packet)->type     = type;

    if( buffer != NULL )
        memcpy( packet + sizeof(gnw_header_t), buffer, (size_t) length);

    gnw_emitPacket( fd, packet, length + sizeof( gnw_header_t ) );

    free( packet );
}

void gnw_sendCommand( int fd, uint8_t command ) {
    char buffer[1] = { command };
    gnw_emitCommandPacket( fd, GNW_COMMAND, &buffer, 1 );
}

ssize_t gnw_wait( int fd, uint8_t type, char * buffer, ssize_t maxLen ) {
    struct pollfd watch_fd[1];
    memset( watch_fd, 0, sizeof( struct pollfd ) * 1 );

    watch_fd[0].fd = fd;
    watch_fd[0].events = POLLIN;

    int timeout = 10;
    ssize_t bytes = 0;
    while( bytes < 1 && timeout-- > 0 ) {
        printf( "Waiting for %d...\n", type );
        int rv = poll( watch_fd, 1, 1000 );

        // Wait error, drop back to callee
        if( rv == -1 )
            return -1;

        // Timeout :(
        if( rv == 0 ) {
            printf( "Timeout :(\n" );
            continue;
        }

        bytes = read(fd, buffer, maxLen);

        gnw_header_t *header = (gnw_header_t *) buffer;
        if (header->type == type || type == GNW_INVALID) {
            printf("Got %d!\n", header->type);
            return bytes;
        }
    }

    fprintf( stderr, "Err: Timeout waiting for command (%d)\n", type );
    return -1;
}