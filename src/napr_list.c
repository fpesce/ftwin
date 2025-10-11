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
 * List to use only in another struct ('cause it knows nothing about what it handles)
 * so , use with a high level stuff that knows function to compare or destroy element.
 */
#include <stdlib.h>
#include <stdio.h>

#include "napr_list.h"

struct napr_cell_t
{
    napr_cell_t *next;
#ifdef DOUBLY_LINK
    napr_cell_t *prev;
#endif				/* DOUBLY_LINK */
    void *data;
};

struct napr_list_t
{
    apr_pool_t *p;
    napr_cell_t *head;
    napr_cell_t *tail;
    int nb_cells;
};

/* APR_POOL_IMPLEMENT_ACCESSOR(list); */
apr_pool_t *napr_list_get_allocator(const napr_list_t *list)
{
    return list->p;
}

napr_list_t *napr_list_make(apr_pool_t *p)
{
    napr_list_t *napr_list = apr_palloc(p, sizeof(struct napr_list_t));

    if (napr_list != NULL) {
	napr_list->p = p;
	napr_list->head = NULL;
	napr_list->tail = NULL;
	napr_list->nb_cells = 0;
    }

    return napr_list;
}

int napr_list_cons(napr_list_t *napr_list, void *element)
{
    napr_cell_t *cell;
    int rc; /** return code. */

    if (NULL != (cell = (napr_cell_t *) apr_palloc(napr_list->p, sizeof(struct napr_cell_t)))) {
	cell->data = element;
	cell->next = napr_list->head;
#ifdef DOUBLY_LINK
	cell->prev = NULL;
#endif /* DOUBLY_LINK */

#ifdef DOUBLY_LINK
	if (napr_list->head != NULL)
	    napr_list->head->prev = cell;
#endif /* DOUBLY_LINK */
	napr_list->head = cell;
	napr_list->nb_cells += 1;

	if (napr_list->nb_cells == 1)
	    napr_list->tail = napr_list->head;

	rc = 0;
    }
    else {
	rc = -1;
    }

    return rc;
}

void napr_list_cdr(napr_list_t *napr_list)
{
    void *cell;

    cell = napr_list->head->next;
    napr_list->nb_cells -= 1;
    napr_list->head = cell;
#ifdef DOUBLY_LINK
    if (napr_list->head != NULL)
	napr_list->head->prev = NULL;
#endif /* DOUBLY_LINK */
}

void napr_list_delete(napr_list_t *napr_list)
{
    while (napr_list->head != NULL)
	napr_list_cdr(napr_list);
}

int napr_list_member(napr_list_t *napr_list, void *element, int (*compare)(const void *key1, const void *key2))
{
    napr_cell_t *ptr;
    int rc = 1;

    /* Stop when pointer is NULL (then rc must be equal to smthg != 0) */
    for (ptr = napr_list->head; (NULL != ptr) && (0 != (rc = compare(ptr->data, element))); ptr = ptr->next);

    /* if 0 == rc then the compare function returned 0 then it means this elemnt is member of list -> return 1 */
    return (0 == rc) ? 1 : 0;
}

int napr_list_insert(napr_list_t *napr_list, void *element, int (*compare)(const void *key1, const void *key2))
{
    int rc = 0;

    if (0 != napr_list_member(napr_list, element, compare)) {
	/* if element is not member of the list */
	rc = 1;
	if (0 > napr_list_enqueue(napr_list, element))
	    /* allocation error case */
	    rc = -1;
    }

    /* rc is 0 if no adding was done -1 in case of error + 1 otherwise */
    return rc;
}

int napr_list_enqueue(napr_list_t *napr_list, void *element)
{
    napr_cell_t *cell;
    int rc = 0;

    if (0 != napr_list->nb_cells) {
	if (NULL != (cell = (napr_cell_t *) apr_palloc(napr_list->p, sizeof(struct napr_cell_t)))) {
	    napr_list->nb_cells += 1;
	    cell->data = element;
	    cell->next = NULL;
#ifdef DOUBLY_LINK
	    cell->prev = napr_list->tail;
#endif /* DOUBLY_LINK */
	    napr_list->tail->next = cell;
	    napr_list->tail = cell;
	}
	else
	    rc = -1;
    }
    else {
	rc = napr_list_cons(napr_list, element);
    }

    return rc;
}

napr_cell_t *napr_list_first(napr_list_t *napr_list)
{
    return napr_list->head;
}

napr_cell_t *napr_list_last(napr_list_t *napr_list)
{
    return napr_list->tail;;
}

napr_cell_t *napr_list_next(napr_cell_t *cell)
{
    return cell->next;
}

#ifdef DOUBLY_LINK
napr_cell_t *napr_list_prev(napr_cell_t *cell)
{
    return cell->prev;
}
#endif /* DOUBLY_LINK */

void *napr_list_get(napr_cell_t *cell)
{
    return cell->data;
}
