/**
 * @file napr_heap.h
 * @brief A generic binary heap implementation (min-heap or max-heap).
 * @ingroup DataStructures
 */
/*
 * Copyright (C) 2007 Francois Pesce : francois.pesce (at) gmail (dot) com
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

/**
 * @brief Opaque heap structure.
 */
typedef struct napr_heap_t napr_heap_t;

/**
 * @brief Callback function to compare two elements in the heap.
 * @param[in] a The first element.
 * @param[in] b The second element.
 * @return Should return an integer less than, equal to, or greater than zero if the
 *         first argument is considered to be respectively less than, equal to, or
 *         greater than the second.
 */
typedef int (napr_heap_cmp_callback_fn_t) (const void *, const void *);

/**
 * @brief Optional callback function to delete/deallocate an element when not using APR pools.
 * @param[in] data The element to delete.
 */
typedef void (napr_heap_del_callback_fn_t) (void *);


#include <apr_pools.h>
/**
 * @brief Creates a new heap.
 *
 * A heap is a specialized tree-based data structure that satisfies the heap property:
 * if P is a parent node of C, then the key (the value) of P is either greater than
 * or equal to (in a max heap) or less than or equal to (in a min heap) the key of C.
 * This implementation provides O(log n) time complexity for insertions and O(1) for
 * finding the min/max element.
 *
 * @param[in] pool The APR pool to use for allocations.
 * @param[in] cmp The comparison function that defines the heap order.
 * @return A pointer to the new heap, or NULL on error.
 */
napr_heap_t *napr_heap_make(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp);

/**
 * @brief Creates a new thread-safe heap.
 * @param[in] pool The APR pool to use for allocations.
 * @param[in] cmp The comparison function.
 * @return A pointer to the new thread-safe heap, or NULL on error.
 */
napr_heap_t *napr_heap_make_r(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp);

/**
 * @brief Inserts an element into the heap, maintaining the heap property.
 * @param[in] heap The heap.
 * @param[in] datum The data item to insert.
 * @return 0 on success, -1 on failure.
 */
int napr_heap_insert(napr_heap_t *heap, void *datum);

/**
 * @brief Removes and returns the element at the top of the heap (the min or max element).
 * @param[in] heap The heap.
 * @return A pointer to the top element, or NULL if the heap is empty.
 */
void *napr_heap_extract(napr_heap_t *heap);

/**
 * @brief Gets the element at a specific index in the heap's internal array.
 * @param[in] heap The heap.
 * @param[in] n The index.
 * @return A pointer to the nth element, or NULL if the index is out of bounds.
 * @warning Modifying the element in a way that changes its comparison order will
 *          violate the heap property and lead to incorrect behavior.
 */
void *napr_heap_get_nth(const napr_heap_t *heap, unsigned int n);

/**
 * @brief Gets the current number of elements in the heap.
 * @param[in] heap The heap.
 * @return The number of elements.
 */
unsigned int napr_heap_size(const napr_heap_t *heap);

/**
 * @brief Inserts an element into a thread-safe heap.
 * @param[in] heap The thread-safe heap.
 * @param[in] datum The data item to insert.
 * @return 0 on success, -1 on failure.
 */
int napr_heap_insert_r(napr_heap_t *heap, void *datum);

#endif /* NAPR_HEAP_H */
