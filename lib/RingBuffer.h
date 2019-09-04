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

/**
 * Prints the contents of the RingBuffer to the terminal
 *
 * @param root A pointer to the RingBuffer to print
 */
void ringbuffer_print( RingBuffer_t * root );

/**
 * Builds a new RingBuffer, handling the allocation of memory.
 *
 * @param size The length of the RingBuffer, in bytes
 * @return The new RingBuffer structure
 */
RingBuffer_t * ringbuffer_init( size_t size );

/**
 * Attempts to destroy the RingBuffer.
 *
 * If the buffer contains any data (head !== tail) then the function
 * will refuse to delete the buffer.
 *
 * @param ring The RingBuffer structure to delete
 * @return True, if the RingBuffer was successfully deleted, else false
 */
bool ringbuffer_destroy( RingBuffer_t * ring );

/**
 * Determines the capacity of the supplied RingBuffer.
 *
 * This is the actual size of the buffer area, less any bookkeeping data.
 *
 * @param root The RingBuffer to examine
 * @return The number of available bytes
 */
size_t ringbuffer_capacity( RingBuffer_t * root);

/**
 * Determines the length of data buffered in the RingBuffer.
 *
 * May be zero.
 *
 * @param root The RingBuffer to examine
 * @return The amount of data currently held, in bytes
 */
size_t ringbuffer_length( RingBuffer_t * root );

/**
 * 'Peeks' the byte at 'offset' bytes in the RingBuffer supplied.
 *
 * @param root The RingBuffer to peek
 * @param offset The byte-offset to peek at, from the head of the RingBuffer
 * @return The value at this position
 */
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

/**
 * Drains up-to 'maxLength' bytes from the RingBuffer supplied.
 *
 * If the buffer runs out of data to push to the receiving block, the remaining bytes
 * will be untouched. (Note, zero your buffers, if you care about known state beyond
 * the end of the copy operation).
 *
 * @param root The RingBuffer to read from
 * @param buffer A pointer to memory to copy the data from the buffer in to
 * @param maxLength The maximum length to copy
 * @return The number of bytes actually copied
 */
ssize_t ringbuffer_read( RingBuffer_t * root, void * buffer, size_t maxLength );

/**
 * Writes 'length' bytes from 'buffer' into the RingBuffer 'root'.
 *
 * If the capacity of the RingBuffer is smaller than the supplied block then this
 * function will bail and return zero to indicate no bytes were copied.
 *
 * Clients should re-attempt to add their data again, or report accordingly.
 *
 * @param root The RingBuffer to write into
 * @param buffer A pointer to memory to copy into the buffer
 * @param length The number of bytes to copy from the 'buffer' pointer
 * @return The number of bytes actually copied.
 */
ssize_t ringbuffer_write( RingBuffer_t * root, void * buffer, size_t length );