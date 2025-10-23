/**
 * @file check_db_cow.c
 * @brief Tests for Copy-on-Write (CoW) mechanisms in napr_db
 *
 * Tests page allocation and CoW functionality for write transactions.
 */

#include <check.h>
#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <string.h>
#include <unistd.h>

#include "../src/napr_db_internal.h"
#include "check_db_constants.h"

/* Test database path in /tmp */
#define TEST_DB_PATH "/tmp/test_cow.db"

static const uint32_t DEADBEEF = 0xDEADBEEF;
static const uint32_t PAGE_COUNT_TEST = 5;

/* Helper to create and open a test database */
static apr_status_t create_test_db(apr_pool_t *pool, napr_db_env_t **env_out)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;

    /* Remove existing test database */
    unlink(TEST_DB_PATH);

    /* Create new database */
    status = napr_db_env_create(&env, pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_set_mapsize(env, ONE_MB);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_open(env, TEST_DB_PATH, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    if (status != APR_SUCCESS) {
        return status;
    }

    *env_out = env;
    return APR_SUCCESS;
}

/*
 * Test: db_page_alloc increments last_pgno correctly
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_alloc_single)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t allocated_pgno = 0;
    pgno_t initial_last_pgno = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Record initial state */
    initial_last_pgno = txn->new_last_pgno;

    /* Allocate a single page */
    status = db_page_alloc(txn, 1, &allocated_pgno);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify allocated page number is correct */
    ck_assert_int_eq(allocated_pgno, initial_last_pgno + 1);

    /* Verify new_last_pgno was incremented */
    ck_assert_int_eq(txn->new_last_pgno, initial_last_pgno + 1);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_alloc with multiple pages
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_alloc_multiple)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t allocated_pgno = 0;
    pgno_t initial_last_pgno = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Record initial state */
    initial_last_pgno = txn->new_last_pgno;

    /* Allocate PAGE_COUNT_TEST contiguous pages */
    status = db_page_alloc(txn, PAGE_COUNT_TEST, &allocated_pgno);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify allocated page number is correct */
    ck_assert_int_eq(allocated_pgno, initial_last_pgno + 1);

    /* Verify new_last_pgno was incremented by PAGE_COUNT_TEST */
    ck_assert_int_eq(txn->new_last_pgno, initial_last_pgno + PAGE_COUNT_TEST);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_alloc sequential allocations
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_alloc_sequential)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t pgno1 = 0;
    pgno_t pgno2 = 0;
    pgno_t pgno3 = 0;
    pgno_t initial_last_pgno = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    initial_last_pgno = txn->new_last_pgno;

    /* Allocate pages sequentially */
    status = db_page_alloc(txn, 1, &pgno1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_page_alloc(txn, 1, &pgno2);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_page_alloc(txn, 1, &pgno3);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify pages are sequential */
    ck_assert_int_eq(pgno1, initial_last_pgno + 1);
    ck_assert_int_eq(pgno2, initial_last_pgno + 2);
    ck_assert_int_eq(pgno3, initial_last_pgno + 3);

    /* Verify final state */
    ck_assert_int_eq(txn->new_last_pgno, initial_last_pgno + 3);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_alloc rejects read-only transactions
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_alloc_rdonly_rejected)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t allocated_pgno = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin READ-ONLY transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Attempt to allocate - should fail */
    status = db_page_alloc(txn, 1, &allocated_pgno);
    ck_assert_int_eq(status, APR_EINVAL);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_get_writable creates dirty copy on first call
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cow_first_modification)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *original_page = NULL;
    DB_PageHeader *dirty_copy = NULL;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Get pointer to page 0 (meta page) in the memory map */
    original_page = (DB_PageHeader *) env->map_addr;

    /* Get writable copy */
    status = db_page_get_writable(txn, original_page, &dirty_copy);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(dirty_copy);

    /* Verify dirty copy is different address than original */
    ck_assert_ptr_ne(dirty_copy, original_page);

    /* Verify dirty copy has same content as original */
    ck_assert_int_eq(memcmp(dirty_copy, original_page, PAGE_SIZE), 0);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_get_writable returns same dirty copy on subsequent calls
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cow_subsequent_modifications)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *original_page = NULL;
    DB_PageHeader *dirty_copy1 = NULL;
    DB_PageHeader *dirty_copy2 = NULL;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Get pointer to page 0 in the memory map */
    original_page = (DB_PageHeader *) env->map_addr;

    /* First call - creates dirty copy */
    status = db_page_get_writable(txn, original_page, &dirty_copy1);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(dirty_copy1);

    /* Second call - should return same dirty copy */
    status = db_page_get_writable(txn, original_page, &dirty_copy2);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(dirty_copy2);

    /* Verify both pointers are identical (same buffer) */
    ck_assert_ptr_eq(dirty_copy1, dirty_copy2);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_get_writable modifications don't affect original
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cow_isolation)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *original_page = NULL;
    DB_PageHeader *dirty_copy = NULL;
    uint32_t original_magic = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Get pointer to page 0 (meta page) */
    original_page = (DB_PageHeader *) env->map_addr;
    DB_MetaPage *original_meta = (DB_MetaPage *) original_page;

    /* Save original magic number */
    original_magic = original_meta->magic;

    /* Get writable copy */
    status = db_page_get_writable(txn, original_page, &dirty_copy);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Modify the dirty copy */
    DB_MetaPage *dirty_meta = (DB_MetaPage *) dirty_copy;
    dirty_meta->magic = DEADBEEF;

    /* Verify original is unchanged */
    ck_assert_int_eq(original_meta->magic, original_magic);

    /* Verify dirty copy has the new value */
    ck_assert_int_eq(dirty_meta->magic, DEADBEEF);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_get_writable tracks multiple pages
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cow_multiple_pages)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t pgno2 = 0;
    pgno_t pgno3 = 0;
    DB_PageHeader *page2 = NULL;
    DB_PageHeader *page3 = NULL;
    DB_PageHeader *dirty2 = NULL;
    DB_PageHeader *dirty3 = NULL;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Allocate two data pages */
    status = db_page_alloc(txn, 2, &pgno2);
    ck_assert_int_eq(status, APR_SUCCESS);
    pgno3 = pgno2 + 1;

    /* Extend file to accommodate new pages */
    apr_off_t new_size = (apr_off_t) (pgno3 + 1) * PAGE_SIZE;
    status = apr_file_trunc(env->file, new_size);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Remap the file */
    apr_mmap_delete(env->mmap);
    env->mapsize = (apr_size_t) new_size;
    status = apr_mmap_create(&env->mmap, env->file, 0, env->mapsize, APR_MMAP_READ | APR_MMAP_WRITE, env->pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = apr_mmap_offset(&env->map_addr, env->mmap, 0);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Initialize the pages with proper headers */
    page2 = (DB_PageHeader *) ((char *) env->map_addr + (pgno2 * PAGE_SIZE));
    page3 = (DB_PageHeader *) ((char *) env->map_addr + (pgno3 * PAGE_SIZE));
    memset(page2, 0, PAGE_SIZE);
    memset(page3, 0, PAGE_SIZE);
    page2->pgno = pgno2;
    page3->pgno = pgno3;

    /* Get writable copies of both pages */
    status = db_page_get_writable(txn, page2, &dirty2);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(dirty2);

    status = db_page_get_writable(txn, page3, &dirty3);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(dirty3);

    /* Verify they are different buffers */
    ck_assert_ptr_ne(dirty2, dirty3);

    /* Verify each is different from its original */
    ck_assert_ptr_ne(dirty2, page2);
    ck_assert_ptr_ne(dirty3, page3);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: db_page_get_writable rejects read-only transactions
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cow_rdonly_rejected)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *original_page = NULL;
    DB_PageHeader *dirty_copy = NULL;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin READ-ONLY transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Get pointer to page 0 */
    original_page = (DB_PageHeader *) env->map_addr;

    /* Attempt to get writable copy - should fail */
    status = db_page_get_writable(txn, original_page, &dirty_copy);
    ck_assert_int_eq(status, APR_EINVAL);

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Suite creation
 */
Suite *make_db_cow_suite(void)
{
    Suite *suite = suite_create("DB_COW");

    /* Page allocation tests */
    TCase *tc_alloc = tcase_create("PageAlloc");
    tcase_add_test(tc_alloc, test_page_alloc_single);
    tcase_add_test(tc_alloc, test_page_alloc_multiple);
    tcase_add_test(tc_alloc, test_page_alloc_sequential);
    tcase_add_test(tc_alloc, test_page_alloc_rdonly_rejected);
    suite_add_tcase(suite, tc_alloc);

    /* Copy-on-Write tests */
    TCase *tc_cow = tcase_create("CopyOnWrite");
    tcase_add_test(tc_cow, test_cow_first_modification);
    tcase_add_test(tc_cow, test_cow_subsequent_modifications);
    tcase_add_test(tc_cow, test_cow_isolation);
    tcase_add_test(tc_cow, test_cow_multiple_pages);
    tcase_add_test(tc_cow, test_cow_rdonly_rejected);
    suite_add_tcase(suite, tc_cow);

    return suite;
}
