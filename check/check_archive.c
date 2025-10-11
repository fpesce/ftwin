/*
 * Copyright (C) 2023 Jules
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

static void touch(const char* filename)
{
    apr_file_t* file;
    apr_file_open(&file, filename, APR_FOPEN_CREATE | APR_FOPEN_WRITE, APR_FPROT_OS_DEFAULT, pool);
    apr_file_close(file);
}

START_TEST(test_archive_regex)
{
    int stdout_bk;
    FILE* tmp;
    char buf[1024] = {0};

    touch("test.zip");
    touch("test.rar");
    touch("test.7z");
    touch("test.tar.gz");
    touch("test.tgz");
    touch("test.notanarchive");

    stdout_bk = dup(STDOUT_FILENO);
    tmp = freopen("tmp.out", "w", stdout);

    const char *argv[] = {"ftwin", "-t", "test.zip", "test.rar", "test.7z", "test.tar.gz", "test.tgz", "test.notanarchive", NULL};
    int argc = sizeof(argv) / sizeof(argv[0]) - 1;
    ftwin_main(argc, argv);

    fflush(stdout);
    fclose(tmp);
    dup2(stdout_bk, STDOUT_FILENO);

    FILE* f = fopen("tmp.out", "r");
    fread(buf, 1, sizeof(buf), f);
    fclose(f);

    ck_assert_str_eq(buf, "Please submit at least two files...\n");
}
END_TEST

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