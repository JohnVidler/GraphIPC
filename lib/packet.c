#include "packet.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

//#define DEBUG

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

#ifdef DEBUG
    printf( "{%2x}", *data );
#endif

    return buffer;
}

uint8_t * packet_read_u16( uint8_t * buffer, uint16_t * data )
{
    *data = (*buffer << 8) | *(buffer+1);

#ifdef DEBUG
    printf( "{%4x}", *data );
#endif

    return buffer+2;
}

uint8_t * packet_read_u32( uint8_t * buffer, uint32_t * data )
{
    *data = (*(buffer) << 24) | (*(buffer+1) << 16) | (*(buffer+2) << 8) | *(buffer+3);

#ifdef DEBUG
    printf( "{%8x}", *data );
#endif

    return buffer+4;
}


uint8_t * packet_write_u8_buffer( uint8_t * buffer, uint8_t * data, size_t length ) {
#ifdef DEBUG
    printf( "{+%lu}", length );
#endif

    memcpy( buffer, data, length );
    return buffer + length;
}

uint8_t * packet_write_u16_buffer( uint8_t * buffer, uint16_t * data, size_t length ) {
#ifdef DEBUG
    printf( "{+%lu}", length * 2 );
#endif

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

uint8_t * packet_shift( uint8_t * buffer, size_t buffer_size, uint8_t * out, size_t length ) {
    if( out != NULL )
        memcpy( out, buffer, length ); // Optionally copy to 'out'
    memmove( buffer, buffer+length, buffer_size-length ); // 'Shift' the buffer over

    return buffer+(buffer_size-length);
}