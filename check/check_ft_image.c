/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <check.h>
#include <apr_pools.h>

#include "ft_config.h"
#include "ft_image.h"
#include "napr_heap.h"
#include "ft_file.h"

enum
{
    OUTPUT_BUFFER_SIZE = 1024
};

static const double DEFAULT_THRESHOLD = 0.1;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern apr_pool_t *main_pool;

/* Forward declaration - defined in ft_config.c */
extern int ft_file_cmp(const void *param1, const void *param2);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_image_comparison)
{
    ft_conf_t *conf = NULL;
    apr_status_t status = APR_SUCCESS;
    ft_file_t *file1 = NULL;
    ft_file_t *file2 = NULL;
    ft_file_t *file3 = NULL;

    conf = ft_config_create(main_pool);
    ck_assert_ptr_ne(conf, NULL);

    /* Override default heap with our custom one for testing */
    conf->heap = napr_heap_make(main_pool, ft_file_cmp);
    conf->threshold = DEFAULT_THRESHOLD;
    conf->sep = ',';

    file1 = ft_file_make(main_pool, "check/tests/images/red.png", "check/tests/images/red.png");
    file2 = ft_file_make(main_pool, "check/tests/images/blue.png", "check/tests/images/blue.png");
    file3 = ft_file_make(main_pool, "check/tests/images/red_duplicate.png", "check/tests/images/red_duplicate.png");

    napr_heap_insert(conf->heap, file1);
    napr_heap_insert(conf->heap, file2);
    napr_heap_insert(conf->heap, file3);

    int stdout_pipe[2];
    ck_assert_int_eq(pipe(stdout_pipe), 0);

    int original_stdout = dup(STDOUT_FILENO);
    (void) dup2(stdout_pipe[1], STDOUT_FILENO);

    status = ft_image_twin_report(conf);
    ck_assert_int_eq(status, APR_SUCCESS);

    (void) close(stdout_pipe[1]);
    (void) dup2(original_stdout, STDOUT_FILENO);

    char buf[OUTPUT_BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    (void) read(stdout_pipe[0], buf, sizeof(buf) - 1);

    ck_assert_ptr_ne(strstr(buf, "red.png"), NULL);
    ck_assert_ptr_ne(strstr(buf, "red_duplicate.png"), NULL);
    ck_assert_ptr_eq(strstr(buf, "blue.png"), NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_image_suite(void)
{
    Suite *suite = suite_create("Image");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_image_comparison);
    suite_add_tcase(suite, tc_core);

    return suite;
}
