/*
 * Copyright (C) 2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

static const int INITIAL_HASH_SIZE = 16;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *main_pool = NULL;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *pool = NULL;

static void setup(void)
{
    apr_status_t status = APR_SUCCESS;

    if (main_pool == NULL) {
        status = apr_initialize();
        if (status != APR_SUCCESS) {
            DEBUG_ERR("Error initializing APR");
            exit(1);
        }
        (void) atexit(apr_terminate);
        status = apr_pool_create(&main_pool, NULL);
        if (status != APR_SUCCESS) {
            DEBUG_ERR("Error creating main_pool");
            exit(1);
        }
    }
    status = apr_pool_create(&pool, main_pool);
    if (status != APR_SUCCESS) {
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

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_basic)
{
    napr_hash_t *hash = NULL;
    apr_uint32_t hash_value = 0;
    char *key1 = apr_pstrdup(pool, "key1");
    char *key2 = apr_pstrdup(pool, "key2");
    char *result = NULL;
    apr_status_t status = APR_SUCCESS;

    /* Create a string hash table - for string hash, the data IS the key */
    hash = napr_hash_str_make(pool, INITIAL_HASH_SIZE, 4);
    ck_assert_ptr_ne(hash, NULL);

    /* Test insertion and search */
    result = napr_hash_search(hash, key1, strlen(key1), &hash_value);
    ck_assert_ptr_eq(result, NULL);

    status = napr_hash_set(hash, key1, hash_value);
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

static void populate_hash_for_rebuild_test(napr_hash_t *hash, char **keys, int num_keys)
{
    apr_uint32_t hash_value = 0;
    void *result = NULL;
    apr_status_t status = APR_SUCCESS;

    for (int index = 0; index < num_keys; index++) {
        keys[index] = apr_psprintf(pool, "key_%d", index);

        result = napr_hash_search(hash, keys[index], strlen(keys[index]), &hash_value);
        ck_assert_ptr_eq(result, NULL);

        status = napr_hash_set(hash, keys[index], hash_value);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

static void verify_hash_after_rebuild(napr_hash_t *hash, char **keys, int num_keys)
{
    for (int index = 0; index < num_keys; index++) {
        char *result = napr_hash_search(hash, keys[index], strlen(keys[index]), NULL);
        ck_assert_ptr_ne(result, NULL);
        ck_assert_str_eq(result, keys[index]);
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_rebuild)
{
    const int num_rebuild_keys = 50;
    napr_hash_t *hash = NULL;
    char **keys = NULL;

    /* Create hash with small initial size and low fill factor to trigger rebuild */
    hash = napr_hash_str_make(pool, 2, 2);      /* 2 buckets, 2 items per bucket */
    ck_assert_ptr_ne(hash, NULL);

    /* Allocate keys array */
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: apr_pcalloc is APR library macro with proper size calculation
    keys = (char **) apr_pcalloc(pool, num_rebuild_keys * sizeof(char *));

    /* Insert enough items to force a rebuild */
    populate_hash_for_rebuild_test(hash, keys, num_rebuild_keys);

    /* Verify all items are still accessible after rebuild */
    verify_hash_after_rebuild(hash, keys, num_rebuild_keys);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

static void populate_hash_for_removal_test(napr_hash_t *hash, char **keys, apr_uint32_t *hash_values, int num_keys)
{
    void *result = NULL;
    apr_status_t status = APR_SUCCESS;

    for (int index = 0; index < num_keys; index++) {
        keys[index] = apr_psprintf(pool, "key_%d", index);
        result = napr_hash_search(hash, keys[index], strlen(keys[index]), &hash_values[index]);
        ck_assert_ptr_eq(result, NULL);
        status = napr_hash_set(hash, keys[index], hash_values[index]);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

static void verify_removal(napr_hash_t *hash, char **keys, const apr_uint32_t *hash_values)
{
    void *result = NULL;

    /* Remove middle element */
    napr_hash_remove(hash, keys[1], hash_values[1]);
    result = napr_hash_search(hash, keys[1], strlen(keys[1]), NULL);
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

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_remove_multiple)
{
    const int HASH_VALUES_COUNT = 10;
    const int LOOP_LIMIT = 5;
    napr_hash_t *hash = NULL;
    apr_uint32_t hash_values[HASH_VALUES_COUNT];
    char **keys = NULL;

    memset(hash_values, 0, sizeof(hash_values));
    hash = napr_hash_str_make(pool, 1, HASH_VALUES_COUNT);
    ck_assert_ptr_ne(hash, NULL);

    keys = (char **) apr_pcalloc(pool, HASH_VALUES_COUNT * sizeof(char *));
    populate_hash_for_removal_test(hash, keys, hash_values, LOOP_LIMIT);
    verify_removal(hash, keys, hash_values);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_iterator_multiple_elements)
{
    napr_hash_t *hash = NULL;
    apr_uint32_t hash_value = 0;
    char **keys = NULL;
    int index = 0;
    int count = 0;
    napr_hash_index_t *hash_iterator = NULL;
    void *result = NULL;
    apr_status_t status = APR_SUCCESS;

    const int ITEMS_PER_BUCKET = 5;
    const int KEYS_COUNT = 10;
    const int LOOP_LIMIT = 8;

    /* Create hash with configuration that promotes collisions */
    hash = napr_hash_str_make(pool, 2, ITEMS_PER_BUCKET);       /* 2 buckets, 5 items per bucket */
    ck_assert_ptr_ne(hash, NULL);

    /* Allocate keys array */
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: apr_pcalloc is APR library macro with proper size calculation
    keys = (char **) apr_pcalloc(pool, KEYS_COUNT * sizeof(char *));

    /* Insert multiple items */
    for (index = 0; index < LOOP_LIMIT; index++) {
        keys[index] = apr_psprintf(pool, "key_%d", index);

        result = napr_hash_search(hash, keys[index], strlen(keys[index]), &hash_value);
        ck_assert_ptr_eq(result, NULL);

        status = napr_hash_set(hash, keys[index], hash_value);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Iterate through all elements */
    count = 0;
    for (hash_iterator = napr_hash_first(pool, hash); hash_iterator; hash_iterator = napr_hash_next(hash_iterator)) {
        const void *key = NULL;
        apr_size_t klen = 0;
        void *val = NULL;
        napr_hash_this(hash_iterator, &key, &klen, &val);
        ck_assert_ptr_ne(val, NULL);
        count++;
    }

    ck_assert_int_eq(count, 8);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_pool_get)
{
    napr_hash_t *hash = NULL;
    apr_pool_t *hash_pool = NULL;

    hash = napr_hash_str_make(pool, INITIAL_HASH_SIZE, 4);
    ck_assert_ptr_ne(hash, NULL);

    /* Test napr_hash_pool_get */
    hash_pool = napr_hash_pool_get(hash);
    ck_assert_ptr_eq(hash_pool, pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_napr_hash_iterator_empty_buckets)
{
    napr_hash_t *hash = NULL;
    apr_uint32_t hash_value = 0;
    napr_hash_index_t *hash_iterator = NULL;
    int count = 0;
    char *key1 = NULL;
    char *key2 = NULL;
    const int BUCKET_COUNT = 128;

    const int ITEMS_PER_BUCKET = 4;
    /* Create hash with larger size to ensure empty buckets */
    hash = napr_hash_str_make(pool, BUCKET_COUNT, ITEMS_PER_BUCKET);
    ck_assert_ptr_ne(hash, NULL);

    /* Insert just a few items so many buckets remain empty */
    key1 = apr_pstrdup(pool, "key1");
    key2 = apr_pstrdup(pool, "key2");

    napr_hash_search(hash, key1, strlen(key1), &hash_value);
    napr_hash_set(hash, key1, hash_value);

    napr_hash_search(hash, key2, strlen(key2), &hash_value);
    napr_hash_set(hash, key2, hash_value);

    /* Iterate - should skip empty buckets */
    count = 0;
    for (hash_iterator = napr_hash_first(pool, hash); hash_iterator; hash_iterator = napr_hash_next(hash_iterator)) {
        void *val = NULL;
        napr_hash_this(hash_iterator, NULL, NULL, &val);
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
    Suite *suite = NULL;
    TCase *tc_core = NULL;
    suite = suite_create("Napr_Hash");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_napr_hash_basic);
    tcase_add_test(tc_core, test_napr_hash_rebuild);
    tcase_add_test(tc_core, test_napr_hash_remove_multiple);
    tcase_add_test(tc_core, test_napr_hash_iterator_multiple_elements);
    tcase_add_test(tc_core, test_napr_hash_pool_get);
    tcase_add_test(tc_core, test_napr_hash_iterator_empty_buckets);
    suite_add_tcase(suite, tc_core);

    return suite;
}
