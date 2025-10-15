/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
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
#include <string.h>
#include <apr_strings.h>

#include "debug.h"
#include "napr_hash.h"

static apr_pool_t *main_pool = NULL;
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
	// NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
	// Safe: DEBUG_ERR macro uses fprintf with fixed format string
	DEBUG_ERR("Error creating pool");
	exit(1);
    }
}

static void teardown(void)
{
    apr_pool_destroy(pool);
}

START_TEST(test_napr_hash_basic)
{
    napr_hash_t *hash;
    apr_uint32_t hash_value;
    char *key1 = apr_pstrdup(pool, "key1");
    char *key2 = apr_pstrdup(pool, "key2");
    char *result;

    /* Create a string hash table - for string hash, the data IS the key */
    hash = napr_hash_str_make(pool, 16, 4);
    ck_assert_ptr_ne(hash, NULL);

    /* Test insertion and search */
    result = napr_hash_search(hash, key1, strlen(key1), &hash_value);
    ck_assert_ptr_eq(result, NULL);

    apr_status_t status = napr_hash_set(hash, key1, hash_value);
    ck_assert_int_eq(status, APR_SUCCESS);

    result = napr_hash_search(hash, key1, strlen(key1), NULL);
    ck_assert_ptr_eq(result, key1);

    /* Test another key */
    result = napr_hash_search(hash, key2, strlen(key2), &hash_value);
    ck_assert_ptr_eq(result, NULL);
    status = napr_hash_set(hash, key2, hash_value);
    ck_assert_int_eq(status, APR_SUCCESS);

    result = napr_hash_search(hash, key2, strlen(key2), NULL);
    ck_assert_ptr_eq(result, key2);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_napr_hash_rebuild)
{
    napr_hash_t *hash;
    apr_uint32_t hash_value;
    char **keys;
    int i;

    /* Create hash with small initial size and low fill factor to trigger rebuild */
    hash = napr_hash_str_make(pool, 2, 2);	/* 2 buckets, 2 items per bucket */
    ck_assert_ptr_ne(hash, NULL);

    /* Allocate keys array */
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: apr_pcalloc is APR library macro with proper size calculation
    keys = apr_pcalloc(pool, 50 * sizeof(char *));

    /* Insert enough items to force a rebuild */
    for (i = 0; i < 50; i++) {
	keys[i] = apr_psprintf(pool, "key_%d", i);

	void *result = napr_hash_search(hash, keys[i], strlen(keys[i]), &hash_value);
	ck_assert_ptr_eq(result, NULL);

	apr_status_t status = napr_hash_set(hash, keys[i], hash_value);
	ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Verify all items are still accessible after rebuild */
    for (i = 0; i < 50; i++) {
	char *result = napr_hash_search(hash, keys[i], strlen(keys[i]), NULL);
	ck_assert_ptr_ne(result, NULL);
	ck_assert_str_eq(result, keys[i]);
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_napr_hash_remove_multiple)
{
    napr_hash_t *hash;
    apr_uint32_t hash_values[10];
    char **keys;
    int i;

    /* Create hash with very low fill factor to force collisions */
    hash = napr_hash_str_make(pool, 1, 10);	/* 1 bucket, 10 items capacity */
    ck_assert_ptr_ne(hash, NULL);

    /* Allocate keys array */
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: apr_pcalloc is APR library macro with proper size calculation
    keys = apr_pcalloc(pool, 10 * sizeof(char *));

    /* Insert multiple items that will collide in the same bucket */
    for (i = 0; i < 5; i++) {
	keys[i] = apr_psprintf(pool, "key_%d", i);

	void *result = napr_hash_search(hash, keys[i], strlen(keys[i]), &hash_values[i]);
	ck_assert_ptr_eq(result, NULL);

	apr_status_t status = napr_hash_set(hash, keys[i], hash_values[i]);
	ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Remove middle element (not the last one in bucket) */
    napr_hash_remove(hash, keys[1], hash_values[1]);

    /* Verify the removed item is gone */
    char *result = napr_hash_search(hash, keys[1], strlen(keys[1]), NULL);
    ck_assert_ptr_eq(result, NULL);

    /* Verify other items still exist */
    result = napr_hash_search(hash, keys[0], strlen(keys[0]), NULL);
    ck_assert_ptr_eq(result, keys[0]);
    result = napr_hash_search(hash, keys[2], strlen(keys[2]), NULL);
    ck_assert_ptr_eq(result, keys[2]);
    result = napr_hash_search(hash, keys[4], strlen(keys[4]), NULL);
    ck_assert_ptr_eq(result, keys[4]);

    /* Remove last element */
    napr_hash_remove(hash, keys[4], hash_values[4]);
    result = napr_hash_search(hash, keys[4], strlen(keys[4]), NULL);
    ck_assert_ptr_eq(result, NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_napr_hash_iterator_multiple_elements)
{
    napr_hash_t *hash;
    apr_uint32_t hash_value;
    char **keys;
    int i, count;
    napr_hash_index_t *hi;

    /* Create hash with configuration that promotes collisions */
    hash = napr_hash_str_make(pool, 2, 5);	/* 2 buckets, 5 items per bucket */
    ck_assert_ptr_ne(hash, NULL);

    /* Allocate keys array */
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: apr_pcalloc is APR library macro with proper size calculation
    keys = apr_pcalloc(pool, 10 * sizeof(char *));

    /* Insert multiple items */
    for (i = 0; i < 8; i++) {
	keys[i] = apr_psprintf(pool, "key_%d", i);

	void *result = napr_hash_search(hash, keys[i], strlen(keys[i]), &hash_value);
	ck_assert_ptr_eq(result, NULL);

	apr_status_t status = napr_hash_set(hash, keys[i], hash_value);
	ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Iterate through all elements */
    count = 0;
    for (hi = napr_hash_first(pool, hash); hi; hi = napr_hash_next(hi)) {
	const void *key;
	apr_size_t klen;
	void *val;
	napr_hash_this(hi, &key, &klen, &val);
	ck_assert_ptr_ne(val, NULL);
	count++;
    }

    ck_assert_int_eq(count, 8);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_napr_hash_pool_get)
{
    napr_hash_t *hash;
    apr_pool_t *hash_pool;

    hash = napr_hash_str_make(pool, 16, 4);
    ck_assert_ptr_ne(hash, NULL);

    /* Test napr_hash_pool_get */
    hash_pool = napr_hash_pool_get(hash);
    ck_assert_ptr_eq(hash_pool, pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_napr_hash_iterator_empty_buckets)
{
    napr_hash_t *hash;
    apr_uint32_t hash_value;
    napr_hash_index_t *hi;
    int count;

    /* Create hash with larger size to ensure empty buckets */
    hash = napr_hash_str_make(pool, 128, 4);
    ck_assert_ptr_ne(hash, NULL);

    /* Insert just a few items so many buckets remain empty */
    char *key1 = apr_pstrdup(pool, "key1");
    char *key2 = apr_pstrdup(pool, "key2");

    napr_hash_search(hash, key1, strlen(key1), &hash_value);
    napr_hash_set(hash, key1, hash_value);

    napr_hash_search(hash, key2, strlen(key2), &hash_value);
    napr_hash_set(hash, key2, hash_value);

    /* Iterate - should skip empty buckets */
    count = 0;
    for (hi = napr_hash_first(pool, hash); hi; hi = napr_hash_next(hi)) {
	void *val;
	napr_hash_this(hi, NULL, NULL, &val);
	ck_assert_ptr_ne(val, NULL);
	count++;
    }

    ck_assert_int_eq(count, 2);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_napr_hash_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Napr_Hash");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_napr_hash_basic);
    tcase_add_test(tc_core, test_napr_hash_rebuild);
    tcase_add_test(tc_core, test_napr_hash_remove_multiple);
    tcase_add_test(tc_core, test_napr_hash_iterator_multiple_elements);
    tcase_add_test(tc_core, test_napr_hash_pool_get);
    tcase_add_test(tc_core, test_napr_hash_iterator_empty_buckets);
    suite_add_tcase(s, tc_core);

    return s;
}
