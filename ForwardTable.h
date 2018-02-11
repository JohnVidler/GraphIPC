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

#include "lib/GraphNetwork.h"

typedef struct edge {
    gnw_address_t target;
    void * context;
    struct edge * next;
} edge_t;

typedef struct forward {
    int forward_policy;
    edge_t * edgeList;
    pthread_mutex_t listLock;
    edge_t * round_robin_ref;
} forward_t;

void forward_table_init();
void forward_table_add_edge( gnw_address_t source, gnw_address_t target );
void forward_table_remove_edge( gnw_address_t source, gnw_address_t target );
forward_t * forward_table_find( gnw_address_t source );
void forward_table_remove( gnw_address_t source );

edge_t * forward_table_get_iterator( gnw_address_t source );
void forward_table_release_iterator( gnw_address_t source );