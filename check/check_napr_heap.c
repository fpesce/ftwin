/*
 * Copyright (C) 2007-2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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
static apr_pool_t *pool = NULL;

static void setup(void)
{
    apr_status_t status = APR_SUCCESS;

    if (main_pool == NULL) {
	(void) apr_initialize();
	(void) atexit(apr_terminate);
	(void) apr_pool_create(&main_pool, NULL);
    }

    status = apr_pool_create(&pool, main_pool);
    if (status != APR_SUCCESS) {
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
    if (number2->size < number1->size) {
	return 1;
    }

    return 0;
}

/**
 * @brief Populates a heap with a given array of values.
 *
 * This helper function simplifies test setup by encapsulating the logic for
 * creating and inserting multiple elements into a napr_heap.
 *
 * @param heap The heap to populate.
 * @param values An array of values to insert into the heap.
 * @param num_values The number of elements in the values array.
 */
static void populate_heap(napr_heap_t *heap, const apr_off_t values[], int num_values)
{
    for (int i = 0; i < num_values; i++) {
	check_heap_numbers_t *number = apr_palloc(pool, sizeof(struct check_heap_numbers_t));

	number->size = values[i];
	napr_heap_insert_r(heap, number);
    }
}

/**
 * @brief Validates that elements are extracted from the heap in the expected order.
 *
 * This function extracts all elements from the heap and asserts that their
 * values match the corresponding values in an array of expected sorted elements.
 * It also verifies that the heap is empty after all elements have been extracted.
 *
 * @param heap The heap to validate.
 * @param expected_values An array of the expected values in sorted order.
 * @param num_values The number of elements in the expected_values array.
 */
static void validate_heap_extraction(napr_heap_t *heap, const apr_off_t expected_values[], int num_values)
{
    for (int i = 0; i < num_values; i++) {
	check_heap_numbers_t *number = napr_heap_extract(heap);

	ck_assert_msg(number != NULL, "Heap unexpectedly empty at index %d", i);
	ck_assert_int_eq(number->size, expected_values[i]);
    }
    ck_assert_ptr_eq(napr_heap_extract(heap), NULL);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_heap_unordered_bug)
{
    const apr_off_t values[] = { 6298, 43601, 193288, 30460, 193288 };
    const apr_off_t expected_sorted_values[] = { 193288, 193288, 43601, 30460, 6298 };
    const int num_values = sizeof(values) / sizeof(apr_off_t);
    napr_heap_t *heap = napr_heap_make_r(pool, check_heap_numbers_cmp);

    populate_heap(heap, values, num_values);
    validate_heap_extraction(heap, expected_sorted_values, num_values);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_napr_heap_suite(void)
{
    Suite *suite = suite_create("Napr_Heap");
    TCase *tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_napr_heap_unordered_bug);
    suite_add_tcase(suite, tc_core);

    return suite;
}
