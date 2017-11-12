//
// Created by John Vidler on 01/11/17.
//

#include <string.h>
#include "utility.h"

uint32_t strlen_array( unsigned int offset, unsigned int length, char ** array ) {
    uint32_t count = 0;

    for( unsigned int i = offset; i<offset+length; i++ )
        count += strlen( array[i] );

    return count;
}