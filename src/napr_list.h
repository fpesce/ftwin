/*
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * 	http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * List to use with apr lib environment.
 * TODO during _make, may create it's own pool, then * provide an allocator
 * (via napr_list.h api), to allocate elements you want to put in, then provide
 * a "delete function" (because list are memory intensive).
 */
/**
 * @file napr_list.h
 * @brief Not APR chained list
 */
#ifndef NAPR_LIST_H
#define NAPR_LIST_H
#include <apr_pools.h>

typedef struct napr_list_t napr_list_t;
typedef struct napr_cell_t napr_cell_t;

/**
 * Return the allocator associated to the heap, thus you can allow elements
 * that will be automatically freed when the heap will be.
 * @param heap The heap you are working with.
 * @return Return a pointer to the allcator of type apr_pool_t.
 */
apr_pool_t *napr_list_get_allocator(const napr_list_t *list);
/* APR_POOL_DECLARE_ACCESSOR(list); */

/**
 * Fill a napr_list structure with needed fields.
 * @param p Associated pool.
 * @return NULL if an allocation error occured.
 */
napr_list_t *napr_list_make(apr_pool_t *p);

/** 
 * Add an element to a napr_list
 * @param napr_list The list where you want to add the element.
 * @param element Allocated element that will be put in the beggining of a napr_list.
 * @return 0 if no error occured -1 otherwise.
 */
int napr_list_cons(napr_list_t *napr_list, void *element);

/** 
 * Remove the first element of a napr_list.
 * @param napr_list The list where you want to remove the first element.
 */
void napr_list_cdr(napr_list_t *napr_list);

/** 
 * Destroy all the elements of a list.
 * @param napr_list The list where you want to remove the elements.
 */
void napr_list_delete(napr_list_t *napr_list);

/** 
 * Check if an element is equal to an element of a list.
 * @param napr_list The list you are working on.
 * @param element The element you want to compare.
 * @param compare The function used to compare two elements.
 * @return 1 if the element is in a napr_list 0 otherwise.
 */
int napr_list_member(napr_list_t *napr_list, void *element, int (*compare) (const void *key1, const void *key2));

/** 
 * Insert an element in a napr_list, if no element is equal to the one you want to insert (using function to compare)
 * @param napr_list The list you are working on.
 * @param element The element you want to insert.
 * @param compare The function used to compare two elements.
 * @return 1 if the element is in a napr_list, -1 if an error occured during insertion, 0 otherwise.
 */
int napr_list_insert(napr_list_t *napr_list, void *element, int (*compare) (const void *key1, const void *key2));

/**
 * Insert an element at the end of a list.
 * @param napr_list The list you are working on.
 * @param element The element you want to insert.
 * @return 0 if no error occured -1 otherwise.
 */
int napr_list_enqueue(napr_list_t *napr_list, void *element);

/**
 * Get the first element of a list.
 * @param napr_list The list you are working on.
 * @return The first element, NULL if the list is empty.
 */
napr_cell_t *napr_list_first(napr_list_t *napr_list);

/**
 * Get the last element of a list.
 * @param napr_list The list you are working on.
 * @return The last element, NULL if the list is empty.
 */
napr_cell_t *napr_list_last(napr_list_t *napr_list);

/**
 * Get the following element of an element.
 * @param cell The current element.
 * @return The following element of a cell, NULL if cell was the last of the list.
 */
napr_cell_t *napr_list_next(napr_cell_t *cell);

#ifdef DOUBLY_LINK
/**
 * Get the previous element of an element.
 * @param cell The current element.
 * @return The previous element of a cell, NULL if cell was the first of the list.
 */
napr_cell_t *napr_list_prev(napr_cell_t *cell);
#endif /* DOUBLY_LINK */

/**
 * Get the content of an element.
 * @param cell The current element.
 * @return The content of an element of type cell_t, NULL if cell is empty.
 */
void *napr_list_get(napr_cell_t *cell);
#endif /* NAPR_LIST_H */
