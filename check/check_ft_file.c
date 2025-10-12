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

#include "checksum.h"
#include "debug.h"
#include "ft_file.h"

extern apr_pool_t *main_pool;
static apr_pool_t *pool;

static void setup(void)
{
    apr_status_t rs;

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

static const char *fname1 = CHECK_DIR "/tests/truerand";
static apr_off_t size1 = 16384;
static const char *fname2 = CHECK_DIR "/tests/copyrand";
static const char *fname3 = CHECK_DIR "/tests/testrand";

START_TEST(test_checksum_file)
{
    apr_status_t status;
    ft_hash_t hash1, hash2;
    int rv;

    status = checksum_file(fname1, size1, 2 * size1, &hash1, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = checksum_file(fname2, size1, 2 * size1, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    rv = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_eq(rv, 0);

    status = checksum_file(fname3, size1, 2 * size1, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    rv = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_ne(rv, 0);

    status = checksum_file(fname1, size1, size1 / 2, &hash1, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = checksum_file(fname2, size1, size1 / 2, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    rv = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_eq(rv, 0);

    status = checksum_file(fname3, size1, size1 / 2, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    rv = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_ne(rv, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_filecmp)
{
    int rv;
    apr_status_t status;

    status = filecmp(pool, fname1, fname2, size1, 2 * size1, &rv);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(rv, 0);
    status = filecmp(pool, fname1, fname2, size1, size1 / 2, &rv);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(rv, 0);

    status = filecmp(pool, fname1, fname3, size1, size1 / 2, &rv);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_ne(rv, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_file_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Ft_File");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_checksum_file);
    tcase_add_test(tc_core, test_filecmp);
    suite_add_tcase(s, tc_core);

    return s;
}
