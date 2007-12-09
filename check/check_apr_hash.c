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

#include <apr_hash.h>
#include <apr_strings.h>

#include "debug.h"

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

/* http://www.burtleburtle.net/bob/hash/integer.html */
static unsigned int hash_int(const char *key, apr_ssize_t * klen)
{
    apr_off_t i = *(apr_off_t *) key;

    printf("b:%" APR_OFF_T_FMT "\n", i);
    /* We take a bad hash_func to collide numbers */
    i = (i & 0x0000000f);
    printf("a:%" APR_OFF_T_FMT "\n", i);

    return i;
}

START_TEST(test_apr_hash_int)
{
    apr_off_t array[] = { 0x00000001, 0x00000002, 0x00000004, 0x10000001, 0x00000004 };
    const char *array_str[] = { "1", "2", "3", "4", "5" };
    apr_hash_t *hash;
    int i;

    hash = apr_hash_make_custom(pool, hash_int);
    for (i = 0; i < 5; i++) {
	apr_hash_set(hash, &(array[i]), sizeof(apr_off_t), array_str[i]);
    }
    for (i = 0; i < 5; i++) {
	printf("%s\n", (char *) apr_hash_get(hash, &(array[i]), sizeof(apr_off_t)));
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_apr_hash_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Apr_Hash");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_apr_hash_int);
    suite_add_tcase(s, tc_core);

    return s;
}
