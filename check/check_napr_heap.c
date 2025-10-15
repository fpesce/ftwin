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

#include <unistd.h>
#include <stdlib.h>
#include <check.h>

#include <apr_strings.h>

#include "debug.h"
#include "napr_heap.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *main_pool = NULL;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *pool;

static void setup(void)
{
    apr_status_t rs;

    if (main_pool == NULL) {
	apr_initialize();
	atexit(apr_terminate);
	apr_pool_create(&main_pool, NULL);
    }
    rs = apr_pool_create(&pool, main_pool);
    if (rs != APR_SUCCESS) {
	DEBUG_ERR("Error creating pool");
	exit(1);
    }
}

static void teardown(void)
{
    apr_pool_destroy(pool);
}

struct check_heap_numbers_t
{
    apr_off_t size;
};
typedef struct check_heap_numbers_t check_heap_numbers_t;

static int check_heap_numbers_cmp(const void *param1, const void *param2)
{
    const check_heap_numbers_t *number1 = param1;
    const check_heap_numbers_t *number2 = param2;

    if (number1->size < number2->size) {
	return -1;
    }
    else if (number2->size < number1->size) {
	return 1;
    }

    return 0;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_heap_unordered_bug)
{
    apr_off_t array[] = { 6298, 43601, 193288, 30460, 193288 };
    napr_heap_t *heap;
    check_heap_numbers_t *number;
    int i;

    heap = napr_heap_make_r(pool, check_heap_numbers_cmp);

    for (i = 0; i < sizeof(array) / sizeof(apr_off_t); i++) {
	number = apr_palloc(pool, sizeof(struct check_heap_numbers_t));
	number->size = array[i];
	napr_heap_insert_r(heap, number);
    }

    for (i = 0; i < sizeof(array) / sizeof(apr_off_t); i++) {
	number = napr_heap_extract(heap);
	switch (i) {
	case 0:
	    ck_assert_int_eq(number->size, 193288);
	    break;
	case 1:
	    ck_assert_int_eq(number->size, 193288);
	    break;
	case 2:
	    ck_assert_int_eq(number->size, 43601);
	    break;
	case 3:
	    ck_assert_int_eq(number->size, 30460);
	    break;
	case 4:
	    ck_assert_int_eq(number->size, 6298);
	    break;
	}
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_napr_heap_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Napr_Heap");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_napr_heap_unordered_bug);
    suite_add_tcase(s, tc_core);

    return s;
}
