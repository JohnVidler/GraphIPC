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
#pragma once

#include <stdbool.h>
#include "RingBuffer.h"
//#include "RingBuffer.h"

#define GNW_VERSION  2

#define GNW_MAGIC 0x55

// Max 8-bit number! 0x00->0xFF
#define GNW_INVALID  0x0
#define GNW_DATA     0x10
#define GNW_COMMAND  0xF0

// OR'd for reply versions of the above
#define GNW_REPLY    0x01

// Used for wait commands that can accept any packet type
#define GNW_ANY      0

// Client states
#define GNW_STATE_OPEN    0x00
#define GNW_STATE_RUN     0x02
#define GNW_STATE_CLOSE   0xFE
#define GNW_STATE_ZOMBIE  0xFF

// Valid command operators
#define GNW_CMD_NEW_ADDRESS  1
#define GNW_CMD_STATUS       2
#define GNW_CMD_POLICY       3
#define GNW_CMD_CONNECT      4
#define GNW_CMD_DISCONNECT   5

// Link Constants
#define GNW_BROADCAST  0
#define GNW_ANYCAST    1
#define GNW_ROUNDROBIN 2
#define GNW_MERGE      3
#define GNW_COMBINE    4

#define GNW_MAX_LINKS  10

// Router configuration
#define ROUTER_BACKLOG 10
#define ROUTER_PORT    (const char *)("19000")

// The router itself is uid == 0, so no process can ever be this UID.
#define UID_INVALID 0

typedef uint64_t gnw_address_t;
typedef uint32_t UID_t;

typedef struct {
    uint64_t bytesWritten;
    uint64_t bytesRead;
    uint64_t dataPackets;
    uint64_t commandPackets;
} gnw_stats_t;

typedef struct {
    int state;
} gnw_state_t;

/** GNW Packet header structure */
typedef struct {
    uint8_t       magic;
    uint8_t       version;
    uint8_t       type;
    gnw_address_t source;
    uint32_t      length;
} gnw_header_t;

void gnw_format_address( unsigned char * buffer, uint64_t address );

void gnw_dumpPacket( FILE * fd, unsigned char * buffer, ssize_t length );

void gnw_emitPacket( int fd, unsigned char * buffer, size_t length );
void gnw_emitDataPacket( int fd, gnw_address_t source,  unsigned char * buffer, ssize_t length );
void gnw_emitCommandPacket( int fd, uint8_t type, unsigned char * buffer, ssize_t length );

void gnw_sendCommand( int fd, uint8_t command );

void gnw_request_connect( int fd, gnw_address_t _source, gnw_address_t _target );

bool gnw_nextPacket( RingBuffer_t * buffer, gnw_state_t * context, void * packetBuffer );