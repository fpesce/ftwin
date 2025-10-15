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

#include <stdlib.h>
#include <check.h>

#include "ft_system.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_get_cpu_cores)
{
    unsigned int core_count = ft_get_cpu_cores();
    const unsigned int MIN_CORES = 1;
    const unsigned int MAX_CORES = 256;

    /* Should return at least 1 core, and at most a reasonable number (256) */
    ck_assert_uint_ge(core_count, MIN_CORES);
    ck_assert_uint_le(core_count, MAX_CORES);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_system_suite(void)
{
    Suite *suite = suite_create("System");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_get_cpu_cores);

    suite_add_tcase(suite, tc_core);

    return suite;
}
