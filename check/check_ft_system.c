/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <check.h>
#include "ft_system.h"

START_TEST(test_get_cpu_cores)
{
    unsigned int num_cores = ft_get_cpu_cores();
    /* We can't know the exact number of cores, but it should be at least 1.
     * The function has a fallback of 4, so this should always pass.
     */
    ck_assert_uint_gt(num_cores, 0);
}
END_TEST

Suite *make_ft_system_suite(void)
{
    Suite *s = suite_create("System");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_get_cpu_cores);
    suite_add_tcase(s, tc_core);
    return s;
}