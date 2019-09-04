#include "packet.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

uint8_t * packet_write_u8( uint8_t * buffer, uint8_t data )
{
    *buffer++ = data;
    return buffer;
}

uint8_t * packet_write_u16( uint8_t * buffer, uint16_t data )
{
    *buffer++ = (data >> 8) & 0xFF;
    *buffer++ = data & 0xFF;
    return buffer;
}

uint8_t * packet_write_u32( uint8_t * buffer, uint32_t data )
{
    *buffer++ = (data >> 24) & 0xFF;
    *buffer++ = (data >> 16) & 0xFF;
    *buffer++ = (data >> 8) & 0xFF;
    *buffer++ = data & 0xFF;
    return buffer;
}

uint8_t * packet_read_u8( uint8_t * buffer, uint8_t * data )
{
    *data = *buffer++;
    return buffer;
}

uint8_t * packet_read_u16( uint8_t * buffer, uint16_t * data )
{
    *data = (*buffer << 8) | *(buffer+1);
    return buffer+2;
}

uint8_t * packet_read_u32( uint8_t * buffer, uint32_t * data )
{
    *data = (*(buffer+3) << 24) | (*(buffer+2) << 16) | (*(buffer+1) << 8) | *(buffer);
    return buffer+4;
}


uint8_t * packet_write_u8_buffer( uint8_t * buffer, uint8_t * data, size_t length ) {
    memcpy( buffer, data, length );
    return buffer + length;
}

uint8_t * packet_write_u16_buffer( uint8_t * buffer, uint16_t * data, size_t length ) {
    uint16_t * ptr = data;
    while( ptr < data + length )
        buffer = packet_write_u16( buffer, *ptr++ );
    return buffer;
}

uint8_t * packet_read_u8_buffer( uint8_t * buffer, uint8_t * data, size_t length ) {
    memcpy( data, buffer, length );
    return buffer + length;
}

uint8_t * packet_read_u16_buffer( uint8_t * buffer, uint16_t * data, size_t length ) {
    uint16_t * ptr = data;
    while( ptr < data + length )
        buffer = packet_read_u16( buffer, ptr++ );
    return buffer;
}