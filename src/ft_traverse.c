#include <apr_file_io.h>
#include <apr_strings.h>
#include <pcre.h>

#include "debug.h"
#include "ft_traverse.h"
#include "ft_types.h"
#include "ft_config.h"

#define MATCH_VECTOR_SIZE 210

static apr_status_t traverse_recursive(ft_conf_t *conf, const char *filename, apr_pool_t *gc_pool, struct stats const *stats, ft_ignore_context_t * parent_ctx)
{
    char errbuf[128];
    apr_finfo_t finfo;
    apr_dir_t *dir;
    apr_int32_t statmask = APR_FINFO_SIZE | APR_FINFO_MTIME | APR_FINFO_TYPE | APR_FINFO_USER | APR_FINFO_GROUP | APR_FINFO_UPROT | APR_FINFO_GPROT;
    apr_size_t fname_len;
    apr_uint32_t hash_value;
    apr_status_t status;

    if (!is_option_set(conf->mask, OPTION_FSYML)) {
	statmask |= APR_FINFO_LINK;
    }

    if (APR_SUCCESS != (status = apr_stat(&finfo, filename, statmask, gc_pool))) {
	if (is_option_set(conf->mask, OPTION_FSYML)) {
	    statmask ^= APR_FINFO_LINK;
	    if ((APR_SUCCESS == apr_stat(&finfo, filename, statmask, gc_pool)) && (finfo.filetype & APR_LNK)) {
		if (is_option_set(conf->mask, OPTION_VERBO)) {
		    (void) fprintf(stderr, "Skipping : [%s] (broken link)\n", filename);
		}
		return APR_SUCCESS;
	    }
	}

	DEBUG_ERR("error calling apr_stat on filename %s : %s", filename, apr_strerror(status, errbuf, 128));
	return status;
    }

    if (conf->respect_gitignore && parent_ctx) {
	ft_ignore_match_result_t match = ft_ignore_match(parent_ctx, filename, finfo.filetype == APR_DIR);
	if (match == FT_IGNORE_MATCH_IGNORED) {
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		(void) fprintf(stderr, "Ignoring (gitignore): [%s]\n", filename);
	    }
	    return APR_SUCCESS;
	}
    }

    if (0 != conf->userid) {
	if (finfo.user == conf->userid) {
	    if (!(APR_UREAD & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO)) {
		    (void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		}
		return APR_SUCCESS;
	    }
	}
	else if (NULL != napr_hash_search(conf->gids, &finfo.group, sizeof(gid_t), &hash_value)) {
	    if (!(APR_GREAD & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO)) {
		    (void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		}
		return APR_SUCCESS;
	    }
	}
	else if (!(APR_WREAD & finfo.protection)) {
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		(void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
	    }
	    return APR_SUCCESS;
	}
    }

    if (APR_DIR == finfo.filetype) {
	if (0 != conf->userid) {
	    if (finfo.user == conf->userid) {
		if (!(APR_UEXECUTE & finfo.protection)) {
		    if (is_option_set(conf->mask, OPTION_VERBO)) {
			(void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		    }
		    return APR_SUCCESS;
		}
	    }
	    else if (NULL != napr_hash_search(conf->gids, &finfo.group, sizeof(gid_t), &hash_value)) {
		if (!(APR_GEXECUTE & finfo.protection)) {
		    if (is_option_set(conf->mask, OPTION_VERBO)) {
			(void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		    }
		    return APR_SUCCESS;
		}
	    }
	    else if (!(APR_WEXECUTE & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO)) {
		    (void) fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		}
		return APR_SUCCESS;
	    }
	}

	if (APR_SUCCESS != (status = apr_dir_open(&dir, filename, gc_pool))) {
	    DEBUG_ERR("error calling apr_dir_open(%s): %s", filename, apr_strerror(status, errbuf, 128));
	    return status;
	}
	fname_len = strlen(filename);

	ft_ignore_context_t *current_ctx = parent_ctx;
	if (conf->respect_gitignore) {
	    const char *gitignore_path = apr_pstrcat(gc_pool, filename, "/.gitignore", NULL);
	    apr_finfo_t gitignore_finfo;

	    if (APR_SUCCESS == apr_stat(&gitignore_finfo, gitignore_path, APR_FINFO_TYPE, gc_pool)
		&& gitignore_finfo.filetype == APR_REG) {
		ft_ignore_context_t *local_ctx = ft_ignore_context_create(gc_pool, parent_ctx, filename);
		if (APR_SUCCESS == ft_ignore_load_file(local_ctx, gitignore_path)) {
		    current_ctx = local_ctx;
		    if (is_option_set(conf->mask, OPTION_VERBO)) {
			(void) fprintf(stderr, "Loaded .gitignore from: [%s]\n", filename);
		    }
		}
	    }
	}
	while ((APR_SUCCESS == (status = apr_dir_read(&finfo, APR_FINFO_NAME | APR_FINFO_TYPE, dir)))
	       && (NULL != finfo.name)) {
	    char *fullname;
	    apr_size_t fullname_len;
	    struct stats child;
	    struct stats const *ancestor;

	    if (NULL != napr_hash_search(conf->ig_files, finfo.name, strlen(finfo.name), NULL)) {
		continue;
	    }

	    if ('.' == finfo.name[0] && !is_option_set(conf->mask, OPTION_SHOW_HIDDEN)) {
		continue;
	    }

	    if (APR_DIR == finfo.filetype && !is_option_set(conf->mask, OPTION_RECSD)) {
		continue;
	    }

	    fullname = apr_pstrcat(gc_pool, filename, ('/' == filename[fname_len - 1]) ? "" : "/", finfo.name, NULL);
	    fullname_len = strlen(fullname);

	    if ((NULL != conf->ig_regex) && (APR_DIR != finfo.filetype)) {
		int match_code;
		int ovector[MATCH_VECTOR_SIZE];
		match_code = pcre_exec(conf->ig_regex, NULL, fullname, fullname_len, 0, 0, ovector, MATCH_VECTOR_SIZE);
		if (match_code >= 0) {
		    continue;
		}
	    }

	    if ((NULL != conf->wl_regex) && (APR_DIR != finfo.filetype)) {
		int match_code;
		int ovector[MATCH_VECTOR_SIZE];
		match_code = pcre_exec(conf->wl_regex, NULL, fullname, fullname_len, 0, 0, ovector, MATCH_VECTOR_SIZE);
		if (match_code < 0) {
		    continue;
		}
	    }

	    if (stats) {
		if (stats->stat.inode) {
		    for (ancestor = stats; (ancestor = ancestor->parent) != 0;) {
			if (ancestor->stat.inode == stats->stat.inode && ancestor->stat.device == stats->stat.device) {
			    if (is_option_set(conf->mask, OPTION_VERBO)) {
				(void) fprintf(stderr, "Warning: %s: recursive directory loop\n", filename);
			    }
			    return APR_SUCCESS;
			}
		    }
		}
	    }
	    child.parent = stats;
	    child.stat = finfo;

	    status = traverse_recursive(conf, fullname, gc_pool, &child, current_ctx);

	    if (APR_SUCCESS != status) {
		DEBUG_ERR("error recursively calling traverse_recursive: %s", apr_strerror(status, errbuf, 128));
		return status;
	    }
	}
	if ((APR_SUCCESS != status) && (APR_ENOENT != status)) {
	    DEBUG_ERR("error calling apr_dir_read: %s", apr_strerror(status, errbuf, 128));
	    return status;
	}

	if (APR_SUCCESS != (status = apr_dir_close(dir))) {
	    DEBUG_ERR("error calling apr_dir_close: %s", apr_strerror(status, errbuf, 128));
	    return status;
	}
    }
    else if (APR_REG == finfo.filetype || ((APR_LNK == finfo.filetype) && (is_option_set(conf->mask, OPTION_FSYML)))) {
	if (finfo.size >= conf->minsize && (conf->maxsize == 0 || finfo.size <= conf->maxsize)) {
	    ft_file_t *file;
	    ft_fsize_t *fsize;

	    file = apr_palloc(conf->pool, sizeof(struct ft_file_t));
	    file->path = apr_pstrdup(conf->pool, filename);
	    file->size = finfo.size;
	    file->mtime = finfo.mtime;

	    if ((conf->p_path) && (strlen(filename) >= conf->p_path_len)
		&& ((is_option_set(conf->mask, OPTION_ICASE) && !strncasecmp(filename, conf->p_path, conf->p_path_len))
		    || (!is_option_set(conf->mask, OPTION_ICASE) && !memcmp(filename, conf->p_path, conf->p_path_len)))) {
		file->prioritized |= 0x1;
	    }
	    else {
		file->prioritized &= 0x0;
	    }
	    file->cvec_ok &= 0x0;
	    napr_heap_insert(conf->heap, file);

	    if (NULL == (fsize = napr_hash_search(conf->sizes, &finfo.size, sizeof(apr_off_t), &hash_value))) {
		fsize = apr_palloc(conf->pool, sizeof(struct ft_fsize_t));
		fsize->val = finfo.size;
		fsize->chksum_array = NULL;
		fsize->nb_checksumed = 0;
		fsize->nb_files = 0;
		napr_hash_set(conf->sizes, fsize, hash_value);
	    }
	    fsize->nb_files++;
	}
    }

    return APR_SUCCESS;
}

apr_status_t ft_traverse_path(ft_conf_t *conf, const char *path)
{
    apr_pool_t *gc_pool;
    apr_status_t status;

    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, conf->pool))) {
	char errbuf[128];
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    status = traverse_recursive(conf, path, gc_pool, NULL, conf->global_ignores);

    apr_pool_destroy(gc_pool);

    return status;
}
