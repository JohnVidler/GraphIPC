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
#include <string.h>

bool _waitFor = false;
unsigned long _dataLength = 0;
unsigned long _interval = 1000000;
unsigned long _messages = 10;

char * _delimiter = "\n";
char * _preBuffer = NULL;

int main(int argc, char ** argv ) {

    int c;
    while( (c = getopt( argc, argv, "wi:c:s:d:" )) != -1 ) {
        switch( c ) {
            case 's':
                _dataLength = strtoul( optarg, NULL, 10 );
                _preBuffer = (char *)malloc( _dataLength + 1 );
                memset( _preBuffer, 0, _dataLength + 1 );
                memset( _preBuffer, '?', _dataLength );
                break;
            
            case 'd':
                _delimiter = optarg;
                break;

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
        char input[4096] = { 0 };
        input[0] = '\0';

        if( _waitFor ) {
            fgets( input, 4096, stdin );
            input[strlen(input)-1] = '\0';
        }

        usleep( _interval );

        if( _dataLength > 0 ) {
            printf( "%s%s", _preBuffer, _delimiter );
        }
        else {
            time( &rawTime );
            timeInfo = localtime( &rawTime );

            char * timeString = asctime(timeInfo);
            timeString[24] = '\0';

            if( input[0] == '\0' )
                printf( "%lu\t%s%s", cycles, timeString, _delimiter );
            else
                printf( "%lu\t%s\t%s%s", cycles, timeString, input, _delimiter );
        }

        fflush( stdout );
    }

    return 0;
}