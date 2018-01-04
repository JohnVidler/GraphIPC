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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include "utility.h"

uint32_t strlen_array( unsigned int offset, unsigned int length, char ** array ) {
    uint32_t count = 0;

    for( unsigned int i = offset; i<offset+length; i++ )
        count += strlen( array[i] );

    return count;
}

char * const fmt_sizeStr[6] = { "B", "KB", "MB", "GB", "TB", "PB" };
unsigned long long fmt_humanSize( unsigned long long size, char * unitRef ) {
    unsigned long long mul = 0;
    while( size > 1024 ) {
        size = size / 1024;
        //printf( "Size: %llu %s\n", size, fmt_sizeStr[mul] );
        unitRef = fmt_sizeStr[mul];
        mul++;
    }
    return size;
}

/**
 * This <strong>should</strong> get the MTU of the supplied interface, but it doesn't work.
 * Can't find a good source on how ioctl calls like this are supposed to work, which makes
 * this pretty much impossible.
 *
 * @param interface The interface to query
 * @return The MTU of the interface supplied
 */
int getIFaceMTU( const char * interface ) {
    int dev = open( "/proc/net/dev", O_RDONLY );

    struct ifreq query;
    memcpy( query.ifr_name, interface, strlen(interface) );

    if( ioctl( dev, SIOCGIFMTU, &query ) == 0 ) {
        close( dev );
        return query.ifr_mtu;
    }
    close( dev );
    return -1;
}