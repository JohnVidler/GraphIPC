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
#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <stdio.h>
#include <memory.h>
#include <unitypes.h>
#include <assert.h>
#include "RingBuffer.h"

void ringbuffer_print( RingBuffer_t * root ) {
    //printf( "Ring State\n" );
    /*printf( "\tStart:\t%llu\n", root->start );
    printf( "\tEnd:\t%llu\n", root->end - root->start );
    printf( "\tHead:\t%llu\n", root->head - root->start );
    printf( "\tTail:\t%llu\n", root->tail - root->start );
    printf( "\tLength:\t%llu\n", ringbuffer_length(root) );*/

    printf( "[" );
    char * tmp = root->start;
    while( tmp < (char *)root->end ) {
        if( *tmp < 32 || *tmp > 126 )
            printf( "_" );
        else
            printf( "%c", *tmp );
        tmp++;
    }
    printf( "]\n" );

    printf( "[" );
    tmp = root->start;
    while( tmp < (char *)root->end ) {
        if( tmp == root->head && tmp == root->tail )
            printf( "X" );
        else if( tmp == root->head )
            printf( "H" );
        else if( tmp == root->tail )
            printf( "T" );
        else
            printf( " " );
        tmp++;
    }
    printf( "]\n" );

    fflush( stdout );
}

ssize_t minValue( ssize_t a, ssize_t b ) {
    if( a < b )
        return a;
    return b;
}

RingBuffer_t * ringbuffer_init(size_t size)
{
    RingBuffer_t * root = (RingBuffer_t *)malloc( sizeof(RingBuffer_t) + size );
    void * ring = root + sizeof(RingBuffer_t);
    root->start = ring;
    root->end   = ring + size;
    root->head  = ring;
    root->tail  = ring;

    sem_init( &(root->lock), 0, 1 );

    return root;
}

bool ringbuffer_destroy( RingBuffer_t * root )
{
    sem_wait( &(root->lock) );
    if( root->head == root->tail ) {
        sem_destroy( &(root->lock) );
        free( root );
        return true;
    }

    sem_post( &(root->lock) );
    return false;
}

ssize_t ringbuffer_length( RingBuffer_t * root ) {

    if( root->head >= root->tail )
        return root->head - root->tail;
    return (root->head - root->start) + (root->end - root->tail);
}

ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, ssize_t maxLength )
{
    sem_wait( &(root->lock) );

    ssize_t readLength = minValue( ringbuffer_length( root ), maxLength );

    // Nothing to read (or zero length output buffer)
    if( readLength == 0 ) {
        sem_post( &(root->lock) );
        return 0;
    }

    // From herein, there is no need to check the read length, as the
    // minValue length lookup above prevents the buffer from being over-read.
    // -John

    if( root->tail < root->head ) {
        memcpy( buffer, root->tail, readLength );
        root->tail += readLength;
        sem_post( &(root->lock) );
        return readLength;
    }

    // Danger of wandering into NULL space, might need to make two copy ops :(
    ssize_t rightLength = root->end - root->tail;
    if( rightLength < readLength ) {
        memcpy(buffer, root->tail, rightLength); // Copy to the end of the first block
        memcpy(buffer + rightLength, root->start, readLength-rightLength); // Then fill out with the rest

        root->tail = root->start + readLength-rightLength;
    } else {
        // We can do this in one copy
        memcpy( buffer, root->tail, readLength );
        root->tail += readLength;
    }

    sem_post( &(root->lock) );
    return readLength;
}

uint8_t ringbuffer_peek( RingBuffer_t * root ) {
    sem_wait( &(root->lock) );
    uint8_t tmp = *(uint8_t *)(root->tail);
    sem_post( &(root->lock) );

    return tmp;
}

ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, size_t length ) {
    sem_wait( &(root->lock) );

    if( root->head < root->tail ) {
        // Danger of overwriting the tail
        size_t rightLen = root->head - root->tail;
        if( rightLen > length ) {
            sem_post( &(root->lock) );
            return 0; // No space left!
        }

        memcpy( root->head, buffer, rightLen );
        root->head = root->head + rightLen;

        sem_post( &(root->lock) );
        return length;
    }
    else
    {
        size_t rightLen = root->end - root->head;

        if( rightLen < length ) {
            size_t leftLen = root->tail - root->start;

            if( leftLen + rightLen < length ) {
                sem_post( &(root->lock) );
                return 0; // No space left!
            }

            memcpy( root->head, buffer, rightLen ); // Write the initial space
            memcpy( root->start, buffer+rightLen, length-rightLen ); // Write the remaining data
            root->head = root->start + (length - rightLen); // Update the head pointer

            sem_post( &(root->lock) );
            return length;
        }

        // If here, then there's enough space in the right-hand-side to fit the copy
        memcpy( root->head, buffer, length ); // Write buffer
        root->head += length;
    }

    sem_post( &(root->lock) );
    return length;
}