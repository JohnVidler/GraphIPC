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
#include "AddressTrie.h"

const uint32_t mask_lookup_table[4] = {
        0b11111111000000000000000000000000,
        0b11111111111111110000000000000000,
        0b11111111111111111111111100000000,
        0b11111111111111111111111111111111
};

address_trie_t * address_trie_init() {
    address_trie_t * root = malloc( sizeof(address_trie_t) * 256 );
    for( int i=0; i<256; i++ ) {
        (root + i)->address = 0;
        (root + i)->mask    = 0;
        (root + i)->next    = NULL;
        (root + i)->context = NULL;
    }
    return root;
}

address_trie_t * address_trie_put( address_trie_t * root, void * context, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    // Is there another layer to go through already?
    if( (root+localOffset)->next != NULL )
        return address_trie_put( (root+localOffset)->next, context, address, maskBytes+1 );

    // Is there no further refined address needed?
    if ((root + localOffset)->context == NULL) {
        (root + localOffset)->address = address;
        (root + localOffset)->mask = maskBytes;
        (root + localOffset)->context = context;
        return root;
    }

    // Update the current context, if the address is a valid match for the lookup address (shortcuts!)
    /*if( (root+localOffset)->address == lookupAddress )
        (root+localOffset)->context = context;*/

    // If we're here, then next IS null, and context IS NOT null, time to split and refine...
    (root + localOffset)->next = address_trie_init();

    address_trie_put( (root+localOffset)->next, (root+localOffset)->context, (root+localOffset)->address, maskBytes+1 );
    (root+localOffset)->address = lookupAddress;
    (root+localOffset)->mask    = maskBytes;

    return address_trie_put( (root+localOffset)->next, context, address, maskBytes+1 );
}

/*bool address_trie_remove( address_trie_t * root, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    // Is there an address here matching at all?
    if( (root+localOffset)->context == NULL )
        return false;

    // Is there further refinement to be done?
    if( (root+localOffset)->next == NULL ) {

    }
}*/

void * address_trie_find( address_trie_t * root, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    if( (root+localOffset)->address == address )
        return (root+localOffset)->context;

    if( (root+localOffset)->next != NULL )
        return address_trie_find( (root+localOffset)->next, address, maskBytes+1 );

    printf( "Giving up :(\n" );

    return NULL;
}

void address_trie_dump( address_trie_t * root, unsigned int indent ) {
    for( int i=0; i<256; i++ ) {
        if( (root+i)->context != NULL ) {
            // Just for pretty output :)
            for (int ind = 0; ind < indent; ind++)
                printf("  ");

            printf("%02x -> %08x / %d", i, (root+i)->address, ((root+i)->mask+1) * 8 );
            printf( " --> %p\n", (root+i)->context );

            if( (root+i)->next != NULL )
                address_trie_dump( (root+i)->next, indent+1 );
        }
    }
}

/*address_trie_t * address_trie_put_route( address_trie_t * root, gnw_address_t address, void * context ) {
    gnw_address_t lookup_address = __builtin_bswap32( address );
}*/