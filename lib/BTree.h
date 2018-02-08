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

#include <stdint.h>

typedef struct btree_node {
    uint32_t key;
    void * value;
    struct btree_node * parent;
    struct btree_node * lt;
    struct btree_node * gt;
} btree_node_t;

btree_node_t * rotate_lt( btree_node_t * node );
btree_node_t * rotate_gt( btree_node_t * node );

btree_node_t * btree_init( btree_node_t * parent );
btree_node_t * btree_put( btree_node_t * root, uint32_t key, void * value );
void * btree_get( btree_node_t * root, uint32_t key );
void * btree_remove( btree_node_t * root, uint32_t key );