/**
 * @file check_db_constants.h
 * @brief Constants for database tests
 */
#ifndef CHECK_DB_CONSTANTS_H
#define CHECK_DB_CONSTANTS_H

/*
 * =================================================================
 * Test database paths
 * =================================================================
 */

#define DB_TEST_PATH_COW "/tmp/test_cow.db"
#define DB_TEST_PATH_CURSOR "/tmp/test_cursor.db"
#define DB_TEST_PATH_ENV "/tmp/test_napr_db.db"
#define DB_TEST_PATH_READ "/tmp/test_db_read.db"
#define DB_TEST_PATH_SPLIT "/tmp/test_split.db"
#define DB_TEST_PATH_TXN "/tmp/test_napr_db_txn.db"
#define DB_TEST_PATH_WRITE "/tmp/test_write.db"
#define DB_TEST_PATH_DELETE "/tmp/test_db_delete.db"
#define DB_TEST_PATH_MVCC "/tmp/test_mvcc.db"

/*
 * =================================================================
 * Test data and buffer sizes
 * =================================================================
 */

#define DB_TEST_DELETE_KEY_SIZE 4
#define DB_TEST_DELETE_DATA_SIZE 6

#define DB_TEST_KEY_BUF_SIZE 32
#define DB_TEST_DATA_BUF_SIZE 64

/*
 * =================================================================
 * Page and key counts for tests
 * =================================================================
 */

#define DB_TEST_PAGE_COUNT_5 5
#define DB_TEST_KEY_COUNT_8 8
#define DB_TEST_KEY_COUNT_9 9
#define DB_TEST_KEY_COUNT_5 5
#define DB_TEST_KEY_COUNT_10 10
#define DB_TEST_NUM_KEYS_1000 1000
#define DB_TEST_NUM_KEYS_10K 10000

/*
 * =================================================================
 * Miscellaneous test constants
 * =================================================================
 */

#define DB_TEST_MAGIC_DEADBEEF 0xDEADBEEF
#define DB_TEST_DECIMAL_BASE 10
#define DB_TEST_TIMEOUT_ONE_MINUTE 60

/*
 * =================================================================
 * Map sizes for test environments
 * =================================================================
 */

#define DB_TEST_MAPSIZE_1MB (1024UL * 1024UL)
#define DB_TEST_MAPSIZE_10MB (10UL * DB_TEST_MAPSIZE_1MB)
#define DB_TEST_MAPSIZE_20MB (20UL * DB_TEST_MAPSIZE_1MB)

/*
 * =================================================================
 * Pre-existing constants from old file
 * =================================================================
 */
#define TEST_PAGE_NO_10 10
#define TEST_PAGE_NO_20 20
#define TEST_PAGE_NO_30 30

#endif /* CHECK_DB_CONSTANTS_H */
