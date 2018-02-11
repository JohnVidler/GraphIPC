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
#include "Log.h"

#include <stdarg.h>

unsigned int minLogLevel = WARNING;

void log_setLevel( unsigned int level ) {
    minLogLevel = level;
}

void log_vwrite( unsigned int level, const char * fmt, va_list args ) {
    if( level < minLogLevel )
        return;

    FILE * stream = stdout;
    if( level >= ERROR ){
        stream = stderr;
        fprintf( stream, ANSI_COLOR_RED "[ERROR]\t" );
    } else if( level >= WARNING ) {
        fprintf( stream, ANSI_COLOR_YELLOW "[WARN ]\t" );
    } else if(level >= INFO) {
        fprintf( stream, ANSI_COLOR_RESET "[INFO ]\t" );
    } else {
        fprintf( stream, ANSI_COLOR_RESET "[%05u]\t", level );
    }
    vfprintf( stream, fmt, args );
    fprintf( stream, "\n" ANSI_COLOR_RESET );
    fflush( stream );
}

void log_write( unsigned int level, const char * fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    log_vwrite( level, fmt, args );
    va_end( args );
}

void log_info( const char * fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    log_vwrite( INFO, fmt, args );
    va_end( args );
}

void log_warn( const char * fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    log_vwrite( WARNING, fmt, args );
    va_end( args );
}

void log_error( const char * fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    log_vwrite( ERROR, fmt, args );
    va_end( args );
}