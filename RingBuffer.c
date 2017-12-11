//
// Created by john on 11/12/17.
//

#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <stdio.h>
#include <memory.h>
#include "RingBuffer.h"

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

    if( root->head > root->tail )
        return root->head - root->tail;
    else
        return (root->head - root->end) + (root->start - root->tail);

}

ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, ssize_t maxLength )
{
    sem_wait( &(root->lock) );

    root->tail += maxLength;

    sem_post( &(root->lock) );
    return 0;
}

ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, ssize_t length )
{
    sem_wait( &(root->lock) );

    if( root->head < root->tail ) {
        // Danger of overwriting the tail
        //ssize_t rightLen =
    }
    else
    {
        ssize_t rightLen = root->end - root->head;

        if( rightLen < length ) {
            ssize_t leftLen = root->tail - root->start;

            if( leftLen + rightLen < length )
                return -1; // No space left!

            memcpy( root->head, buffer, rightLen ); // Write the initial space
            memcpy( root->start, buffer+rightLen, leftLen ); // Write the remaining data
            root->head = root->start + leftLen; // Update the head pointer

            return length;
        }

        printf( "Remaining %lu B\n", rightLen );
        // Danger of overrunning the storage
    }

    //memcpy( root->head, buffer, length );
    root->head += length;

    sem_post( &(root->lock) );
    return 0;
}