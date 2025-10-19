/*
 * Copyright (C) 2025 Francois Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <apr_getopt.h>
#include <apr_strings.h>
#include <apr_user.h>
#include <pcre.h>
#include <unistd.h>
#include <grp.h>

#include "config.h"
#include "debug.h"
#include "ft_config.h"
#include "ft_constants.h"
#include "ft_system.h"
#include "ft_types.h"
#include "human_size.h"
#include "key_hash.h"

static int should_exit_on_error = 1;

void ft_config_set_should_exit_on_error(int should_exit)
{
    should_exit_on_error = should_exit;
}

/* Forward declarations for key functions defined in ftwin.c */
const void *ft_fsize_get_key(const void *opaque);
const void *ft_gids_get_key(const void *opaque);

int ft_file_cmp(const void *param1, const void *param2)
{
    const ft_file_t *file1 = (const ft_file_t *) param1;
    const ft_file_t *file2 = (const ft_file_t *) param2;

    if (file1->size < file2->size) {
        return -1;
    }
    if (file2->size < file1->size) {
        return 1;
    }

    return 0;
}

/* Default ignore patterns */
static const char *const default_ignores[] = {
    /* VCS */
    ".git/", ".hg/", ".svn/",
    /* Build Artifacts */
    "build/", "dist/", "out/", "target/", "bin/",
    "*.o", "*.class", "*.pyc", "*.pyo",
    /* Dependency Caches */
    "node_modules/", "vendor/", ".venv/",
    /* OS & Editor Artifacts */
    ".DS_Store", "Thumbs.db", "*.swp", "*~", ".idea/", ".vscode/",
    NULL
};

static apr_status_t ft_pcre_free_cleanup(void *pcre_space)
{
    pcre_free(pcre_space);
    return APR_SUCCESS;
}

static pcre *ft_pcre_compile(const char *regex, int caseless, apr_pool_t *pool)
{
    const char *errptr = NULL;
    int erroffset = 0;
    int options = PCRE_DOLLAR_ENDONLY | PCRE_DOTALL;
    pcre *result = NULL;

    if (caseless) {
        options |= PCRE_CASELESS;
    }

    result = pcre_compile(regex, options, &errptr, &erroffset, NULL);
    if (NULL == result) {
        DEBUG_ERR("can't parse %s at [%.*s] for -e / --regex-ignore-file: %s", regex, erroffset, regex, errptr);
    }
    else {
        apr_pool_cleanup_register(pool, result, ft_pcre_free_cleanup, apr_pool_cleanup_null);
    }


    return result;
}

static const int MAX_GIDS = 256;

static apr_status_t fill_gids_ht(const char *username, napr_hash_t *gids, apr_pool_t *pool)
{
    gid_t list[MAX_GIDS];
    apr_uint32_t hash_value = 0;
    int nb_gid = 0;

    memset(list, 0, sizeof(list));
    nb_gid = getgroups((int) (sizeof(list) / sizeof(gid_t)), list);
    if (nb_gid < 0) {
        DEBUG_ERR("error calling getgroups()");
        return APR_EGENERAL;
    }
    /*
     * According to getgroups manpage:
     * It is unspecified whether the effective group ID of the calling process
     * is included in the returned list.  (Thus, an application should also
     * call getegid(2) and add or remove the resulting value.)
     */
    if (nb_gid < (sizeof(list) / sizeof(gid_t))) {
        list[nb_gid] = getegid();
        nb_gid++;
    }

    for (int idx = 0; idx < nb_gid; idx++) {
        ft_gid_t *gid = napr_hash_search(gids, &(list[idx]), sizeof(gid_t), &hash_value);
        if (NULL == gid) {
            gid = apr_palloc(pool, sizeof(struct ft_gid_t));
            gid->val = list[idx];
            napr_hash_set(gids, gid, hash_value);
        }
    }

    return APR_SUCCESS;
}

static void ft_hash_add_ignore_list(napr_hash_t *hash, const char *file_list)
{
    const char *filename = NULL;
    const char *end = NULL;
    apr_uint32_t hash_value = 0;
    apr_pool_t *pool = NULL;
    char *tmp = NULL;

    pool = napr_hash_pool_get(hash);
    filename = file_list;
    do {
        end = strchr(filename, ',');
        if (NULL != end) {
            tmp = apr_pstrndup(pool, filename, end - filename);
        }
        else {
            tmp = apr_pstrdup(pool, filename);
        }
        napr_hash_search(hash, tmp, strlen(tmp), &hash_value);
        napr_hash_set(hash, tmp, hash_value);

        if (NULL != end) {
            filename = end + 1;
        }
    } while ((NULL != end) && ('\0' != *filename));
}

static void ft_load_defaults(ft_conf_t *conf)
{
    for (int idx = 0; default_ignores[idx] != NULL; idx++) {
        ft_ignore_add_pattern_str(conf->global_ignores, default_ignores[idx]);
    }
}

static void version(void)
{
    (void) fprintf(stdout, PACKAGE_STRING "\n");
    (void) fprintf(stdout, "Copyright (C) 2007 François Pesce\n");
    (void) fprintf(stdout, "Licensed under the Apache License, Version 2.0 (the \"License\");\n");
    (void) fprintf(stdout, "you may not use this file except in compliance with the License.\n");
    (void) fprintf(stdout, "You may obtain a copy of the License at\n");
    (void) fprintf(stdout, "\n");
    (void) fprintf(stdout, "\thttp://www.apache.org/licenses/LICENSE-2.0\n");
    (void) fprintf(stdout, "\n");
    (void) fprintf(stdout, "Unless required by applicable law or agreed to in writing, software\n");
    (void) fprintf(stdout, "distributed under the License is distributed on an \"AS IS\" BASIS,\n");
    (void) fprintf(stdout, "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n");
    (void) fprintf(stdout, "See the License for the specific language governing permissions and\n");
    (void) fprintf(stdout, "limitations under the License.\n\n");
    (void) fprintf(stdout, "Report bugs to " PACKAGE_BUGREPORT "\n");
}

static void usage(const char *name, const apr_getopt_option_t *opt_option)
{
    (void) fprintf(stdout, PACKAGE_STRING "\n");
    (void) fprintf(stdout, "Usage: %s [OPTION]... [FILES or DIRECTORIES]...\n", name);
    (void) fprintf(stdout, "Find identical files passed as parameter or recursively found in directories.\n");
    (void) fprintf(stdout, "\n");
    (void) fprintf(stdout, "Mandatory arguments to long options are mandatory for short options too.\n");
    (void) fprintf(stdout, "\n");

    for (int idx = 0; NULL != opt_option[idx].name; idx++) {
        (void) fprintf(stdout, "-%c,\t--%s\t%s\n", opt_option[idx].optch, opt_option[idx].name, opt_option[idx].description);
    }
}

static void print_usage_and_exit(const char *name, const apr_getopt_option_t *opt_option, const char *error_msg, const char *arg)
{
    if (error_msg) {
        (void) fprintf(stderr, "Error: %s %s\n\n", error_msg, arg);
    }
    usage(name, opt_option);
    if (should_exit_on_error) {
        exit(EXIT_FAILURE);
    }
}

static const int HASH_STR_BUCKET_SIZE = 32;
static const int HASH_STR_MAX_ENTRIES = 8;
static const int HASH_SIZE_BUCKET_SIZE = 4096;
static const int HASH_SIZE_MAX_ENTRIES = 8;
static const apr_off_t EXCESS_SIZE_DEFAULT = (50LL * 1024 * 1024);

ft_conf_t *ft_config_create(apr_pool_t *pool)
{
    apr_uint32_t hash_value = 0;
    ft_conf_t *conf = apr_pcalloc(pool, sizeof(ft_conf_t));

    conf->pool = pool;
    conf->heap = napr_heap_make(pool, ft_file_cmp);
    conf->ig_files = napr_hash_str_make(pool, HASH_STR_BUCKET_SIZE, HASH_STR_MAX_ENTRIES);

    napr_hash_create_args_t hash_args = {
        .pool = pool,
        .nel = HASH_SIZE_BUCKET_SIZE,
        .ffactor = HASH_SIZE_MAX_ENTRIES,
        .get_key = ft_fsize_get_key,
        .get_key_len = ft_fsize_get_key_len,
        .key_cmp = apr_off_t_key_cmp,
        .hash = apr_off_t_key_hash
    };
    conf->sizes = napr_hash_make_ex(&hash_args);

    hash_args.get_key = ft_gids_get_key;
    hash_args.get_key_len = ft_gid_get_key_len;
    hash_args.key_cmp = gid_t_key_cmp;
    hash_args.hash = gid_t_key_hash;
    conf->gids = napr_hash_make_ex(&hash_args);

    /* To avoid endless loop, ignore looping directory ;) */
    napr_hash_search(conf->ig_files, ".", 1, &hash_value);
    napr_hash_set(conf->ig_files, ".", hash_value);
    napr_hash_search(conf->ig_files, "..", 2, &hash_value);
    napr_hash_set(conf->ig_files, "..", hash_value);

    conf->ig_regex = NULL;
    conf->wl_regex = NULL;
    conf->ar_regex = NULL;
    conf->p_path = NULL;
    conf->p_path_len = 0;
    conf->minsize = 0;
    conf->maxsize = 0;
    conf->sep = '\n';
    conf->excess_size = (apr_off_t) EXCESS_SIZE_DEFAULT;
    conf->num_threads = ft_get_cpu_cores();     /* Default to number of CPU cores */
    conf->respect_gitignore = 1;        /* Respect .gitignore by default */
    conf->global_ignores = ft_ignore_context_create(pool, NULL, "/");   /* Initialize global ignores */
    ft_load_defaults(conf);     /* Load default ignore patterns */
    conf->mask = OPTION_RECSD;
    conf->threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;

    return conf;
}

static const double DEFAULT_THRESHOLD = 0.5;

static const apr_getopt_option_t opt_option[] = {
    {"hidden", 'a', FALSE, "do not ignore hidden files."},
    {"case-unsensitive", 'c', FALSE, "this option applies to regex match."},
    {"display-size", 'd', FALSE, "\tdisplay size before duplicates (human-readable)."},
    {"dry-run", 'n', FALSE, "\tonly print the operations that would be done."},
    {"regex-ignore-file", 'e', TRUE, "filenames that match this are ignored."},
    {"follow-symlink", 'f', FALSE, "follow symbolic links."},
    {"help", 'h', FALSE, "\t\tdisplay usage."},
    {"image-cmp", 'I', FALSE, "\twill run ftwin in image cmp mode (using libpuzzle)."},
    {"image-threshold", 'T', TRUE,
     "will change the image similarity threshold\n\t\t\t\t (default is [1], accepted [2/3/4/5])."},
    {"ignore-list", 'i', TRUE, "\tcomma-separated list of file names to ignore."},
#if HAVE_JANSSON
    {"json", 'J', FALSE, "\t\toutput results in machine-readable JSON format."},
#endif
    {"minimal-length", 'm', TRUE, "minimum size of file to process."},
    {"max-size", 'M', TRUE, "maximum size of file to process."},
    {"optimize-memory", 'o', FALSE, "reduce memory usage, but increase process time."},
    {"priority-path", 'p', TRUE, "\tfile in this path are displayed first when\n\t\t\t\tduplicates are reported."},
    {"recurse-subdir", 'r', FALSE, "recurse subdirectories (default: on)."},
    {"no-recurse", 'R', FALSE, "do not recurse in subdirectories."},
    {"separator", 's', TRUE, "\tseparator character between twins, default: \\n."},
    {"tar-cmp", 't', FALSE, "\twill process files archived in .tar default: off."},
    {"threads", 'j', TRUE, "\tnumber of threads for parallel hashing (default: CPU cores)."},
    {"verbose", 'v', FALSE, "\tdisplay a progress bar."},
    {"version", 'V', FALSE, "\tdisplay version."},
    {"whitelist-regex-file", 'w', TRUE, "filenames that doesn't match this are ignored."},
    {"excessive-size", 'x', TRUE, "excessive size of file that switch off mmap use."},
    {NULL, 0, 0, NULL},         /* end (a.k.a. sentinel) */
};

/**
 * @brief A structure to hold pointers to the various regex string options.
 *
 * This is used to avoid passing multiple `char **` arguments to helper
 * functions, which can be error-prone (swappable parameters).
 */
struct regex_options
{
    char **ignore_regex;     /**< Pointer to the ignore regex string. */
    char **whitelist_regex;  /**< Pointer to the whitelist regex string. */
    char **archive_regex;    /**< Pointer to the archive regex string. */
};

/* Forward declarations for helper functions */

/**
 * @brief Maps a command-line option character to its corresponding flag and value.
 */
typedef struct
{
    int option_char;    /**< The single-character option, e.g., 'a'. */
    int option_flag;    /**< The flag to set, e.g., OPTION_SHOW_HIDDEN. */
    int value;          /**< The value to set (1 for on, 0 for off). */
} flag_option_mapping_t;

static void handle_flag_option(int option, ft_conf_t *conf);
static void handle_string_option(int option, const char *optarg, ft_conf_t *conf, struct regex_options *opts);
static apr_status_t handle_numeric_option(int option, const char *optarg, ft_conf_t *conf, const char *name,
                                          const apr_getopt_option_t *opt_option);
static apr_status_t handle_special_option(int option, const char *optarg, ft_conf_t *conf, struct regex_options *opts, const char *name,
                                          const apr_getopt_option_t *opt_option);

static apr_status_t process_options(int option, const char *optarg, ft_conf_t *conf, struct regex_options *opts, const char *name)
{
    apr_status_t status = APR_SUCCESS;

    switch (option) {
        /* Simple Flags */
    case 'a':
    case 'c':
    case 'd':
    case 'n':
    case 'f':
    case 'o':
    case 'r':
    case 'R':
    case 'v':
        handle_flag_option(option, conf);
        break;

        /* String Arguments */
    case 'e':
    case 'i':
    case 'p':
    case 's':
    case 'w':
        handle_string_option(option, optarg, conf, opts);
        break;

        /* Numeric Arguments */
    case 'j':
    case 'm':
    case 'M':
    case 'x':
        status = handle_numeric_option(option, optarg, conf, name, opt_option);
        break;

        /* Special/Complex Cases */
    case 'h':
    case 'V':
    case 'I':
    case 'T':
    case 'J':
    case 't':
        status = handle_special_option(option, optarg, conf, opts, name, opt_option);
        break;

    default:
        /* Should not happen. */
        break;
    }

    return status;
}

static const flag_option_mapping_t flag_mappings[] = {
    {'a', OPTION_SHOW_HIDDEN, 1},
    {'c', OPTION_ICASE, 1},
    {'d', OPTION_SIZED, 1},
    {'n', OPTION_DRY_RUN, 1},
    {'f', OPTION_FSYML, 1},
    {'o', OPTION_OPMEM, 1},
    {'r', OPTION_RECSD, 1},
    {'R', OPTION_RECSD, 0}
};

static void handle_flag_option(int option, ft_conf_t *conf)
{
    for (size_t idx = 0; idx < sizeof(flag_mappings) / sizeof(flag_mappings[0]); ++idx) {
        if (flag_mappings[idx].option_char == option) {
            set_option(&conf->mask, flag_mappings[idx].option_flag, flag_mappings[idx].value);
            return;
        }
    }

    if (option == 'v') {
        /* The verbose flag is a special case: it should only be set if JSON output is not enabled. */
        if (!is_option_set(conf->mask, OPTION_JSON)) {
            set_option(&conf->mask, OPTION_VERBO, 1);
        }
    }
}

static void handle_string_option(int option, const char *optarg, ft_conf_t *conf, struct regex_options *opts)
{
    switch (option) {
    case 'e':
        *(opts->ignore_regex) = apr_pstrdup(conf->pool, optarg);
        break;
    case 'i':
        ft_hash_add_ignore_list(conf->ig_files, optarg);
        break;
    case 'p':
        conf->p_path = apr_pstrdup(conf->pool, optarg);
        conf->p_path_len = strlen(conf->p_path);
        break;
    case 's':
        conf->sep = *optarg;
        break;
    case 'w':
        *(opts->whitelist_regex) = apr_pstrdup(conf->pool, optarg);
        break;
    default:
        /* Should not happen. */
        break;
    }
}

static apr_status_t handle_numeric_option(int option, const char *optarg, ft_conf_t *conf, const char *name,
                                          const apr_getopt_option_t *opt_option)
{
    switch (option) {
    case 'j':{
            char *endptr = NULL;
            long threads = strtol(optarg, &endptr, BASE_TEN);
            if (*endptr != '\0' || threads < 1 || threads > MAX_THREADS) {
                print_usage_and_exit(name, opt_option, "Invalid number of threads (must be 1-256):", optarg);
                return APR_EGENERAL;
            }
            conf->num_threads = (unsigned int) threads;
            break;
        }
    case 'm':
        conf->minsize = parse_human_size(optarg);
        if (conf->minsize < 0) {
            print_usage_and_exit(name, opt_option, "Invalid size for --minimal-length:", optarg);
            return APR_EGENERAL;
        }
        break;
    case 'M':
        conf->maxsize = parse_human_size(optarg);
        if (conf->maxsize < 0) {
            print_usage_and_exit(name, opt_option, "Invalid size for --max-size:", optarg);
            return APR_EGENERAL;
        }
        break;
    case 'x':
        conf->excess_size = (apr_off_t) strtoul(optarg, NULL, BASE_TEN);
        if (ULONG_MAX == conf->minsize) {
            print_usage_and_exit(name, opt_option, "can't parse for -x / --excessive-size", optarg);
            return APR_EGENERAL;
        }
        break;
    default:
        /* Should not happen. */
        break;
    }
    return APR_SUCCESS;
}

/**
 * @brief Handles image-specific command-line options ('I' and 'T').
 *
 * This function centralizes the logic for image comparison options,
 * reducing the complexity of the main option handling switch.
 */
static apr_status_t handle_image_options(int option, const char *optarg, ft_conf_t *conf, char **wregex, const char *name,
                                         const apr_getopt_option_t *opt_option)
{
    switch (option) {
    case 'I':
        set_option(&conf->mask, OPTION_ICASE, 1);
        set_option(&conf->mask, OPTION_PUZZL, 1);
        *wregex = apr_pstrdup(conf->pool, ".*\\.(gif|png|jpe?g)$");
        break;
    case 'T':
        switch (*optarg) {
        case '1':
            conf->threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;
            break;
        case '2':
            conf->threshold = PUZZLE_CVEC_SIMILARITY_LOW_THRESHOLD;
            break;
        case '3':
            conf->threshold = DEFAULT_THRESHOLD;
            break;
        case '4':
            conf->threshold = PUZZLE_CVEC_SIMILARITY_THRESHOLD;
            break;
        case '5':
            conf->threshold = PUZZLE_CVEC_SIMILARITY_HIGH_THRESHOLD;
            break;
        default:
            print_usage_and_exit(name, opt_option, "invalid threshold:", optarg);
            return APR_EGENERAL;
        }
        break;
    default:
        /* Should not be reached */
        break;
    }
    return APR_SUCCESS;
}

static apr_status_t handle_special_option(int option, const char *optarg, ft_conf_t *conf, struct regex_options *opts, const char *name,
                                          const apr_getopt_option_t *opt_option)
{
    apr_status_t status = APR_SUCCESS;
    switch (option) {
    case 'h':
        usage(name, opt_option);
        if (should_exit_on_error) {
            exit(0);
        }
        return APR_EGENERAL;
    case 'V':
        version();
        if (should_exit_on_error) {
            exit(0);
        }
        return APR_EGENERAL;
    case 'I':
    case 'T':
        status = handle_image_options(option, optarg, conf, opts->whitelist_regex, name, opt_option);
        break;
#if HAVE_JANSSON
    case 'J':
        set_option(&conf->mask, OPTION_JSON, 1);
        if (is_option_set(conf->mask, OPTION_VERBO)) {
            (void) fprintf(stderr, "Warning: Verbose mode disabled for JSON output.\n");
            set_option(&conf->mask, OPTION_VERBO, 0);
        }
        break;
#endif
    case 't':
        set_option(&conf->mask, OPTION_UNTAR, 1);
        *(opts->archive_regex) = apr_pstrdup(conf->pool, ".*\\.(tar\\.gz|tgz|tar\\.bz2|tbz2|tar\\.xz|txz|zip|rar|7z|tar)$");
        break;
    default:
        /* Should not happen. */
        break;
    }
    return status;
}

apr_status_t ft_config_parse_args(ft_conf_t *conf, int argc, const char **argv, int *first_arg_index)
{
    char errbuf[ERR_BUF_SIZE];
    char *regex_str = NULL;
    char *wregex_str = NULL;
    char *arregex_str = NULL;
    struct regex_options opts = { &regex_str, &wregex_str, &arregex_str };
    apr_getopt_t *opt_state = NULL;
    const char *optarg = NULL;
    int option = 0;
    apr_status_t status = APR_SUCCESS;

    memset(errbuf, 0, sizeof(errbuf));
    status = apr_getopt_init(&opt_state, conf->pool, argc, argv);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_getopt_init: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    while (APR_SUCCESS == (status = apr_getopt_long(opt_state, opt_option, &option, &optarg))) {
        status = process_options(option, optarg, conf, &opts, argv[0]);
        if (status != APR_SUCCESS) {
            /*
             * A non-success status here typically means an option like --help
             * or --version was processed, and exit was suppressed. Stop parsing.
             */
            return status;
        }
    }

    if (argc - opt_state->ind < 2) {
        print_usage_and_exit(argv[0], opt_option, "Please submit at least two files or one directory to process.", "");
        return APR_EGENERAL;
    }

    status = apr_uid_current(&(conf->userid), &(conf->groupid), conf->pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_uid_current: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    status = apr_uid_name_get(&(conf->username), conf->userid, conf->pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_uid_name_get: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    status = fill_gids_ht(conf->username, conf->gids, conf->pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling fill_gids_ht: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    if (NULL != regex_str) {
        conf->ig_regex = ft_pcre_compile(regex_str, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
        if (NULL == conf->ig_regex) {
            return APR_EGENERAL;
        }
    }

    if (NULL != wregex_str) {
        conf->wl_regex = ft_pcre_compile(wregex_str, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
        if (NULL == conf->wl_regex) {
            return APR_EGENERAL;
        }
    }

    if (NULL != arregex_str) {
        conf->ar_regex = ft_pcre_compile(arregex_str, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
        if (NULL == conf->ar_regex) {
            return APR_EGENERAL;
        }
    }

    /* Return the index of the first non-option argument */
    if (first_arg_index != NULL) {
        *first_arg_index = opt_state->ind;
    }

    return APR_SUCCESS;
}
