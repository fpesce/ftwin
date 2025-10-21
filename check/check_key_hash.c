/*
 * Copyright (C) 2024 Francois Pesce : francois.pesce (at) gmail (dot) com
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
#include "key_hash.h"

static const apr_off_t VAL_OFF_TEN = 10;
static const apr_off_t VAL_OFF_TWENTY = 20;
static const gid_t GID_HUNDRED = 100;
static const gid_t GID_TWO_HUNDRED = 200;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_apr_off_t_key_cmp_less)
{
    apr_off_t val1 = VAL_OFF_TEN;
    apr_off_t val2 = VAL_OFF_TWENTY;
    int result = apr_off_t_key_cmp(&val1, &val2, sizeof(apr_off_t));
    ck_assert_int_lt(result, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_gid_t_key_cmp_less)
{
    gid_t val1 = GID_HUNDRED;
    gid_t val2 = GID_TWO_HUNDRED;
    int result = gid_t_key_cmp(&val1, &val2, sizeof(gid_t));
    ck_assert_int_lt(result, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_key_hash_suite(void)
{
    Suite *suite = suite_create("KeyHash");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_apr_off_t_key_cmp_less);
    tcase_add_test(tc_core, test_gid_t_key_cmp_less);
    suite_add_tcase(suite, tc_core);
    return suite;
}
