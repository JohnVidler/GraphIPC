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
#include <stdbool.h>

#pragma ONCE;

#define GNW_VERSION  1

#define GNW_INVALID  0
#define GNW_DATA     1
#define GNW_ACK      2
#define GNW_NACK     3
#define GNW_COMMAND  100

// Used for wait commands that can accept any packet type
#define GNW_ANY      0

// Client states
#define GNW_STATE_OPEN    0x00
#define GNW_STATE_SETUP   0x01
#define GNW_STATE_RUN     0x02
#define GNW_STATE_COMMAND 0x03
#define GNW_STATE_CLOSE   0xFE
#define GNW_STATE_ZOMBIE  0xFF

// Valid commands
#define GNW_CMD_NEW_ADDRESS  1

// Link Constants
#define GNW_BROADCAST  0
#define GNW_ANYCAST    1
#define GNW_ROUNDROBIN 2

#define GNW_MAX_LINKS  10

// Router configuration
#define ROUTER_BACKLOG 10
#define ROUTER_PORT    (const char *)("19000")
#define CONTROL_PORT   (const char *)("19001")

// The router itself is uid == 0, so no process can ever be this UID.
#define UID_INVALID 0

typedef uint32_t UID_t;

typedef struct {
    uint64_t bytesWritten;
    uint64_t bytesRead;
    uint64_t dataPackets;
    uint64_t commandPackets;
} gnw_stats_t;

typedef struct {
    unsigned int version  : 4;
    unsigned int flags    : 4;
    unsigned int type     : 8;
    unsigned int reserved : 16;   // Probably will become a sequence number, but reserved for now
} __attribute__((packed, aligned(8))) gnw_header_t;


void gnw_format_address( char * buffer, uint64_t address );

void gnw_dumpPacket( FILE * fd, char * buffer, ssize_t length );

void gnw_emitPacket( int fd, char * buffer, ssize_t length );
void gnw_emitDataPacket( int fd, char * buffer, ssize_t length );
void gnw_emitCommandPacket( int fd, uint8_t type, char * buffer, ssize_t length );

void gnw_sendCommand( int fd, uint8_t command );

ssize_t gnw_wait( int fd, uint8_t type, char * buffer, ssize_t maxLen );