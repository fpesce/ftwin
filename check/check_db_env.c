/**
 * @file check_db_env.c
 * @brief Environment lifecycle tests for napr_db
 *
 * Tests database environment creation, initialization, file mapping,
 * meta page initialization, and meta page selection.
 */

#include <check.h>
#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include "../src/napr_db_internal.h"

/* Test fixtures */
static apr_pool_t *test_pool = NULL;
static const char *test_db_path = "/tmp/test_napr_db.db";

static void setup(void)
{
    apr_initialize();
    apr_pool_create(&test_pool, NULL);

    /* Remove any existing test database */
    apr_file_remove(test_db_path, test_pool);
}

static void teardown(void)
{
    /* Clean up test database */
    if (test_pool) {
        apr_file_remove(test_db_path, test_pool);
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/*
 * Test: Create, SetMapsize, Close lifecycle
 */
START_TEST(test_env_create_setmapsize_close)
{
    napr_db_env_t *env = NULL;
    apr_status_t status;

    /* Create environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(env);
    ck_assert_ptr_eq(env->pool, test_pool);
    ck_assert_int_eq(env->mapsize, 0);
    ck_assert_ptr_null(env->file);
    ck_assert_ptr_null(env->mmap);
    ck_assert_ptr_null(env->map_addr);

    /* Set mapsize */
    apr_size_t mapsize = 10 * 1024 * 1024;      /* 10 MB */
    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(env->mapsize, mapsize);

    /* Close (should be safe even without opening) */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}

END_TEST
/*
 * Test: Open new database (NAPR_DB_CREATE)
 * Verifies:
 * - File is created
 * - Meta Pages 0 and 1 are initialized
 * - Meta Page 0 has TXNID 0, Meta Page 1 has TXNID 1
 * - Both have correct magic, version, root, and last_pgno
 */
START_TEST(test_env_open_new_db)
{
    napr_db_env_t *env = NULL;
    apr_status_t status;
    apr_size_t mapsize = 1024 * 1024;   /* 1 MB */

    /* Create and configure environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Open new database with CREATE flag */
    status = napr_db_env_open(env, test_db_path, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify file handle and mmap are established */
    ck_assert_ptr_nonnull(env->file);
    ck_assert_ptr_nonnull(env->mmap);
    ck_assert_ptr_nonnull(env->map_addr);

    /* Verify meta page pointers are set */
    ck_assert_ptr_nonnull(env->meta0);
    ck_assert_ptr_nonnull(env->meta1);
    ck_assert_ptr_nonnull(env->live_meta);

    /* Verify meta0 points to page 0 of mmap */
    ck_assert_ptr_eq(env->meta0, (DB_MetaPage *) env->map_addr);

    /* Verify meta1 points to page 1 of mmap */
    ck_assert_ptr_eq(env->meta1, (DB_MetaPage *) ((char *) env->map_addr + PAGE_SIZE));

    /* Verify Meta Page 0 initialization */
    ck_assert_uint_eq(env->meta0->magic, DB_MAGIC);
    ck_assert_uint_eq(env->meta0->version, DB_VERSION);
    ck_assert_uint_eq(env->meta0->txnid, 0);
    ck_assert_uint_eq(env->meta0->root, 0);
    ck_assert_uint_eq(env->meta0->last_pgno, 1);

    /* Verify Meta Page 1 initialization */
    ck_assert_uint_eq(env->meta1->magic, DB_MAGIC);
    ck_assert_uint_eq(env->meta1->version, DB_VERSION);
    ck_assert_uint_eq(env->meta1->txnid, 1);
    ck_assert_uint_eq(env->meta1->root, 0);
    ck_assert_uint_eq(env->meta1->last_pgno, 1);

    /* Verify live_meta points to meta1 (highest TXNID) */
    ck_assert_ptr_eq(env->live_meta, env->meta1);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}

END_TEST
/*
 * Test: Open existing database
 * Verifies:
 * - Database can be re-opened
 * - MMAP is re-established
 * - Correct meta page (highest TXNID) is selected as live_meta
 */
START_TEST(test_env_open_existing_db)
{
    napr_db_env_t *env1 = NULL, *env2 = NULL;
    apr_status_t status;
    apr_size_t mapsize = 1024 * 1024;   /* 1 MB */

    /* First, create a new database */
    status = napr_db_env_create(&env1, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env1, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env1, test_db_path, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify initial state: live_meta should be meta1 (TXNID 1) */
    ck_assert_ptr_eq(env1->live_meta, env1->meta1);
    ck_assert_uint_eq(env1->live_meta->txnid, 1);

    /* Close the first environment */
    status = napr_db_env_close(env1);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Now re-open the existing database */
    status = napr_db_env_create(&env2, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env2, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Open without CREATE flag (existing DB) */
    status = napr_db_env_open(env2, test_db_path, 0);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify mmap is established */
    ck_assert_ptr_nonnull(env2->file);
    ck_assert_ptr_nonnull(env2->mmap);
    ck_assert_ptr_nonnull(env2->map_addr);
    ck_assert_ptr_nonnull(env2->meta0);
    ck_assert_ptr_nonnull(env2->meta1);
    ck_assert_ptr_nonnull(env2->live_meta);

    /* Verify meta pages are correctly validated */
    ck_assert_uint_eq(env2->meta0->magic, DB_MAGIC);
    ck_assert_uint_eq(env2->meta0->version, DB_VERSION);
    ck_assert_uint_eq(env2->meta1->magic, DB_MAGIC);
    ck_assert_uint_eq(env2->meta1->version, DB_VERSION);

    /* Verify live_meta selection: should be meta1 (highest TXNID) */
    ck_assert_ptr_eq(env2->live_meta, env2->meta1);
    ck_assert_uint_eq(env2->live_meta->txnid, 1);

    /* Close environment */
    status = napr_db_env_close(env2);
    ck_assert_int_eq(status, APR_SUCCESS);
}

END_TEST
/*
 * Test suite setup
 */
Suite *make_db_env_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("DB Environment");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_env_create_setmapsize_close);
    tcase_add_test(tc_core, test_env_open_new_db);
    tcase_add_test(tc_core, test_env_open_existing_db);

    suite_add_tcase(s, tc_core);

    return s;
}
