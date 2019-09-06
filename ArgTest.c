/*
 * GraphIPC - ArgTest
 * A simple test program to check argument passing and pipe connections
 *
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

#include <getopt.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <zconf.h>
#include <stdlib.h>
#include <stdbool.h>

bool _waitFor = false;
unsigned long _interval = 1000000;
unsigned long _messages = 10;

int main(int argc, char ** argv ) {

    int c;
    while( (c = getopt( argc, argv, "wi:c:" )) != -1 ) {
        switch( c ) {
            case 'i':
                _interval = strtoul( optarg, NULL, 10 );
                break;
            
            case 'c':
                _messages = strtoul( optarg, NULL, 10 );
                break;
            
            case 'w':
                _waitFor = true;
                break;

            default:
                fprintf( stderr, "Bad arguments, STOP.\n" );
                exit( 1 );
        }
    }

    time_t rawTime;
    struct tm * timeInfo;

    unsigned long cycles = 0;
    while( cycles++ < _messages ) {
        if( _waitFor )
            gets( stdin );

        time(&rawTime);
        timeInfo = localtime(&rawTime);

        printf( "%lu:\t%s", cycles, asctime(timeInfo) );
        fflush( stdout );
        usleep( _interval );
    }

    return 0;
}