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

#include <check.h>
#include <apr.h>
#include <apr_pools.h>
#include <stdint.h>
#include "key_hash.h"

START_TEST(test_64bit_key_truncation)
{
    apr_pool_t *pool = NULL;
    apr_initialize();
    apr_pool_create(&pool, NULL);

    uint64_t val1 = 1024;
    uint64_t val2 = 1024 + (1ULL << 32); // val2 has the same lower 32 bits as val1

    /* Test the buggy 32-bit functions - we expect them to collide */
    apr_uint32_t hash1_buggy = apr_uint32_key_hash(&val1, sizeof(uint32_t));
    apr_uint32_t hash2_buggy = apr_uint32_key_hash(&val2, sizeof(uint32_t));
    int cmp_buggy = apr_uint32_key_cmp(&val1, &val2, sizeof(uint32_t));

    ck_assert_uint_eq(hash1_buggy, hash2_buggy);
    ck_assert_int_eq(cmp_buggy, 0);

    /* Test the corrected 64-bit functions - we expect them to be different */
    apr_uint32_t hash1_correct = apr_off_t_key_hash(&val1, sizeof(uint64_t));
    apr_uint32_t hash2_correct = apr_off_t_key_hash(&val2, sizeof(uint64_t));
    int cmp_correct = apr_off_t_key_cmp(&val1, &val2, sizeof(uint64_t));

    ck_assert_uint_ne(hash1_correct, hash2_correct);
    ck_assert_int_ne(cmp_correct, 0);

    apr_pool_destroy(pool);
    apr_terminate();
}
END_TEST

Suite *make_key_hash_suite(void)
{
    Suite *s = suite_create("KeyHash");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_64bit_key_truncation);
    suite_add_tcase(s, tc_core);
    return s;
}