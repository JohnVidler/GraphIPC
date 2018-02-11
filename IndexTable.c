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
#include "AddressTable.h"
#include "lib/avl.h"
#include <stdint.h>
#include <stdlib.h>
#include <unitypes.h>
#include "lib/LinkedList.h"

int compareAddress32( const void * a, const void * b, void * param ) {
    if( a == NULL || b == NULL )
        return 0;

    return ((entry_t *)a)->address - ((entry_t *)b)->address;
}

void * table_create() {
    return avl_create( compareAddress32, NULL, &avl_allocator_default );
}

void * table_put(struct avl_table *table, uint32_t address, void * data) {
    entry_t * newEntry = malloc( sizeof(entry_t) );
    newEntry->address = address;
    newEntry->data = data;

    avl_insert( table, newEntry );

    return newEntry->data;
}

void * table_find(struct avl_table *table, uint32_t address) {
    entry_t query = { .address=address };
    return &((entry_t * )avl_find( table, &query ))->links;
}

void * table_remove( struct avl_table *table, uint32_t address) {
    entry_t query = { .address=address };
    return avl_delete( table, &query );
}