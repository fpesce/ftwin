#ifndef FT_CONFIG_H
#define FT_CONFIG_H

#include "ft_types.h"

/* Constants */
static const int ERROR_BUFFER_SIZE = 128;
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
#if HAVE_PUZZLE
    double threshold;
#endif
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
 * @brief Parses command-line arguments and populates the configuration structure.
 * @param conf The configuration structure to populate.
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @param first_arg_index Output parameter to store the index of the first non-option argument.
 * @return APR_SUCCESS on success, or an error code.
 */
apr_status_t ft_config_parse_args(ft_conf_t *conf, int argc, const char **argv, int *first_arg_index);

#endif /* FT_CONFIG_H */
