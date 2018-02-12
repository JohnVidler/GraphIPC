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
#include <malloc.h>
#include "IndexTable.h"

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
    entry_t * entry = avl_find( table, &query );
    if( entry != NULL )
        return entry->data;
    return NULL;
}

void * table_remove( struct avl_table *table, uint32_t address) {
    entry_t query = { .address=address };
    entry_t * entry = avl_delete( table, &query );
    if( entry != NULL ) {
        void *data = entry->data;
        free(entry);
        return data;
    }
    free( entry );
    return NULL;
}

void table_walk_r( struct avl_node * node, void (*handler)(uint32_t, void *) ) {
    if( node == NULL )
        return;

    entry_t * entry = node->avl_data;

    handler( entry->address, entry->data );

    table_walk_r( node->avl_link[0], handler );
    table_walk_r( node->avl_link[1], handler );
}

void table_walk( struct avl_table *table, void (*handler)(uint32_t, void *) ) {
    table_walk_r( table->avl_root, handler );
}