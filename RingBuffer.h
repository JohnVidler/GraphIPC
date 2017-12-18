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
} RingBuffer_t;

void ringbuffer_print( RingBuffer_t * root );

RingBuffer_t * ringbuffer_init( size_t size );

bool ringbuffer_destroy(RingBuffer_t * ring );

ssize_t ringbuffer_length( RingBuffer_t * root );

uint8_t ringbuffer_peek( RingBuffer_t * root );

ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, ssize_t maxLength );

ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, size_t length );