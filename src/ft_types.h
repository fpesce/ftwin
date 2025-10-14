#ifndef FT_TYPES_H
#define FT_TYPES_H

#include <pcre.h>
#include <sys/types.h>

#include <apr_file_info.h>
#include <apr_getopt.h>
#include <apr_pools.h>
#include <apr_thread_mutex.h>
#include <apr_time.h>
#include <napr_hash.h>

#include "config.h"
#include "checksum.h"
#include "ft_ignore.h"
#include "napr_heap.h"

#if HAVE_PUZZLE
#include <puzzle.h>
#endif

#if HAVE_JANSSON
#include <jansson.h>
#endif

/* Option flags */
#define is_option_set(mask, option)  ((mask & option) == option)

#define set_option(mask, option, on)     \
    do {                                 \
        if (on)                          \
            *mask |= option;             \
        else                             \
            *mask &= ~option;            \
    } while (0)

#define OPTION_ICASE 0x0001
#define OPTION_FSYML 0x0002
#define OPTION_RECSD 0x0004
#define OPTION_VERBO 0x0008
#define OPTION_OPMEM 0x0010
#define OPTION_REGEX 0x0020
#define OPTION_SIZED 0x0040
#define OPTION_SHOW_HIDDEN 0x0080
#if HAVE_PUZZLE
#define OPTION_PUZZL 0x0100
#endif
#if HAVE_ARCHIVE
#define OPTION_UNTAR 0x0200
#endif
#define OPTION_DRY_RUN 0x0400
#define OPTION_JSON 0x0800

/* ANSI Color Codes */
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_BOLD "\x1b[1m"
#define ANSI_COLOR_RESET "\x1b[0m"

/* Type Definitions */

typedef struct ft_file_t
{
    apr_off_t size;
    apr_time_t mtime;
    char *path;
#if HAVE_ARCHIVE
    char *subpath;
#endif
#if HAVE_PUZZLE
    PuzzleCvec cvec;
    int cvec_ok:1;
#endif
    int prioritized:1;
} ft_file_t;

typedef struct ft_chksum_t
{
    ft_hash_t hash_value;
    ft_file_t *file;
} ft_chksum_t;

typedef struct ft_fsize_t
{
    apr_off_t val;
    ft_chksum_t *chksum_array;
    apr_uint32_t nb_files;
    apr_uint32_t nb_checksumed;
} ft_fsize_t;

typedef struct ft_gid_t
{
    gid_t val;
} ft_gid_t;

/**
 * @brief Forward declaration for the main configuration structure.
 * The full definition is in ft_config.h.
 */
typedef struct ft_conf_t ft_conf_t;

struct stats
{
    struct stats const *parent;
    apr_finfo_t stat;
};

/* Parallel hashing data structures */

/**
 * Task structure passed to worker threads for hashing individual files.
 */
typedef struct hashing_task_t
{
    ft_fsize_t *fsize;
    apr_uint32_t index;
} hashing_task_t;

/**
 * Shared context for parallel hashing operations.
 * Protected by mutexes where necessary for thread safety.
 */
typedef struct hashing_context_t
{
    ft_conf_t *conf;
    apr_thread_mutex_t *stats_mutex;
    apr_pool_t *pool;

    /* Statistics (protected by stats_mutex) */
    apr_size_t files_processed;
    apr_size_t total_files;
} hashing_context_t;

#endif /* FT_TYPES_H */