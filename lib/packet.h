#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Write a single uint8_t to the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint8_t to write
 * 
 * @return The new buffer position
 */
uint8_t * packet_write_u8( uint8_t * buffer, uint8_t data );

/**
 * Write a single uint16_t to the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint16_t to write
 * 
 * @return The new buffer position
 */
uint8_t * packet_write_u16( uint8_t * buffer, uint16_t data );

/**
 * Write a single uint32_t to the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint32_t to write
 * 
 * @return The new buffer position
 */
uint8_t * packet_write_u32( uint8_t * buffer, uint32_t data );

/**
 * Reads a single uint8_t from the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint8_t pointer to read into
 * 
 * @return The new buffer position
 */
uint8_t * packet_read_u8( uint8_t * buffer, uint8_t * data );

/**
 * Reads a single uint16_t from the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint16_t pointer to read into
 * 
 * @return The new buffer position
 */
uint8_t * packet_read_u16( uint8_t * buffer, uint16_t * data );

/**
 * Reads a single uint32_t from the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint32_t pointer to read into
 * 
 * @return The new buffer position
 */
uint8_t * packet_read_u32( uint8_t * buffer, uint32_t * data );

/**
 * Write a uint8_t buffer to the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data The uint8_t buffer to write
 * @param length The length of the input buffer
 * 
 * @return The new buffer position
 */
uint8_t * packet_write_u8_buffer( uint8_t * buffer, uint8_t * data, size_t length );

/**
 * Write a uint16_t buffer to the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data The uint16_t buffer to write
 * @param length The length of the input buffer
 * 
 * @return The new buffer position
 */
uint8_t * packet_write_u16_buffer( uint8_t * buffer, uint16_t * data, size_t length );

/**
 * Reads a uint8_t buffer from the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint8_t buffer pointer to write into
 * @param length The number of uint8_t's to read
 * 
 * @return The new buffer position
 */
uint8_t * packet_read_u8_buffer( uint8_t * buffer, uint8_t * data, size_t length );

/**
 * Reads a uint16_t buffer from the supplied buffer, and returns a pointer
 * to the new location.
 * 
 * @param buffer The current buffer position
 * @param data A uint16_t buffer pointer to write into
 * @param length The number of uint16_t's to read
 * 
 * @return The new buffer position
 */
uint8_t * packet_read_u16_buffer( uint8_t * buffer, uint16_t * data, size_t length );