/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
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
#include <string.h>
#include <ftwin.h>
#include <ft_report.h>
#include <ft_config.h>
#include <ft_types.h>

START_TEST(test_chksum_cmp)
{
    ft_file_t file1 = {.path = "file1",.prioritized = 0 };
    ft_file_t file2 = {.path = "file2",.prioritized = 0 };

    ft_chksum_t chk1 = {.file = &file1 };
    ft_chksum_t chk2 = {.file = &file2 };

    // Set hash1 > hash2
    chk1.hash_value.high64 = 0x2;
    chk1.hash_value.low64 = 0;
    chk2.hash_value.high64 = 0x1;
    chk2.hash_value.low64 = 0;

    ck_assert_int_gt(ft_chksum_cmp(&chk1, &chk2), 0);

    // Set hash1 == hash2
    chk2.hash_value.high64 = 0x2;
    chk2.hash_value.low64 = 0;
    ck_assert_int_eq(ft_chksum_cmp(&chk1, &chk2), 0);

    // Same hash, but file2 is prioritized
    file2.prioritized = 1;
    ck_assert_int_gt(ft_chksum_cmp(&chk1, &chk2), 0);
}

END_TEST Suite *make_ft_report_suite(void)
{
    Suite *suite = suite_create("Report");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_chksum_cmp);

    suite_add_tcase(suite, tc_core);
    return suite;
}
