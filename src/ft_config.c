#include <apr_getopt.h>
#include <apr_strings.h>
#include <apr_user.h>
#include <pcre.h>
#include <unistd.h>
#include <grp.h>

#include "config.h"
#include "debug.h"
#include "ft_config.h"
#include "ft_system.h"
#include "ft_types.h"
#include "human_size.h"
#include "key_hash.h"

/* Forward declarations for key functions defined in ftwin.c */
const void *ft_fsize_get_key(const void *opaque);
apr_size_t ft_fsize_get_key_len(const void *opaque);
int apr_off_t_key_cmp(const void *key1, const void *key2, apr_size_t len);
apr_uint32_t apr_off_t_key_hash(const void *key, apr_size_t klen);
const void *ft_gids_get_key(const void *opaque);
apr_size_t ft_gid_get_key_len(const void *opaque);
int gid_t_key_cmp(const void *key1, const void *key2, apr_size_t len);
apr_uint32_t gid_t_key_hash(const void *key, apr_size_t klen);

int ft_file_cmp(const void *param1, const void *param2)
{
    const ft_file_t *file1 = param1;
    const ft_file_t *file2 = param2;

    if (file1->size < file2->size)
	return -1;
    else if (file2->size < file1->size)
	return 1;

    return 0;
}

/* Default ignore patterns */
static const char *default_ignores[] = {
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

static pcre *ft_pcre_compile(const char *regex, int caseless, apr_pool_t *p)
{
    const char *errptr;
    int erroffset, options = PCRE_DOLLAR_ENDONLY | PCRE_DOTALL;
    pcre *result;

    if (caseless)
	options |= PCRE_CASELESS;

    result = pcre_compile(regex, options, &errptr, &erroffset, NULL);
    if (NULL == result)
	DEBUG_ERR("can't parse %s at [%.*s] for -e / --regex-ignore-file: %s", regex, erroffset, regex, errptr);
    else
	apr_pool_cleanup_register(p, result, ft_pcre_free_cleanup, apr_pool_cleanup_null);


    return result;
}

static apr_status_t fill_gids_ht(const char *username, napr_hash_t *gids, apr_pool_t *p)
{
    gid_t list[256];
    ft_gid_t *gid;
    apr_uint32_t hash_value;
    int i, nb_gid;

    nb_gid = getgroups(sizeof(list) / sizeof(gid_t), list);
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

    for (i = 0; i < nb_gid; i++) {
	gid = napr_hash_search(gids, &(list[i]), sizeof(gid_t), &hash_value);
	if (NULL == gid) {
	    gid = apr_palloc(p, sizeof(struct ft_gid_t));
	    gid->val = list[i];
	    napr_hash_set(gids, gid, hash_value);
	}
    }

    return APR_SUCCESS;
}

static void ft_hash_add_ignore_list(napr_hash_t *hash, const char *file_list)
{
    const char *filename, *end;
    apr_uint32_t hash_value;
    apr_pool_t *pool;
    char *tmp;

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
    int i;

    for (i = 0; default_ignores[i] != NULL; i++) {
	ft_ignore_add_pattern_str(conf->global_ignores, default_ignores[i]);
    }
}

static void version()
{
    fprintf(stdout, PACKAGE_STRING "\n");
    fprintf(stdout, "Copyright (C) 2007 François Pesce\n");
    fprintf(stdout, "Licensed under the Apache License, Version 2.0 (the \"License\");\n");
    fprintf(stdout, "you may not use this file except in compliance with the License.\n");
    fprintf(stdout, "You may obtain a copy of the License at\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "\thttp://www.apache.org/licenses/LICENSE-2.0\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "Unless required by applicable law or agreed to in writing, software\n");
    fprintf(stdout, "distributed under the License is distributed on an \"AS IS\" BASIS,\n");
    fprintf(stdout, "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n");
    fprintf(stdout, "See the License for the specific language governing permissions and\n");
    fprintf(stdout, "limitations under the License.\n\n");
    fprintf(stdout, "Report bugs to " PACKAGE_BUGREPORT "\n");
}

static void usage(const char *name, const apr_getopt_option_t *opt_option)
{
    int i;

    fprintf(stdout, PACKAGE_STRING "\n");
    fprintf(stdout, "Usage: %s [OPTION]... [FILES or DIRECTORIES]...\n", name);
    fprintf(stdout, "Find identical files passed as parameter or recursively found in directories.\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "Mandatory arguments to long options are mandatory for short options too.\n");
    fprintf(stdout, "\n");

    for (i = 0; NULL != opt_option[i].name; i++) {
	fprintf(stdout, "-%c,\t--%s\t%s\n", opt_option[i].optch, opt_option[i].name, opt_option[i].description);
    }
}

static void print_usage_and_exit(const char *name, const apr_getopt_option_t *opt_option, const char *error_msg,
				 const char *arg)
{
    if (error_msg) {
	fprintf(stderr, "Error: %s %s\n\n", error_msg, arg);
    }
    usage(name, opt_option);
    exit(EXIT_FAILURE);
}

ft_conf_t *ft_config_create(apr_pool_t *pool)
{
    apr_uint32_t hash_value;
    ft_conf_t *conf = apr_pcalloc(pool, sizeof(ft_conf_t));

    conf->pool = pool;
    conf->heap = napr_heap_make(pool, ft_file_cmp);
    conf->ig_files = napr_hash_str_make(pool, 32, 8);
    conf->sizes =
	napr_hash_make(pool, 4096, 8, ft_fsize_get_key, ft_fsize_get_key_len, apr_off_t_key_cmp, apr_off_t_key_hash);
    conf->gids = napr_hash_make(pool, 4096, 8, ft_gids_get_key, ft_gid_get_key_len, gid_t_key_cmp, gid_t_key_hash);

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
    conf->excess_size = 50 * 1024 * 1024;
    conf->num_threads = ft_get_cpu_cores();	/* Default to number of CPU cores */
    conf->respect_gitignore = 1;	/* Respect .gitignore by default */
    conf->global_ignores = ft_ignore_context_create(pool, NULL, "/");	/* Initialize global ignores */
    ft_load_defaults(conf);	/* Load default ignore patterns */
    conf->mask = OPTION_RECSD;
#if HAVE_PUZZLE
    conf->threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;
#endif

    return conf;
}

apr_status_t ft_config_parse_args(ft_conf_t *conf, int argc, const char **argv)
{
    static const apr_getopt_option_t opt_option[] = {
	{"hidden", 'a', FALSE, "do not ignore hidden files."},
	{"case-unsensitive", 'c', FALSE, "this option applies to regex match."},
	{"display-size", 'd', FALSE, "\tdisplay size before duplicates (human-readable)."},
	{"dry-run", 'n', FALSE, "\tonly print the operations that would be done."},
	{"regex-ignore-file", 'e', TRUE, "filenames that match this are ignored."},
	{"follow-symlink", 'f', FALSE, "follow symbolic links."},
	{"help", 'h', FALSE, "\t\tdisplay usage."},
#if HAVE_PUZZLE
	{"image-cmp", 'I', FALSE, "\twill run ftwin in image cmp mode (using libpuzzle)."},
	{"image-threshold", 'T', TRUE,
	 "will change the image similarity threshold\n\t\t\t\t (default is [1], accepted [2/3/4/5])."},
#endif
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
#if HAVE_ARCHIVE
	{"tar-cmp", 't', FALSE, "\twill process files archived in .tar default: off."},
#endif
	{"threads", 'j', TRUE, "\tnumber of threads for parallel hashing (default: CPU cores)."},
	{"verbose", 'v', FALSE, "\tdisplay a progress bar."},
	{"version", 'V', FALSE, "\tdisplay version."},
	{"whitelist-regex-file", 'w', TRUE, "filenames that doesn't match this are ignored."},
	{"excessive-size", 'x', TRUE, "excessive size of file that switch off mmap use."},
	{NULL, 0, 0, NULL},	/* end (a.k.a. sentinel) */
    };
    char errbuf[128];
    char *regex = NULL, *wregex = NULL, *arregex = NULL;
    apr_getopt_t *os;
    const char *optarg;
    int optch;
    apr_status_t status;

    if (APR_SUCCESS != (status = apr_getopt_init(&os, conf->pool, argc, argv))) {
	DEBUG_ERR("error calling apr_getopt_init: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    while (APR_SUCCESS == (status = apr_getopt_long(os, opt_option, &optch, &optarg))) {
	switch (optch) {
	case 'a':
	    set_option(&conf->mask, OPTION_SHOW_HIDDEN, 1);
	    break;
	case 'c':
	    set_option(&conf->mask, OPTION_ICASE, 1);
	    break;
	case 'd':
	    set_option(&conf->mask, OPTION_SIZED, 1);
	    break;
	case 'n':
	    set_option(&conf->mask, OPTION_DRY_RUN, 1);
	    break;
	case 'e':
	    regex = apr_pstrdup(conf->pool, optarg);
	    break;
	case 'f':
	    set_option(&conf->mask, OPTION_FSYML, 1);
	    break;
	case 'h':
	    usage(argv[0], opt_option);
	    exit(0);
	case 'i':
	    ft_hash_add_ignore_list(conf->ig_files, optarg);
	    break;
#if HAVE_PUZZLE
	case 'I':
	    set_option(&conf->mask, OPTION_ICASE, 1);
	    set_option(&conf->mask, OPTION_PUZZL, 1);
	    wregex = apr_pstrdup(conf->pool, ".*\\.(gif|png|jpe?g)$");
	    break;
#endif
#if HAVE_JANSSON
	case 'J':
	    set_option(&conf->mask, OPTION_JSON, 1);
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		fprintf(stderr, "Warning: Verbose mode disabled for JSON output.\n");
		set_option(&conf->mask, OPTION_VERBO, 0);
	    }
	    break;
#endif
	case 'j':
	    {
		char *endptr;
		long threads = strtol(optarg, &endptr, 10);
		if (*endptr != '\0' || threads < 1 || threads > 256) {
		    print_usage_and_exit(argv[0], opt_option, "Invalid number of threads (must be 1-256):", optarg);
		}
		conf->num_threads = (unsigned int) threads;
	    }
	    break;
#if HAVE_PUZZLE
	case 'T':
	    switch (*optarg) {
	    case '1':
		conf->threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;
		break;
	    case '2':
		conf->threshold = PUZZLE_CVEC_SIMILARITY_LOW_THRESHOLD;
		break;
	    case '3':
		conf->threshold = 0.5;
		break;
	    case '4':
		conf->threshold = PUZZLE_CVEC_SIMILARITY_THRESHOLD;
		break;
	    case '5':
		conf->threshold = PUZZLE_CVEC_SIMILARITY_HIGH_THRESHOLD;
		break;
	    default:
		print_usage_and_exit(argv[0], opt_option, "invalid threshold:", optarg);
	    }
	    break;
#endif
	case 'm':
	    conf->minsize = parse_human_size(optarg);
	    if (conf->minsize < 0) {
		print_usage_and_exit(argv[0], opt_option, "Invalid size for --minimal-length:", optarg);
	    }
	    break;
	case 'M':
	    conf->maxsize = parse_human_size(optarg);
	    if (conf->maxsize < 0) {
		print_usage_and_exit(argv[0], opt_option, "Invalid size for --max-size:", optarg);
	    }
	    break;
	case 'o':
	    set_option(&conf->mask, OPTION_OPMEM, 1);
	    break;
	case 'p':
	    conf->p_path = apr_pstrdup(conf->pool, optarg);
	    conf->p_path_len = strlen(conf->p_path);
	    break;
	case 'r':
	    set_option(&conf->mask, OPTION_RECSD, 1);
	    break;
	case 'R':
	    set_option(&conf->mask, OPTION_RECSD, 0);
	    break;
	case 's':
	    conf->sep = *optarg;
	    break;
#if HAVE_ARCHIVE
	case 't':
	    set_option(&conf->mask, OPTION_UNTAR, 1);
	    arregex = apr_pstrdup(conf->pool, ".*\\.(tar\\.gz|tgz|tar\\.bz2|tbz2|tar\\.xz|txz|zip|rar|7z|tar)$");
	    break;
#endif
	case 'v':
	    if (!is_option_set(conf->mask, OPTION_JSON)) {
		set_option(&conf->mask, OPTION_VERBO, 1);
	    }
	    break;
	case 'V':
	    version();
	    exit(0);
	case 'w':
	    wregex = apr_pstrdup(conf->pool, optarg);
	    break;
	case 'x':
	    conf->excess_size = strtoul(optarg, NULL, 10);
	    if (ULONG_MAX == conf->minsize) {
                print_usage_and_exit(argv[0], opt_option, "can't parse for -x / --excessive-size", optarg);
	    }
	    break;
	}
    }

    if (APR_SUCCESS != (status = apr_uid_current(&(conf->userid), &(conf->groupid), conf->pool))) {
	DEBUG_ERR("error calling apr_uid_current: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    if (APR_SUCCESS != (status = apr_uid_name_get(&(conf->username), conf->userid, conf->pool))) {
	DEBUG_ERR("error calling apr_uid_name_get: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    if (APR_SUCCESS != (status = fill_gids_ht(conf->username, conf->gids, conf->pool))) {
	DEBUG_ERR("error calling fill_gids_ht: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    if (NULL != regex) {
	conf->ig_regex = ft_pcre_compile(regex, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
	if (NULL == conf->ig_regex) {
	    return APR_EGENERAL;
	}
    }

    if (NULL != wregex) {
	conf->wl_regex = ft_pcre_compile(wregex, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
	if (NULL == conf->wl_regex) {
	    return APR_EGENERAL;
	}
    }

    if (NULL != arregex) {
	conf->ar_regex = ft_pcre_compile(arregex, is_option_set(conf->mask, OPTION_ICASE), conf->pool);
	if (NULL == conf->ar_regex) {
	    return APR_EGENERAL;
	}
    }

    /* TODO: Return the index of the first non-option argument */
    /* For now, just return success if we get here */
    return APR_SUCCESS;
}