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

#include <stdlib.h>
#include <check.h>
#include <stdio.h>

#include <apr.h>
#include <apr_pools.h>
#ifdef HAVE_CONFIG_H
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_BUGREPORT
#include "config.h"
#endif
#include "ftwin.h"
#include <unistd.h>

apr_pool_t *main_pool = NULL;

static char *capture_output(int fd)
{
    static char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    read(fd, buffer, sizeof(buffer) - 1);
    return buffer;
}

START_TEST(test_ftwin_size_options)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    system("cp check/tests/5K_file check/tests/5K_file_copy");

    const char *argv[] =
	{ "ftwin", "-m", "2K", "-M", "8K", "check/tests/1K_file", "check/tests/5K_file", "check/tests/10K_file",
"check/tests/5K_file_copy" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file"), NULL);
    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file_copy"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/1K_file"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/10K_file"), NULL);

    remove("check/tests/5K_file_copy");
}

END_TEST Suite * make_ftwin_suite(void)
{
    Suite *s = suite_create("Ftwin");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_ftwin_size_options);

    suite_add_tcase(s, tc_core);

    return s;
}

Suite *make_napr_heap_suite(void);
Suite *make_apr_hash_suite(void);
Suite *make_ft_file_suite(void);
Suite *make_archive_suite(void);
Suite *make_human_size_suite(void);

int main(int argc, char **argv)
{
    char buf[256];
    int nf;
    apr_status_t status;
    SRunner *sr;
    int num = 0;

    if (argc > 1) {
	num = atoi(argv[1]);
    }

    status = apr_initialize();
    if (APR_SUCCESS != status) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    atexit(apr_terminate);

    if ((status = apr_pool_create(&main_pool, NULL)) != APR_SUCCESS) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    sr = srunner_create(NULL);

    if (!num || num == 1)
	srunner_add_suite(sr, make_napr_heap_suite());

    if (!num || num == 2)
	srunner_add_suite(sr, make_apr_hash_suite());

    if (!num || num == 3)
	srunner_add_suite(sr, make_ft_file_suite());

    if (!num || num == 4)
	srunner_add_suite(sr, make_archive_suite());

    if (!num || num == 5)
	srunner_add_suite(sr, make_human_size_suite());

    if (!num || num == 6)
	srunner_add_suite(sr, make_ftwin_suite());

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_set_xml(sr, "check_log.xml");

    srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    apr_terminate();
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
