/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law of a-zing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <check.h>
#include <ftwin.h>
#include <ft_report_json.h>
#include <ft_config.h>

// Placeholder test - the JSON reporting functions operate on complex
// internal state that's difficult to test in isolation
START_TEST(test_json_placeholder)
{
    // This test suite exists but needs real test data to be meaningful
    ck_assert_int_eq(1, 1);
}

END_TEST Suite *make_ft_report_json_suite(void)
{
    Suite *suite = suite_create("ReportJSON");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_json_placeholder);

    suite_add_tcase(suite, tc_core);
    return suite;
}
