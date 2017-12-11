//
// Created by john on 11/12/17.
//

#include <semaphore.h>

#pragma ONCE

typedef struct {
    sem_t lock;
    void * start;
    void * end;
    void * head;
    void * tail;
} RingBuffer_t;

RingBuffer_t * ringbuffer_init( size_t size );

bool ringbuffer_destroy(RingBuffer_t * ring );

ssize_t ringbuffer_length( RingBuffer_t * root );

ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, ssize_t maxLength );

ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, ssize_t length );