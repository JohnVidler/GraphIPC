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

#include <stdint.h>
#include <semaphore.h>

typedef struct {
    sem_t lock;
    void * start;
    void * end;
    void * head;
    void * tail;
    uint64_t capacity;
} RingBuffer_t;

void ringbuffer_print( RingBuffer_t * root );

RingBuffer_t * ringbuffer_init( size_t size );

bool ringbuffer_destroy(RingBuffer_t * ring );

size_t ringbuffer_capacity(RingBuffer_t *root);

size_t ringbuffer_length( RingBuffer_t *root );

uint8_t ringbuffer_peek( RingBuffer_t * root, size_t offset );
uint16_t ringbuffer_peek16( RingBuffer_t * root, size_t offset );
uint32_t ringbuffer_peek32( RingBuffer_t * root, size_t offset );

/**
 * Performs the same operations as ringbuffer_read, except does not move the read cursor
 *
 * @param root The ringbuffer structure to use
 * @param buffer A pointer to a preallocated area of memory to copy to
 * @param maxLength The maximum read length requested (may be less)
 * @return The number of bytes actually copied, or -1 if in an error state
 */
ssize_t ringbuffer_peek_copy( RingBuffer_t * root, void * buffer, size_t maxLength );

ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, size_t maxLength );

ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, size_t length );