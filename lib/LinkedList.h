//
// Created by john on 04/01/18.
//

#pragma once

#include <stdlib.h>

struct list_element {
    void * data;
    struct list_element * next;
};

typedef struct {
    struct list_element * head;
    struct list_element * tail;
} linked_list_t;

linked_list_t * ll_create();
void ll_destroy( linked_list_t * list );

void ll_append( linked_list_t * list, void * data );
void ll_prepend( linked_list_t * list, void * data );

void * ll_remove_first( linked_list_t * list );
void * ll_remove_last( linked_list_t * list );

size_t ll_length( linked_list_t * list );