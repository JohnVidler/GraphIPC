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
#include <netinet/in.h>
#include "utility.h"

uint32_t strlen_array( unsigned int offset, unsigned int length, char ** array ) {
    uint32_t count = 0;

    for( unsigned int i = offset; i<offset+length; i++ )
        count += strlen( array[i] );

    return count;
}

char * const fmt_iec_sizeStr[9] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
double fmt_iec_size(uint64_t _size, char ** unitRef) {
    double size = (double)_size;
    unsigned int mul = 0;
    while( size > 1023 && mul < 9 ) {
        size = size / 1024;
        mul++;
    }

    *unitRef = fmt_iec_sizeStr[mul];

    return size;
}

char * const fmt_si_sizeStr[9] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
double fmt_si_size(uint64_t _size, char ** unitRef) {
    double size = (double)_size;
    unsigned int mul = 0;
    while( size > 999 && mul < 9 ) {
        size = size / 1000;
        mul++;
    }

    *unitRef = fmt_si_sizeStr[mul];

    return size;
}