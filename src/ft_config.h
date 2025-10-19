#ifndef FT_CONFIG_H
#define FT_CONFIG_H

#include "ft_types.h"
#include <stddef.h>

/**
 * @defgroup CoreConstants Core Constants
 * @ingroup Core
 * @brief Fundamental constants used throughout the application.
 *
 * This includes buffer sizes, conversion factors, and other static values
 * that are shared across different modules.
 * @{
 */

/**
 * @brief The number of bytes in one kibibyte (1024).
 */
static const size_t KIBIBYTE = 1024;

/**
 * @brief The number of microseconds in one second.
 */
static const double MICROSECONDS_PER_SECOND = 1000000.0;

/**
 * @brief The maximum length for a file path buffer.
 */
static const size_t MAX_PATH_BUFFER = 4096;

/**
 * @brief The maximum value of a character, often used as a buffer size.
 */
static const size_t CHAR_MAX_VAL = 256;

/**
 * @brief The default buffer size for small operations.
 */
static const size_t DEFAULT_SMALL_BUFFER_SIZE = 16;

/**
 * @brief The default buffer size for I/O operations.
 */
static const size_t DEFAULT_IO_BUFFER_SIZE = 8192;

/**
 * @brief The base for string-to-integer conversions.
 */
static const int DECIMAL_BASE = 10;

/**
 * @brief A common loop count for benchmark iterations.
 */
static const int BENCHMARK_ITERATIONS = 20;

/**
 * @brief A larger loop count for stress tests.
 */
static const int STRESS_TEST_ITERATIONS = 100;

/**
 * @brief A timeout value in seconds for parallel operations.
 */
static const int PARALLEL_TIMEOUT_SECONDS = 30;

/**
 * @brief A sample file size for testing purposes.
 */
static const int TEST_FILE_SIZE_SMALL = 5120;

/**
 * @brief A larger sample file size for testing.
 */
static const int TEST_FILE_SIZE_LARGE = 50000;

/**
 * @brief A sample chunk size for file operations in tests.
 */
static const int TEST_CHUNK_SIZE = 10;

/** @} */

/* Constants */
static const int MAX_THREADS = 256;
static const int BASE_TEN = 10;

/**
 * @brief Main configuration structure for the ftwin application.
 * @ingroup CoreLogic
 */
typedef struct ft_conf_t
{
    apr_off_t minsize;
    apr_off_t maxsize;
    apr_off_t excess_size;
    double threshold;
    apr_pool_t *pool;
    napr_heap_t *heap;
    napr_hash_t *sizes;
    napr_hash_t *gids;
    napr_hash_t *ig_files;
    pcre *ig_regex;
    pcre *wl_regex;
    pcre *ar_regex;
    char *p_path;
    char *username;
    apr_size_t p_path_len;
    apr_uid_t userid;
    apr_gid_t groupid;
    unsigned int num_threads;
    ft_ignore_context_t *global_ignores;
    int respect_gitignore;
    unsigned short int mask;
    char sep;
} ft_conf_t;


/**
 * @brief Creates and initializes a new configuration structure.
 * @param pool The main APR memory pool.
 * @return A pointer to the newly created ft_conf_t structure.
 */
ft_conf_t *ft_config_create(apr_pool_t *pool);

/**
 * @brief Sets a flag to control whether the application exits on a fatal configuration error.
 *
 * This is primarily used during testing to prevent the test suite from terminating
 * when testing invalid command-line arguments.
 *
 * @param should_exit A non-zero value to enable exiting on error (default), or 0 to disable.
 */
void ft_config_set_should_exit_on_error(int should_exit);

/**
 * @brief Parses command-line arguments and populates the configuration structure.
 * @param conf The configuration structure to populate.
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @param first_arg_index Output parameter to store the index of the first non-option argument.
 * @return APR_SUCCESS on success, or an error code.
 */
apr_status_t ft_config_parse_args(ft_conf_t *conf, int argc, const char **argv, int *first_arg_index);

#endif /* FT_CONFIG_H */
