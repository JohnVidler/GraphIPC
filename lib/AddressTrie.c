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
#include <string.h>

const uint32_t mask_lookup_table[4] = {
        0b11111111000000000000000000000000,
        0b11111111111111110000000000000000,
        0b11111111111111111111111100000000,
        0b11111111111111111111111111111111
};

address_trie_t ** address_trie_init() {
    address_trie_t ** root = malloc( sizeof(address_trie_t *) * 256 );
    for( int i=0; i<256; i++ )
        root[i] = NULL;
    return root;
}

address_trie_t * address_trie_put( address_trie_t ** root, void * context, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    // On-demand fill this block
    if( root[localOffset] == NULL ) {
        root[localOffset] = malloc(sizeof(address_trie_t));
        memset( root[localOffset], 0, sizeof(address_trie_t) );
    }

    // Is there another layer to go through already?
    if( root[localOffset]->next != NULL )
        return address_trie_put( root[localOffset]->next, context, address, maskBytes+1 );

    // Is there no further refined address needed?
    if (root[localOffset]->context == NULL) {
        root[localOffset]->address = address;
        root[localOffset]->mask = maskBytes;
        root[localOffset]->context = context;
        return root[localOffset];
    }

    // Update the current context, if the address is a valid match for the lookup address (shortcuts!)
    /*if( (root+localOffset)->address == lookupAddress )
        (root+localOffset)->context = context;*/

    // If we're here, then next IS null, and context IS NOT null, time to split and refine...
    root[localOffset]->next = address_trie_init();

    address_trie_put( root[localOffset]->next, root[localOffset]->context, root[localOffset]->address, maskBytes+1 );
    root[localOffset]->address = lookupAddress;
    root[localOffset]->mask    = maskBytes;

    return address_trie_put( root[localOffset]->next, context, address, maskBytes+1 );
}

/**
 *
 * @param root
 * @param address
 * @param maskBytes
 * @return True, if the address was already gone, or is now gone
 */
bool address_trie_remove( address_trie_t ** root, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    if( root[localOffset] == NULL )
        return true; // It was gone anyway!

    // Can we just kill this level? (ie. there are no more beyond here)
    if( root[localOffset]->next == NULL ) {
        free( root[localOffset] );
        root[localOffset] = NULL;
        return true;
    }

    // If we're here, then there are additional levels, can they be gone too?
    if( address_trie_remove( root[localOffset]->next, address, maskBytes+1 ) ) {
        // Scan the next level, count pointers...
        unsigned int pointers = 0;
        for( int i=0; i<256; i++)
            pointers += (root[localOffset]->next[i] != NULL ? 1 : 0);

        // Nothing left!
        if( pointers == 0 ) {
            free( root[localOffset]->next );
            root[localOffset] = NULL;
        }

        return true;
    }

    // Could not remove this block, something is still using it!
    return false;
}

void * address_trie_find( address_trie_t ** root, gnw_address_t address, unsigned int maskBytes ) {
    gnw_address_t lookupAddress = address & mask_lookup_table[maskBytes];
    uint8_t localOffset = (uint8_t)((lookupAddress >> (8*(3-maskBytes))) & 0xff);

    // Don't even bother if there's no memory ref here!
    if( root[localOffset] == NULL )
        return NULL;

    if( root[localOffset]->address == address )
        return root[localOffset]->context;

    if( root[localOffset]->next != NULL )
        return address_trie_find( root[localOffset]->next, address, maskBytes+1 );

    printf( "Giving up :(\n" );

    return NULL;
}

void address_trie_walk( address_trie_t ** root, void(*iter)(void *, void *), void * passthrough ) {
    for( int i=0; i<256; i++ ) {
        if (root[i] != NULL && root[i]->context != NULL) {
            if( root[i]->next != NULL )
                address_trie_walk( root[i]->next, iter, passthrough );
            else
                iter( root[i]->context, passthrough );
        }
    }
}

void address_trie_dump( address_trie_t ** root, unsigned int indent ) {
    for( int i=0; i<256; i++ ) {
        if ( root[i] != NULL && root[i]->context != NULL ) {
            // Just for pretty output :)
            for (int ind = 0; ind < indent; ind++)
                printf("  ");

            printf("%02x -> %08x / %d", i, root[i]->address, (root[i]->mask + 1) * 8);
            printf(" --> %p\n", root[i]->context);

            if (root[i]->next != NULL)
                address_trie_dump(root[i]->next, indent + 1);
        }
    }
}