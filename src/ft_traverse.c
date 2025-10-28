#include <apr_file_io.h>
#include <apr_strings.h>
#include <pcre.h>

#include "debug.h"
#include "ft_constants.h"
#include "ft_traverse.h"
#include "ft_types.h"
#include "ft_config.h"
#include "napr_cache.h"

enum
{
    MATCH_VECTOR_SIZE = 210
};

static apr_status_t traverse_recursive(ft_conf_t *conf, const char *filename, apr_pool_t *gc_pool, struct stats const *stats, ft_ignore_context_t *parent_ctx);

static apr_status_t get_file_info(const char *filename, apr_finfo_t *finfo, ft_conf_t *conf, apr_pool_t *pool)
{
    apr_int32_t statmask = APR_FINFO_SIZE | APR_FINFO_MTIME | APR_FINFO_CTIME | APR_FINFO_TYPE | APR_FINFO_USER | APR_FINFO_GROUP | APR_FINFO_UPROT | APR_FINFO_GPROT;
    if (!is_option_set(conf->mask, OPTION_FSYML)) {
        statmask |= APR_FINFO_LINK;
    }

    apr_status_t status = apr_stat(finfo, filename, statmask, pool);
    if (status != APR_SUCCESS) {
        char errbuf[ERR_BUF_SIZE];
        if (is_option_set(conf->mask, OPTION_FSYML) && (finfo->filetype & APR_LNK)) {
            if (is_option_set(conf->mask, OPTION_VERBO)) {
                (void) fprintf(stderr, "Skipping : [%s] (broken link)\n", filename);
            }
            return APR_SUCCESS;
        }
        DEBUG_ERR("error calling apr_stat on filename %s : %s", filename, apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
    return status;
}

static apr_status_t check_permissions(const apr_finfo_t *finfo, ft_conf_t *conf)
{
    apr_uint32_t hash_value = 0;
    apr_int32_t read_perm = (finfo->filetype == APR_DIR) ? APR_UEXECUTE : APR_UREAD;
    apr_int32_t group_read_perm = (finfo->filetype == APR_DIR) ? APR_GEXECUTE : APR_GREAD;
    apr_int32_t world_read_perm = (finfo->filetype == APR_DIR) ? APR_WEXECUTE : APR_WREAD;

    if (conf->userid != 0) {
        if (finfo->user == conf->userid) {
            if (!(read_perm & finfo->protection)) {
                return APR_EACCES;
            }
        }
        else if (napr_hash_search(conf->gids, &finfo->group, sizeof(gid_t), &hash_value) != NULL) {
            if (!(group_read_perm & finfo->protection)) {
                return APR_EACCES;
            }
        }
        else if (!(world_read_perm & finfo->protection)) {
            return APR_EACCES;
        }
    }
    return APR_SUCCESS;
}

static apr_status_t process_file(const char *filename, const apr_finfo_t *finfo, ft_conf_t *conf)
{
    if (finfo->size >= conf->minsize && (conf->maxsize == 0 || finfo->size <= conf->maxsize)) {
        ft_file_t *file = apr_palloc(conf->pool, sizeof(struct ft_file_t));
        file->path = apr_pstrdup(conf->pool, filename);
        file->size = finfo->size;
        file->mtime = finfo->mtime;
        file->ctime = finfo->ctime;
        file->prioritized = ((conf->p_path) && (strlen(filename) >= conf->p_path_len) &&
                             ((is_option_set(conf->mask, OPTION_ICASE) && !strncasecmp(filename, conf->p_path, conf->p_path_len)) ||
                              (!is_option_set(conf->mask, OPTION_ICASE) && !memcmp(filename, conf->p_path, conf->p_path_len))));
        file->cvec_ok = 0;
        napr_heap_insert(conf->heap, file);

        apr_uint32_t hash_value = 0;
        ft_fsize_t *fsize = napr_hash_search(conf->sizes, &finfo->size, sizeof(apr_off_t), &hash_value);
        if (fsize == NULL) {
            fsize = apr_palloc(conf->pool, sizeof(struct ft_fsize_t));
            fsize->val = finfo->size;
            fsize->chksum_array = NULL;
            fsize->nb_checksumed = 0;
            fsize->nb_files = 0;
            napr_hash_set(conf->sizes, fsize, hash_value);
        }
        fsize->nb_files++;
    }
    return APR_SUCCESS;
}

static apr_status_t process_directory_entry(const char *fullname, const apr_finfo_t *finfo, ft_conf_t *conf, apr_pool_t *gc_pool, const struct stats *stats, ft_ignore_context_t *current_ctx)
{
    int ovector[MATCH_VECTOR_SIZE];
    if ((conf->ig_regex && pcre_exec(conf->ig_regex, NULL, fullname, (int) strlen(fullname), 0, 0, ovector, MATCH_VECTOR_SIZE) >= 0) ||
        (conf->wl_regex && pcre_exec(conf->wl_regex, NULL, fullname, (int) strlen(fullname), 0, 0, ovector, MATCH_VECTOR_SIZE) < 0)) {
        return APR_SUCCESS;
    }

    struct stats child;
    child.parent = stats;
    child.stat = *finfo;

    apr_status_t status = traverse_recursive(conf, fullname, gc_pool, &child, current_ctx);
    if (status != APR_SUCCESS) {
        char errbuf[ERR_BUF_SIZE];
        DEBUG_ERR("error recursively calling traverse_recursive: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
    return status;
}

static apr_status_t process_directory(const char *filename, ft_conf_t *conf, apr_pool_t *gc_pool, const struct stats *stats, ft_ignore_context_t *parent_ctx)
{
    apr_dir_t *dir = NULL;
    apr_finfo_t finfo;
    apr_status_t status = apr_dir_open(&dir, filename, gc_pool);
    if (status != APR_SUCCESS) {
        char errbuf[ERR_BUF_SIZE];
        DEBUG_ERR("error calling apr_dir_open(%s): %s", filename, apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    ft_ignore_context_t *current_ctx = parent_ctx;
    if (conf->respect_gitignore) {
        const char *gitignore_path = apr_pstrcat(gc_pool, filename, "/.gitignore", NULL);
        if (APR_SUCCESS == apr_stat(&finfo, gitignore_path, APR_FINFO_TYPE, gc_pool) && finfo.filetype == APR_REG) {
            ft_ignore_context_t *local_ctx = ft_ignore_context_create(gc_pool, parent_ctx, filename);
            if (APR_SUCCESS == ft_ignore_load_file(local_ctx, gitignore_path)) {
                current_ctx = local_ctx;
            }
        }
    }

    while (apr_dir_read(&finfo, APR_FINFO_NAME | APR_FINFO_TYPE, dir) == APR_SUCCESS && finfo.name != NULL) {
        if (napr_hash_search(conf->ig_files, finfo.name, strlen(finfo.name), NULL) != NULL || (finfo.name[0] == '.' && !is_option_set(conf->mask, OPTION_SHOW_HIDDEN))) {
            continue;
        }
        if (finfo.filetype == APR_DIR && !is_option_set(conf->mask, OPTION_RECSD)) {
            continue;
        }

        char *fullname = apr_pstrcat(gc_pool, filename, (filename[strlen(filename) - 1] == '/') ? "" : "/", finfo.name, NULL);
        status = process_directory_entry(fullname, &finfo, conf, gc_pool, stats, current_ctx);
        if (status != APR_SUCCESS) {
            break;
        }
    }

    apr_dir_close(dir);
    return status;
}

static apr_status_t traverse_recursive(ft_conf_t *conf, const char *filename, apr_pool_t *gc_pool, struct stats const *stats, ft_ignore_context_t *parent_ctx)
{
    apr_finfo_t finfo;
    apr_status_t status = get_file_info(filename, &finfo, conf, gc_pool);

    if (status != APR_SUCCESS) {
        return status;
    }
    if (conf->respect_gitignore && parent_ctx && ft_ignore_match(parent_ctx, filename, finfo.filetype == APR_DIR) == FT_IGNORE_MATCH_IGNORED) {
        return APR_SUCCESS;
    }
    if (check_permissions(&finfo, conf) != APR_SUCCESS) {
        return APR_SUCCESS;
    }

    if (finfo.filetype == APR_DIR) {
        return process_directory(filename, conf, gc_pool, stats, parent_ctx);
    }
    if (finfo.filetype == APR_REG || (finfo.filetype == APR_LNK && is_option_set(conf->mask, OPTION_FSYML))) {
        return process_file(filename, &finfo, conf);
    }

    return APR_SUCCESS;
}

apr_status_t ft_traverse_path(ft_conf_t *conf, const char *path)
{
    apr_pool_t *gc_pool = NULL;
    apr_status_t status = APR_SUCCESS;

    status = apr_pool_create(&gc_pool, conf->pool);
    if (APR_SUCCESS != status) {
        char errbuf[ERR_BUF_SIZE] = { 0 };
        DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    status = traverse_recursive(conf, path, gc_pool, NULL, conf->global_ignores);

    apr_pool_destroy(gc_pool);

    return status;
}
