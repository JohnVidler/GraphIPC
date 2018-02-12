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
#include <pthread.h>
#include "NodeTable.h"
#include "lib/GraphNetwork.h"
#include "IndexTable.h"
#include "Log.h"

struct avl_table * node_table;
pthread_mutex_t node_table_mutex;

void node_table_init() {
    node_table = table_create();
    pthread_mutex_init( &node_table_mutex, NULL );
}

void node_table_add( gnw_address_t address, void * context ) {
    pthread_mutex_lock( &node_table_mutex );
    void * oldContext = node_table_find( address );
    if( oldContext != NULL )
        log_warn( "New address %08x overwrites a previous context - remove old nodes first, otherwise linkage might break internally!", address );
    table_put( node_table, address, context );
    pthread_mutex_unlock( &node_table_mutex );
}

void * node_table_remove( gnw_address_t address ) {
    pthread_mutex_lock( &node_table_mutex );
    void * context = table_remove( node_table, address );
    pthread_mutex_unlock( &node_table_mutex );
    return context;
}

// Note: Does not lock the table mutex! Avoid modifying the table while find() could be running!
void * node_table_find( gnw_address_t address ) {
    return table_find( node_table, address );
}

// Just a plain IndexTable proxy...
void node_table_walk( void (*handler)(uint32_t, void *) ) {
    table_walk( node_table, handler );
}