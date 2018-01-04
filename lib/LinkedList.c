//
// Created by john on 04/01/18.
//

#include "LinkedList.h"
#include <stdio.h>
#include <malloc.h>

linked_list_t * ll_create() {
    linked_list_t * list = malloc( sizeof( linked_list_t ) );
    list->head = NULL;
    list->tail = NULL;
    return list;
}

void ll_destroy( linked_list_t * list ) {
    fprintf( stderr, "Unimplemented function!\n" );
}

void ll_append( linked_list_t * list, void * data ) {
    struct list_element * element = malloc( sizeof(struct list_element) );
    element->data = data;
    element->next = NULL;

    if( list->head == NULL ) {
        list->head = element;
        list->tail = element;
        return;
    }

    list->tail->next = element;
    list->tail = element;
}

void ll_prepend( linked_list_t * list, void * data ) {
    struct list_element * element = malloc( sizeof(struct list_element) );
    element->data = data;
    element->next = NULL;

    if( list->head == NULL ) {
        list->head = element;
        list->tail = element;
    }

    element->next = list->head;
    list->head = element;
}

void * ll_remove_first( linked_list_t * list ) {
    fprintf( stderr, "Unimplemented function!\n" );
    return NULL;
}

void * ll_remove_last( linked_list_t * list ) {
    fprintf( stderr, "Unimplemented function!\n" );
    return NULL;
}

size_t ll_length( linked_list_t * list ) {
    struct list_element * iter = list->head;
    size_t elements = 0;

    while( (iter = iter->next) != NULL )
        elements++;

    return elements;
}