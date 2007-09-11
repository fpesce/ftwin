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

#ifndef NAPR_HEAP_H
#define NAPR_HEAP_H

typedef struct napr_heap_t napr_heap_t;
typedef int (napr_heap_cmp_callback_fn_t) (const void *, const void *);
typedef void (napr_heap_display_callback_fn_t) (const void *);
typedef void (napr_heap_del_callback_fn_t) (void *);


#ifdef HAVE_APR
#include <apr_pools.h>
/**
 * Return the allocator associated to the heap, thus you can allow elements
 * that will be automatically freed when the heap will be.
 * @param heap The heap you are working with.
 * @return Return a pointer to the allcator of type apr_pool_t.
 */
apr_pool_t *napr_heap_get_allocator(const napr_heap_t *heap);
/* APR_POOL_DECLARE_ACCESSOR(heap); */

/**
 * Make a new heap structure, a heap is a structure that is able to return the
 * smallest (or highest, according to the way you compare data) element of its
 * set, in a complexity of O(lg n) the same as the complexity to insert a
 * random element.
 * @param pool The associated pool.
 * @param cmp The function that compare two elements to return the smallest.
 * @return Return a pointer to a newly allocated heap NULL if an error occured.
 */
napr_heap_t *napr_heap_make(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp);

/**
 * Re-entrant (Thread safe) version of napr_heap_make.
 * @param pool The associated pool.
 * @param cmp The function that compare two elements to return the smallest.
 * @return Return a pointer to a newly allocated heap NULL if an error occured.
 */
napr_heap_t *napr_heap_make_r(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp);

#else /* !HAVE_APR */
/**
 * Make a new heap structure, a heap is a structure that is able to return the
 * smallest (or highest, according to the way you compare data) element of its
 * set, in a complexity of O(lg n) the same as the complexity to insert a
 * random element.
 * @param cmp The function that compare two elements to return the smallest.
 * @param del The function that destroy (de-allocate) an element.
 * @return Return a pointer to a newly allocated heap NULL if an error occured.
 */
napr_heap_t *napr_heap_make(napr_heap_cmp_callback_fn_t *cmp, napr_heap_del_callback_fn_t *del);

/**
 * Re-entrant (Thread safe) version of napr_heap_make.
 * @param cmp The function that compare two elements to return the smallest.
 * @param del The function that destroy (de-allocate) an element.
 * @return Return a pointer to a newly allocated heap NULL if an error occured.
 */
napr_heap_t *napr_heap_make_r(napr_heap_cmp_callback_fn_t *cmp, napr_heap_del_callback_fn_t *del);
#endif /* HAVE_APR */

/**
 * Deallocate the heap;(NB: if the heap is using apr_pool_t (HAVE_APR macro
 * defined), then it will deallocate elements that have been allocated using
 * the heap allocator, otherwise the function del is used on each element.
 * @param heap The heap you are working with.
 * @return nothing.
 */
void napr_heap_destroy(napr_heap_t *heap);

/**
 * Insert an element in the heap.
 * @param heap The heap you are working with.
 * @param datum The datum you want to insert.
 * @return 0 if no error occured, -1 otherwise.
 */
int napr_heap_insert(napr_heap_t *heap, void *datum);

/**
 * Extract the highest (or the lowest using cmp function) element in the heap,
 * remove it and return it.
 * @param heap The heap you are working with.
 * @return The highest (or lowest according to cmp function) element of the
 * heap, NULL if the heap is empty.
 */
void *napr_heap_extract(napr_heap_t *heap);

/**
 * Get the nth element element in the heap.
 * @param heap The heap you are working with.
 * @param n The index of the tree (take the nth element you encounter in a
 * breadth first traversal of a binary tree)
 * @return A pointer on the nth element of the heap, NULL if there's no
 * such element.
 * @remark if you modify the part of this element that is used in the
 * comparation function, you're doing something really bad!
 */
void *napr_heap_get_nth(const napr_heap_t *heap, unsigned int n);

/**
 * Get the nth element (to a const pointer because you can't extract it until
 * it is the highest or the lowest using cmp function) element in the heap.
 * @param heap The heap you are working with.
 * @param datum The adress of the datum you want to set.
 * @param n The index of the tree (take the nth element you encounter in a
 * breadth first traversal of a binary tree)
 * @return 0 if no error occured, -1 otherwise.
 */
unsigned int napr_heap_size(const napr_heap_t *heap);

/**
 * Re-entrant (Thread safe) version of napr_heap_insert, heap must have been
 * initialized with napr_heap_make_r.
 * @param heap The heap you are working with.
 * @param datum The datum you want to insert.
 * @return 0 if no error occured, -1 otherwise.
 */
int napr_heap_insert_r(napr_heap_t *heap, void *datum);

/**
 * Re-entrant (Thread safe) version of napr_heap_extract, heap must have been
 * initialized with napr_heap_make_r.
 * @param heap The heap you are working with.
 * @return The highest (or lowest according to cmp function) element of the
 * heap, NULL if the heap is empty.
 */
void *napr_heap_extract_r(napr_heap_t *heap);

/** 
 * Attach a callback to the heap in order to display the data stored.
 * @param heap The heap you are working with.
 * @param display A callback used to display content of a datum.
 */
void napr_heap_set_display_cb(napr_heap_t *heap, napr_heap_display_callback_fn_t display);

/** 
 * Display the binary tree using the display callback.
 * @param heap The heap you are working with.
 */
void napr_heap_display(const napr_heap_t *heap);
#endif /* NAPR_HEAP_H */
