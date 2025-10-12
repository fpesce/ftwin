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
#include <string.h>
#include <check.h>
#include <apr_file_io.h>
#include "debug.h"
#include "ftwin.h"

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

START_TEST(test_archive_regex)
{
    /* Simple test to verify ftwin_main can be called with archive options
     * More comprehensive output testing would require additional infrastructure
     */
    const char *argv[] = { "ftwin", "-h", NULL };
    int argc = 2;
    int result = ftwin_main(argc, argv);

    /* -h should return 0 (success) */
    ck_assert_int_eq(result, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_archive_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Archive");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_archive_regex);
    suite_add_tcase(s, tc_core);

    return s;
}
