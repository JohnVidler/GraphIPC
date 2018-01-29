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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "GraphNetwork.h"

const uint32_t mask_lookup_table[4];

typedef struct address_trie {
    gnw_address_t address;
    unsigned int mask;

    struct address_trie * next;
    void * context;
} address_trie_t;

address_trie_t * address_trie_init();
void address_trie_destroy( address_trie_t * root );
address_trie_t * address_trie_put( address_trie_t * root, void * data, gnw_address_t address, unsigned int mask );
address_trie_t * address_trie_remove();
void * address_trie_find( address_trie_t * root, gnw_address_t address, unsigned int maskBytes );

void address_trie_dump( address_trie_t * root, unsigned int indent );

address_trie_t * address_trie_put_route( address_trie_t * root, gnw_address_t address, void * context );