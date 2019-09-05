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
#include "Assert.h"
#include "RingBuffer.h"

__attribute__((deprecated))
void ringbuffer_print( RingBuffer_t * root ) {
    //printf( "Ring State\n" );
    /*printf( "\tStart:\t%llu\n", root->start );
    printf( "\tEnd:\t%llu\n", root->end - root->start );
    printf( "\tHead:\t%llu\n", root->head - root->start );
    printf( "\tTail:\t%llu\n", root->tail - root->start );
    printf( "\tLength:\t%llu\n", ringbuffer_capacity(root) );*/

    printf( "[" );
    unsigned char * tmp = root->start;
    while( tmp < (unsigned char *)root->end ) {
        if( *tmp < 32 || *tmp > 126 )
            printf( "_" );
        else
            printf( "%c", *tmp );
        tmp++;
    }
    printf( "]\n" );

    printf( "[" );
    tmp = root->start;
    while( tmp < (unsigned char *)root->end ) {
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

__attribute__((deprecated))
ssize_t minValue( size_t a, size_t b ) {
    if( a < b )
        return a;
    return b;
}

__attribute__((deprecated))
RingBuffer_t * ringbuffer_init(size_t size)
{
    RingBuffer_t * root = (RingBuffer_t *)malloc( sizeof(RingBuffer_t) + size );
    void * ring = (void *)root + sizeof(RingBuffer_t);
    root->start = ring;
    root->end   = ring + size;
    root->head  = ring;
    root->tail  = ring;

    root->capacity = size;

    sem_init( &(root->lock), 0, 1 );

    return root;
}

__attribute__((deprecated))
bool ringbuffer_destroy( RingBuffer_t * root )
{
    sem_wait( &(root->lock) );
    sem_destroy( &(root->lock) );
    free( root );
    return true;
}

__attribute__((deprecated))
size_t ringbuffer_capacity(RingBuffer_t *root) {
    return root->capacity - ringbuffer_length( root ) - 1;
}

__attribute__((deprecated))
size_t ringbuffer_length( RingBuffer_t *root ) {
    if( root->head == root->tail )
        return 0;

    if( root->head > root->tail )
        return root->head - root->tail;
    return (root->end - root->tail) + (root->head - root->start);
}

__attribute__((deprecated))
ssize_t ringbuffer_peek_copy( RingBuffer_t * root, void * buffer, size_t maxLength ) {
    sem_wait( &(root->lock) );

    size_t readLength = minValue(ringbuffer_length(root), maxLength );

    assert( readLength <= root->capacity, "Length overflow, attempted read was larger than the buffer ever was." );

    if( readLength == 0 ) {
        sem_post( &(root->lock) );
        return 0;
    }

    if( readLength <= root->end - root->tail ) {

        if( buffer != NULL )
            memcpy( buffer, root->tail, readLength ); // Basic single copy

        sem_post( &(root->lock) );
        return readLength;
    }

    if( buffer != NULL ) {
        memcpy(buffer, root->tail, root->end - root->tail);
        memcpy(buffer + (root->end - root->tail), root->start, readLength - (root->end - root->tail));
    }

    sem_post( &(root->lock) );
    return readLength;
}

__attribute__((deprecated))
ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, size_t maxLength )
{
    sem_wait( &(root->lock) );

    size_t readLength = minValue(ringbuffer_length(root), maxLength );

    assert( readLength <= root->capacity, "Length overflow, attempted read was larger than the buffer ever was." );

    if( readLength == 0 ) {
        sem_post( &(root->lock) );
        return 0;
    }

    if( readLength <= root->end - root->tail ) {

        if( buffer != NULL )
            memcpy( buffer, root->tail, readLength ); // Basic single copy

        root->tail += readLength;
        if( root->tail >= root->end )
            root->tail = root->start;

        sem_post( &(root->lock) );
        return readLength;
    }

    if( buffer != NULL ) {
        memcpy(buffer, root->tail, root->end - root->tail);
        memcpy(buffer + (root->end - root->tail), root->start, readLength - (root->end - root->tail));
    }

    root->tail += readLength;
    if( root->tail >= root->end )
        root->tail = root->start + (root->tail - root->end);

    sem_post( &(root->lock) );
    return readLength;
}

__attribute__((deprecated))
uint8_t ringbuffer_peek( RingBuffer_t * root, size_t offset ) {
    sem_wait( &(root->lock) );

    void * ref = root->tail + (offset % root->capacity);
    if( ref > root->end )
        ref = root->start + ( root->tail + (offset % root->capacity) - root->end );

    uint8_t tmp = *(uint8_t *)(ref);
    sem_post( &(root->lock) );
    return tmp;
}

/**
 * Peek a 16-bit unsigned int from the supplied <strong>byte offset</strong>
 *
 * @param root The RingBuffer structure to use
 * @param offset The offset, in bytes
 * @return The value at this offset
 */
__attribute__((deprecated))
uint16_t ringbuffer_peek16( RingBuffer_t * root, size_t offset ) {
    sem_wait( &(root->lock) );

    void * ref = root->tail + (offset % root->capacity);
    if( ref > root->end )
        ref = root->start + ( root->tail + (offset % root->capacity) - root->end );

    uint16_t tmp = *(uint16_t *)(ref);
    sem_post( &(root->lock) );
    return tmp;
}

/**
 * Peek a 32-bit unsigned int from the supplied <strong>byte offset</strong>
 *
 * @param root The RingBuffer structure to use
 * @param offset The offset, in bytes
 * @return The value at this offset
 */
__attribute__((deprecated))
uint32_t ringbuffer_peek32( RingBuffer_t * root, size_t offset ) {
    /*sem_wait( &(root->lock) );

    void * ref = root->tail + (offset % root->capacity);
    if( ref > root->end )
        ref = root->start + ( root->tail + (offset % root->capacity) - root->end );

    uint32_t tmp = *(uint32_t *)(ref);
    sem_post( &(root->lock) );*/

    uint32_t tmp = (ringbuffer_peek( root, offset+3 ) << 24) | 
                    (ringbuffer_peek( root, offset+2 ) << 16) |
                    (ringbuffer_peek( root, offset+1 ) << 8) |
                    ringbuffer_peek( root, offset+3 );

    return tmp;
}

__attribute__((deprecated))
ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, size_t length ) {
    sem_wait( &(root->lock) );

    size_t space = ringbuffer_capacity(root);

    // Does it fit -at-all- ?
    if( space <= length ) {
        sem_post(&(root->lock));
        return 0;
    }

    // If it fits in the remaining space
    size_t rspace = root->end - root->head;
    if( length < rspace ) {
        memcpy(root->head, buffer, length);

        // Update head, loop if required
        root->head = root->head + length;
        if( root->head > root->end )
            root->head = root->start;

        sem_post( &(root->lock) );
        return length;
    }

    // It still fits, but needs to be split
    memcpy( root->head, buffer, rspace );
    memcpy( root->start, buffer+rspace, length-rspace );

    // Update the head
    root->head = root->start + (length-rspace);

    sem_post( &(root->lock) );
    return length;
}