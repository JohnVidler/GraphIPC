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
#include "ForwardTable.h"
#include "IndexTable.h"
#include "Log.h"

struct avl_table * forward_table;
pthread_mutex_t forward_table_lock;

edge_t * find_edge_to( edge_t * head, gnw_address_t target ) {
    edge_t * iter = head;
    while( iter != NULL ) {
        if( iter->target == target )
            return iter;
        iter = iter->next;
    }
    return NULL;
}

void prepend_edge( forward_t * entry, edge_t * edge ) {
    pthread_mutex_lock( &entry->listLock );
    printf( "Adding edge to entry...\n" );
    edge->next = entry->edgeList;
    entry->edgeList = edge;
    pthread_mutex_unlock( &entry->listLock );
}

bool remove_edge( forward_t * entry, edge_t * edge ) {
    pthread_mutex_lock( &entry->listLock );
    if( entry->round_robin_ref == edge )
        entry->round_robin_ref = NULL;

    if( entry->edgeList == edge ){
        entry->edgeList = edge->next;
        pthread_mutex_unlock( &entry->listLock );
        return true;
    }

    edge_t * iter = entry->edgeList;
    while( iter != NULL ) {
        if( iter->next == edge ) {
            iter->next = edge->next;
            pthread_mutex_unlock( &entry->listLock );
            return true;
        }
        iter = iter->next;
    }
    pthread_mutex_unlock( &entry->listLock );

    log_warn( "Unable to find an edge to [%08x]", edge->target );
    return false;
}

edge_t * forward_table_get_iterator( gnw_address_t source ) {
    forward_t * entry = table_find( forward_table, source );
    if( entry == NULL ) {
        log_error( "Refusing to grant an iterator lock on a non-existent entry! [%08x]", source );
        return NULL;
    }
    pthread_mutex_lock( &entry->listLock );
    return entry->edgeList;
}

void forward_table_release_iterator( gnw_address_t source ) {
    forward_t * entry = table_find( forward_table, source );
    if( entry == NULL ) {
        log_error( "Attempted to release an interator for an unknown forward_table entry? [%08x]", source );
        return;
    }
    pthread_mutex_unlock( &entry->listLock );
}

void forward_table_init() {
    forward_table = table_create();
    pthread_mutex_init( &forward_table_lock, NULL );
}

void forward_table_add_edge( gnw_address_t source, gnw_address_t target ) {
    pthread_mutex_lock( &forward_table_lock );

    // Get (or create) the forward line for this source address
    forward_t * entry = table_find( forward_table, source );
    if( entry == NULL ){
        printf( "No entry, making a new one\n" );
        entry = malloc( sizeof(forward_t) );
        entry->forward_policy = GNW_POLICY_BROADCAST; // Default to broadcast
        entry->round_robin_ref = NULL;
        pthread_mutex_init( &entry->listLock, NULL );
        entry->edgeList = NULL;
        table_put( forward_table, source, entry ); // Actually put this new entry in the table - how did I miss this?!
    }

    printf( "Making a new edge [%08x] -> [%08x]\n", source, target );

    // Build a new edge
    edge_t * edge = malloc( sizeof(edge_t) );
    edge->context = NULL; // Only fill this out on a retrieval op
    edge->target = target;
    edge->next = NULL;
    prepend_edge( entry, edge ); // Automatically locks, internally

    pthread_mutex_unlock( &forward_table_lock );
}

void forward_table_remove_edge( gnw_address_t source, gnw_address_t target ) {
    pthread_mutex_lock( &forward_table_lock );

    // Get the entry for this source address
    forward_t * entry = table_find( forward_table, source );
    if( entry == NULL ) {
        log_warn( "Tried to remove a nonexistent edge [%08x] -> [%08x], did nothing.", source, target );
        pthread_mutex_unlock( &forward_table_lock );
        return;
    }

    // Find the target in the edge list, if at all
    edge_t * edge = find_edge_to( entry->edgeList, target );
    if( edge == NULL ) {
        log_warn( "Tried to remove a nonexistent edge [%08x] -> [%08x], did nothing.", source, target );
        pthread_mutex_unlock( &forward_table_lock );
        return;
    }

    remove_edge(entry, edge);

    // Attempt to prune this entire entry, if we have no more edges!
    pthread_mutex_lock( &entry->listLock ); // Never unlock, we're killing this entry
    if( entry->edgeList == NULL ) {
        table_remove( forward_table, source );
        free( entry );
    }

    pthread_mutex_unlock( &forward_table_lock );
}

forward_t * forward_table_find( gnw_address_t source ) {
    return table_find( forward_table, source );
}

void forward_table_remove( gnw_address_t source ) {
    pthread_mutex_lock( &forward_table_lock );

    forward_t * entry = table_find( forward_table, source );

    // Repeatedly remove edges until there are none left.
    pthread_mutex_lock( &entry->listLock ); // Never unlock, we're killing this entry
    while( entry->edgeList != NULL ) {
        remove_edge( entry, entry->edgeList );
    }

    // Remove the entry in the index'd forward_table
    table_remove( forward_table, source );
    free( entry );

    pthread_mutex_unlock( &forward_table_lock );
}