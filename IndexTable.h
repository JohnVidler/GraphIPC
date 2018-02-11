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
#include <unitypes.h>
#include "lib/avl.h"
#include "lib/LinkedList.h"

typedef struct entry {
    uint32_t address;
    void * data;
} entry_t;

void * table_create();
void * table_put(struct avl_table *table, uint32_t address, void * data);
void * table_find(struct avl_table *table, uint32_t address);
void * table_remove(uint32_t address);