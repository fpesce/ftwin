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

#include <apr_file_io.h>

#include "checksum.h"
#include "debug.h"
#include "ft_file.h"

static apr_pool_t *main_pool = NULL;
static apr_pool_t *pool = NULL;
static const apr_off_t SIZE_16K = 16384;
static const apr_off_t SIZE_1K = 1024;
static const apr_off_t SIZE_5K = 5120;
static const apr_size_t BUFFER_SIZE_1K = 1024;

static void setup(void)
{
    apr_status_t status = APR_SUCCESS;

    if (main_pool == NULL) {
	(void) apr_initialize();
	(void) atexit(apr_terminate);
	status = apr_pool_create(&main_pool, NULL);
	if (status != APR_SUCCESS) {
	    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
	    // Safe: DEBUG_ERR macro uses fprintf with fixed format string
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

static const char *const FNAME_1 = CHECK_DIR "/tests/truerand";
static const apr_off_t FILE_SIZE_1 = SIZE_16K;
static const char *const FNAME_2 = CHECK_DIR "/tests/copyrand";
static const char *const FNAME_3 = CHECK_DIR "/tests/testrand";
static const char *const FNAME_1K = CHECK_DIR "/tests/1K_file";
static const apr_off_t FILE_SIZE_1K = SIZE_1K;
static const char *const FNAME_5K = CHECK_DIR "/tests/5K_file";
static const apr_off_t FILE_SIZE_5K = SIZE_5K;

START_TEST(test_checksum_empty_file)
{
    apr_status_t status = APR_SUCCESS;
    ft_hash_t hash1;
    ft_hash_t hash2;
    apr_file_t *empty_file = NULL;
    const char *empty_fname = CHECK_DIR "/tests/empty_file";
    int return_value = 0;

    /* Create empty file for testing */
    status = apr_file_open(&empty_file, empty_fname, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    (void) apr_file_close(empty_file);

    /* Test checksum of empty file with small path */
    status = checksum_file(empty_fname, 0, BUFFER_SIZE_1K, &hash1, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test checksum of empty file with big path */
    status = checksum_file(empty_fname, 0, 0, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Both should produce same hash */
    return_value = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_eq(return_value, 0);

    /* Clean up */
    (void) apr_file_remove(empty_fname, pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_checksum_small_files)
{
    apr_status_t status = APR_SUCCESS;
    ft_hash_t hash_1k_small;
    ft_hash_t hash_1k_big;
    ft_hash_t hash_5k_small;
    ft_hash_t hash_5k_big;
    int return_value = 0;

    /* Test 1K file with small file path (excess_size > file size) */
    status = checksum_file(FNAME_1K, FILE_SIZE_1K, FILE_SIZE_1K * 2, &hash_1k_small, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test 1K file with big file path (excess_size < file size) */
    status = checksum_file(FNAME_1K, FILE_SIZE_1K, FILE_SIZE_1K / 2, &hash_1k_big, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Both paths should produce same hash */
    return_value = memcmp(&hash_1k_small, &hash_1k_big, sizeof(ft_hash_t));
    ck_assert_int_eq(return_value, 0);

    /* Test 5K file with small file path */
    status = checksum_file(FNAME_5K, FILE_SIZE_5K, FILE_SIZE_5K * 2, &hash_5k_small, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test 5K file with big file path */
    status = checksum_file(FNAME_5K, FILE_SIZE_5K, FILE_SIZE_5K / 2, &hash_5k_big, pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Both paths should produce same hash */
    return_value = memcmp(&hash_5k_small, &hash_5k_big, sizeof(ft_hash_t));
    ck_assert_int_eq(return_value, 0);

    /* 1K and 5K files should have different hashes */
    return_value = memcmp(&hash_1k_small, &hash_5k_small, sizeof(ft_hash_t));
    ck_assert_int_ne(return_value, 0);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_checksum_file)
{
    apr_status_t status = APR_SUCCESS;
    ft_hash_t hash1;
    ft_hash_t hash2;
    int return_value = 0;

    status = checksum_file(FNAME_1, FILE_SIZE_1, 2 * FILE_SIZE_1, &hash1, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = checksum_file(FNAME_2, FILE_SIZE_1, 2 * FILE_SIZE_1, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    return_value = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_eq(return_value, 0);

    status = checksum_file(FNAME_3, FILE_SIZE_1, 2 * FILE_SIZE_1, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    return_value = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_ne(return_value, 0);

    status = checksum_file(FNAME_1, FILE_SIZE_1, FILE_SIZE_1 / 2, &hash1, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = checksum_file(FNAME_2, FILE_SIZE_1, FILE_SIZE_1 / 2, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    return_value = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_eq(return_value, 0);

    status = checksum_file(FNAME_3, FILE_SIZE_1, FILE_SIZE_1 / 2, &hash2, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    return_value = memcmp(&hash1, &hash2, sizeof(ft_hash_t));
    ck_assert_int_ne(return_value, 0);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_filecmp)
{
    int return_value = 0;
    apr_status_t status = APR_SUCCESS;

    status = filecmp(pool, FNAME_1, FNAME_2, FILE_SIZE_1, 2 * FILE_SIZE_1, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(return_value, 0);
    status = filecmp(pool, FNAME_1, FNAME_2, FILE_SIZE_1, FILE_SIZE_1 / 2, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(return_value, 0);

    status = filecmp(pool, FNAME_1, FNAME_3, FILE_SIZE_1, FILE_SIZE_1 / 2, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_ne(return_value, 0);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_filecmp_empty)
{
    int return_value = 0;
    apr_status_t status = APR_SUCCESS;
    apr_file_t *empty1 = NULL;
    apr_file_t *empty2 = NULL;
    const char *empty_fname1 = CHECK_DIR "/tests/empty1";
    const char *empty_fname2 = CHECK_DIR "/tests/empty2";

    /* Create two empty files */
    status = apr_file_open(&empty1, empty_fname1, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    (void) apr_file_close(empty1);

    status = apr_file_open(&empty2, empty_fname2, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    (void) apr_file_close(empty2);

    /* Compare empty files with small path */
    status = filecmp(pool, empty_fname1, empty_fname2, 0, (apr_off_t) BUFFER_SIZE_1K, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(return_value, 0);

    /* Compare empty files with big path */
    status = filecmp(pool, empty_fname1, empty_fname2, 0, 0, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(return_value, 0);

    /* Clean up */
    (void) apr_file_remove(empty_fname1, pool);
    (void) apr_file_remove(empty_fname2, pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_filecmp_small_files)
{
    int return_value = 0;
    apr_status_t status = APR_SUCCESS;
    apr_file_t *small1 = NULL;
    apr_file_t *small2 = NULL;
    apr_file_t *small3 = NULL;
    const char *small_fname1 = CHECK_DIR "/tests/small1";
    const char *small_fname2 = CHECK_DIR "/tests/small2";
    const char *small_fname3 = CHECK_DIR "/tests/small3";
    const char test_data1[] = "Hello, World!";
    const char test_data2[] = "Hello, World!";
    const char test_data3[] = "Goodbye!";
    apr_size_t len = 0;

    /* Create small test file 1 */
    status = apr_file_open(&small1, small_fname1, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    len = sizeof(test_data1) - 1;
    (void) apr_file_write(small1, test_data1, &len);
    (void) apr_file_close(small1);

    /* Create small test file 2 (identical content) */
    status = apr_file_open(&small2, small_fname2, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    len = sizeof(test_data2) - 1;
    (void) apr_file_write(small2, test_data2, &len);
    (void) apr_file_close(small2);

    /* Create small test file 3 (different content) */
    status = apr_file_open(&small3, small_fname3, APR_CREATE | APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    len = sizeof(test_data3) - 1;
    (void) apr_file_write(small3, test_data3, &len);
    (void) apr_file_close(small3);

    /* Compare identical small files with small path (mmap) */
    apr_off_t small_size = sizeof(test_data1) - 1;
    status = filecmp(pool, small_fname1, small_fname2, small_size, small_size * 2, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(return_value, 0);

    /* Compare different small files with small path (mmap) */
    status = filecmp(pool, small_fname1, small_fname3, small_size, small_size * 2, &return_value);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_ne(return_value, 0);

    /* Clean up */
    (void) apr_file_remove(small_fname1, pool);
    (void) apr_file_remove(small_fname2, pool);
    (void) apr_file_remove(small_fname3, pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_file_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_core = NULL;
    suite = suite_create("Ft_File");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_checksum_empty_file);
    tcase_add_test(tc_core, test_checksum_small_files);
    tcase_add_test(tc_core, test_checksum_file);
    tcase_add_test(tc_core, test_filecmp_empty);
    tcase_add_test(tc_core, test_filecmp_small_files);
    tcase_add_test(tc_core, test_filecmp);
    suite_add_tcase(suite, tc_core);

    return suite;
}
