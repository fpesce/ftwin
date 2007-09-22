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
apr_pool_t *pool;

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
    apr_uint32_t val_array[HASHSTATE];
    apr_uint32_t val_array2[HASHSTATE];
    int rv;

    status = checksum_file(fname1, size1, 2 * size1, val_array, pool);
    fail_unless(APR_SUCCESS == status, "checksum small file failed");
    status = checksum_file(fname2, size1, 2 * size1, val_array2, pool);
    fail_unless(APR_SUCCESS == status, "checksum big file failed");
    rv = memcmp(val_array, val_array2, HASHSTATE);
    fail_unless(0 == rv, "mismatching checksums");

    status = checksum_file(fname3, size1, 2 * size1, val_array2, pool);
    fail_unless(APR_SUCCESS == status, "checksum big file failed");
    rv = memcmp(val_array, val_array2, HASHSTATE);
    fail_unless(0 != rv, "unexpected matching checksums");

    status = checksum_file(fname1, size1, size1 / 2, val_array, pool);
    fail_unless(APR_SUCCESS == status, "checksum small file failed");
    status = checksum_file(fname2, size1, size1 / 2, val_array2, pool);
    fail_unless(APR_SUCCESS == status, "checksum big file failed");
    rv = memcmp(val_array, val_array2, HASHSTATE);
    fail_unless(0 == rv, "mismatching checksums");

    status = checksum_file(fname3, size1, size1 / 2, val_array2, pool);
    fail_unless(APR_SUCCESS == status, "checksum big file failed");
    rv = memcmp(val_array, val_array2, HASHSTATE);
    fail_unless(0 != rv, "unexpected matching checksums");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_filecmp)
{
    int rv;
    apr_status_t status;

    status = filecmp(pool, fname1, fname2, size1, 2 * size1, &rv);
    fail_unless(APR_SUCCESS == status, "filecmp small file failed");
    status = filecmp(pool, fname1, fname2, size1, size1 / 2, &rv);
    fail_unless((APR_SUCCESS == status) && (0 == rv), "filecmp big file failed");

    status = filecmp(pool, fname1, fname3, size1, size1 / 2, &rv);
    fail_unless((APR_SUCCESS == status) && (0 != rv), "filecmp big file failed");
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
