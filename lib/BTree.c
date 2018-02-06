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
#include "BTree.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

btree_node_t * btree_init() {
    btree_node_t * root = malloc( sizeof(btree_node_t) );
    root->lt = NULL;
    root->gt = NULL;
    root->key = 0;
    root->value = NULL;

    return root;
}

btree_node_t * btree_put( btree_node_t * root, uint32_t key, void * value ) {

    // In-place insert
    if( root->value == NULL ) {
        root->value = value;
        root->key = key;
        return root;
    }

    // In-place Replace
    if( root->key == key ) {
        root->value = value;
        return root;
    }

    // The key is less than our key
    if( key < root->key ) {

        // There's nowhere to recurse to
        if( root->lt == NULL ) {
            root->lt = btree_init();
            return btree_put( root->lt, key, value );
        }

        // Is the key greater than the less than branch, but smaller than us?
        if( key > root->lt->key ) {
            // If we have a NULL greater than branch, copy root over and insert the new key here.
            if( root->gt == NULL ) {
                root->gt = btree_init();
                root->gt->key = root->key;
                root->gt->value = root->value;
                root->key = key;
                root->value = value;
                return root;
            }
        }

        // GT must be non-null by now, so all we can do is recurse down the GT path
        return btree_put( root->lt, key, value );
    }

    // Key must be greater than our key now
    if( key > root->key ) {
        // If there's nowhere to recurse to...
        if (root->gt == NULL) {
            root->gt = btree_init();
            return btree_put(root->gt, key, value);
        }

        // Otherwise, is the key smaller than the greater than branch, but bigger than us?
        if (key < root->gt->key) {
            // If we have a NULL less than branch, copy root over and insert the new key here.
            if( root->lt == NULL ) {
                root->lt = btree_init();
                root->lt->key = root->key;
                root->gt->value = root->value;
                root->key = key;
                root->value = value;
                return root;
            }
        }

        // LT must be non-null by now, and we can't swap so all we can do is recurse down the GT path
        return btree_put(root->gt, key, value);
    }
}

void * btree_get( btree_node_t * root, uint32_t key ) {

    // Bail if there's nothing here
    if( root->value == NULL )
        return NULL;

    // Return this if we found it here
    if( root->key == key )
        return root->value;

    // Wander the less-than fork
    if( key < root->key && root->lt != NULL )
        return btree_get( root->lt, key );

    // Wander the right-than fork
    if( key > root->key && root->gt != NULL )
        return btree_get( root->gt, key );

    // Unreachable key :(
    return NULL;
}

void * btree_remove( btree_node_t * root, uint32_t key ) {

    // Unreachable key :(
    return NULL;
}