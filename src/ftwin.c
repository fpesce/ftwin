/*
 *
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
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

#include <pcre.h>

#include <unistd.h>		/* getegid */
#include <stdio.h>		/* fgetgrent */
#include <sys/stat.h>		/* umask */
#include <sys/types.h>		/* fgetgrent */
#include <grp.h>		/* fgetgrent */

#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_getopt.h>
#include <napr_hash.h>
#include <apr_strings.h>
#include <apr_user.h>

#include "config.h"

#if HAVE_PUZZLE
#include <puzzle.h>
#endif

#if HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include "checksum.h"
#include "debug.h"
#include "ft_file.h"
#include "napr_heap.h"

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

#if HAVE_PUZZLE
#define OPTION_PUZZL 0x0080
#endif

#if HAVE_ARCHIVE
#define OPTION_UNTAR 0x0100
#endif

typedef struct ft_file_t
{
    apr_off_t size;
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
    apr_uint32_t val_array[HASHSTATE];	/* 256 bits (using Bob Jenkins http://www.burtleburtle.net/bob/c/checksum.c) */
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

typedef struct ft_conf_t
{
    apr_off_t minsize;
    apr_off_t excess_size;	/* Switch off mmap behavior */
#if HAVE_PUZZLE
    double threshold;
#endif
    apr_pool_t *pool;		/* Always needed somewhere ;) */
    napr_heap_t *heap;		/* Will holds the files */
    napr_hash_t *sizes;		/* will holds the sizes hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    napr_hash_t *gids;		/* will holds the gids hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    napr_hash_t *ig_files;
    pcre *ig_regex;
    pcre *wl_regex;
    pcre *ar_regex;		/* archive regex */
    char *p_path;		/* priority path */
    char *username;
    apr_size_t p_path_len;
    apr_uid_t userid;
    apr_gid_t groupid;
    unsigned short int mask;
    char sep;
} ft_conf_t;

struct stats
{
    struct stats const *parent;
    apr_finfo_t stat;
};

static int ft_file_cmp(const void *param1, const void *param2)
{
    const ft_file_t *file1 = param1;
    const ft_file_t *file2 = param2;

    if (file1->size < file2->size)
	return -1;
    else if (file2->size < file1->size)
	return 1;

    return 0;
}

static const void *ft_fsize_get_key(const void *opaque)
{
    const ft_fsize_t *fsize = opaque;

    return &(fsize->val);
}

static const void *ft_gids_get_key(const void *opaque)
{
    const ft_gid_t *gid = opaque;

    return &(gid->val);
}


static apr_size_t get_one(const void *opaque)
{
    return 1;
}

static int apr_uint32_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    apr_uint32_t i1 = *(apr_uint32_t *) key1;
    apr_uint32_t i2 = *(apr_uint32_t *) key2;

    return (i1 == i2) ? 0 : 1;
}

/* http://www.burtleburtle.net/bob/hash/integer.html */
static apr_uint32_t apr_uint32_key_hash(const void *key, apr_size_t klen)
{
    apr_uint32_t i = *(apr_uint32_t *) key;

    i = (i + 0x7ed55d16) + (i << 12);
    i = (i ^ 0xc761c23c) ^ (i >> 19);
    i = (i + 0x165667b1) + (i << 5);
    i = (i + 0xd3a2646c) ^ (i << 9);
    i = (i + 0xfd7046c5) + (i << 3);
    i = (i ^ 0xb55a4f09) ^ (i >> 16);

    return i;
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

	filename = end + 1;
    } while ((NULL != end) && ('\0' != *filename));
}

/**
 * The function used to add recursively or not file and dirs.
 * @param conf Configuration structure.
 * @param filename name of a file or directory to add to the list of twinchecker.
 * @param gc_pool garbage collecting pool, will be cleaned by the caller.
 * @return APR_SUCCESS if no error occured.
 */
#define MATCH_VECTOR_SIZE 210
static apr_status_t ft_conf_add_file(ft_conf_t *conf, const char *filename, apr_pool_t *gc_pool, struct stats const *stats)
{
    int ovector[MATCH_VECTOR_SIZE];
    char errbuf[128];
    apr_finfo_t finfo;
    apr_dir_t *dir;
    apr_int32_t statmask =
	APR_FINFO_SIZE | APR_FINFO_TYPE | APR_FINFO_USER | APR_FINFO_GROUP | APR_FINFO_UPROT | APR_FINFO_GPROT;
    apr_size_t fname_len = 0;
    apr_uint32_t hash_value;
    apr_status_t status;
    int rc;

    /* Step 1 : Check if it's a directory and get the size if not */
    if (!is_option_set(conf->mask, OPTION_FSYML))
	statmask |= APR_FINFO_LINK;

    if (APR_SUCCESS != (status = apr_stat(&finfo, filename, statmask, gc_pool))) {
	if (is_option_set(conf->mask, OPTION_FSYML)) {
	    statmask ^= APR_FINFO_LINK;
	    if ((APR_SUCCESS == apr_stat(&finfo, filename, statmask, gc_pool)) && (finfo.filetype & APR_LNK)) {
		if (is_option_set(conf->mask, OPTION_VERBO))
		    fprintf(stderr, "Skipping : [%s] (broken link)\n", filename);
		return APR_SUCCESS;
	    }
	}

	DEBUG_ERR("error calling apr_stat on filename %s : %s", filename, apr_strerror(status, errbuf, 128));
	return status;
    }

    /* Step 1-bis, if we don't own the right to read it, skip it */
    if (0 != conf->userid) {
	if (finfo.user == conf->userid) {
	    if (!(APR_UREAD & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO))
		    fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		return APR_SUCCESS;
	    }
	}
	else if (NULL != napr_hash_search(conf->gids, &finfo.group, 1, &hash_value)) {
	    if (!(APR_GREAD & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO))
		    fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		return APR_SUCCESS;
	    }
	}
	else if (!(APR_WREAD & finfo.protection)) {
	    if (is_option_set(conf->mask, OPTION_VERBO))
		fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
	    return APR_SUCCESS;
	}
    }

    /* Step 2: If it is, browse it */
    if (APR_DIR == finfo.filetype) {
	if (0 != conf->userid) {
	    if (finfo.user == conf->userid) {
		if (!(APR_UEXECUTE & finfo.protection)) {
		    if (is_option_set(conf->mask, OPTION_VERBO))
			fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		    return APR_SUCCESS;
		}
	    }
	    else if (NULL != napr_hash_search(conf->gids, &finfo.group, 1, &hash_value)) {
		if (!(APR_GEXECUTE & finfo.protection)) {
		    if (is_option_set(conf->mask, OPTION_VERBO))
			fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		    return APR_SUCCESS;
		}
	    }
	    else if (!(APR_WEXECUTE & finfo.protection)) {
		if (is_option_set(conf->mask, OPTION_VERBO))
		    fprintf(stderr, "Skipping : [%s] (bad permission)\n", filename);
		return APR_SUCCESS;
	    }
	}

	if (APR_SUCCESS != (status = apr_dir_open(&dir, filename, gc_pool))) {
	    DEBUG_ERR("error calling apr_dir_open(%s): %s", filename, apr_strerror(status, errbuf, 128));
	    return status;
	}
	fname_len = strlen(filename);
	while ((APR_SUCCESS == (status = apr_dir_read(&finfo, APR_FINFO_NAME | APR_FINFO_TYPE, dir)))
	       && (NULL != finfo.name)) {
	    /* Check if it has to be ignored */
	    char *fullname;
	    apr_size_t fullname_len;
	    /* for recursive loop detection */
	    struct stats child;
	    struct stats const *ancestor;

	    if (NULL != napr_hash_search(conf->ig_files, finfo.name, strlen(finfo.name), NULL))
		continue;

	    if (APR_DIR == finfo.filetype && !is_option_set(conf->mask, OPTION_RECSD))
		continue;

	    fullname = apr_pstrcat(gc_pool, filename, ('/' == filename[fname_len - 1]) ? "" : "/", finfo.name, NULL);
	    fullname_len = strlen(fullname);

	    if ((NULL != conf->ig_regex) && (APR_DIR != finfo.filetype)
		&& (0 <= (rc = pcre_exec(conf->ig_regex, NULL, fullname, fullname_len, 0, 0, ovector, MATCH_VECTOR_SIZE))))
		continue;

	    if ((NULL != conf->wl_regex) && (APR_DIR != finfo.filetype)
		&& (0 > (rc = pcre_exec(conf->wl_regex, NULL, fullname, fullname_len, 0, 0, ovector, MATCH_VECTOR_SIZE))))
		continue;

	    if (stats) {
		if (stats->stat.inode)
		    for (ancestor = stats; (ancestor = ancestor->parent) != 0;)
			if (ancestor->stat.inode == stats->stat.inode && ancestor->stat.device == stats->stat.device) {
			    if (is_option_set(conf->mask, OPTION_VERBO))
				fprintf(stderr, "Warning: %s: recursive directory loop\n", filename);
			    /* skip it */
			    return APR_SUCCESS;
			}
	    }
	    child.parent = stats;
	    child.stat = finfo;

	    status = ft_conf_add_file(conf, fullname, gc_pool, &child);

	    if (APR_SUCCESS != status) {
		DEBUG_ERR("error recursively calling ft_conf_add_file: %s", apr_strerror(status, errbuf, 128));
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
	apr_off_t finfosize;
	char *fname;
#if HAVE_ARCHIVE
	const char *subpath;
	/* XXX La */
	struct archive *a = NULL;
	struct archive_entry *entry = NULL;
	int rv;
#endif

	fname = apr_pstrdup(conf->pool, filename);
	finfosize = finfo.size;
#if HAVE_ARCHIVE
	subpath = NULL;
	fname_len = strlen(filename);
	if (is_option_set(conf->mask, OPTION_UNTAR)) {
	    if ((NULL != conf->ar_regex)
		&& (0 <= (rc = pcre_exec(conf->ar_regex, NULL, filename, fname_len, 0, 0, ovector, MATCH_VECTOR_SIZE)))) {
		a = archive_read_new();
		if (NULL == a) {
		    DEBUG_ERR("error calling archive_read_new()");
		    return APR_EGENERAL;
		}
		rv = archive_read_support_compression_all(a);
		if (0 != rv) {
		    DEBUG_ERR("error calling archive_read_support_compression_all(): %s", archive_error_string(a));
		    return APR_EGENERAL;
		}
		rv = archive_read_support_format_all(a);
		if (0 != rv) {
		    DEBUG_ERR("error calling archive_read_support_format_all(): %s", archive_error_string(a));
		    return APR_EGENERAL;
		}
		rv = archive_read_open_file(a, filename, 10240);
		if (0 != rv) {
		    DEBUG_ERR("error calling archive_read_open_file(%s): %s", filename, archive_error_string(a));
		    return APR_EGENERAL;
		}
	    }
	}

	do {
#endif
	    if (finfosize >= conf->minsize
#if HAVE_ARCHIVE
		&& ((NULL == a) || ((NULL != entry) && !(AE_IFDIR & archive_entry_filetype(entry))))
#endif
		) {
		ft_file_t *file;
		ft_fsize_t *fsize;

		file = apr_palloc(conf->pool, sizeof(struct ft_file_t));
		file->path = fname;
		file->size = finfosize;
#if HAVE_ARCHIVE
		if (subpath) {
		    file->subpath = apr_pstrdup(conf->pool, subpath);
		}
		else {
		    file->subpath = NULL;
		}
#endif
		if ((conf->p_path) && (fname_len >= conf->p_path_len)
		    && ((is_option_set(conf->mask, OPTION_ICASE) && !strncasecmp(filename, conf->p_path, conf->p_path_len))
			|| (!is_option_set(conf->mask, OPTION_ICASE) && !memcmp(filename, conf->p_path, conf->p_path_len)))) {
		    file->prioritized |= 0x1;
		}
		else {
		    file->prioritized &= 0x0;
		}
#if HAVE_PUZZLE
		file->cvec_ok &= 0x0;
#endif
		napr_heap_insert(conf->heap, file);

		if (NULL == (fsize = napr_hash_search(conf->sizes, &finfosize, 1, &hash_value))) {
		    fsize = apr_palloc(conf->pool, sizeof(struct ft_fsize_t));
		    fsize->val = finfosize;
		    fsize->chksum_array = NULL;
		    fsize->nb_checksumed = 0;
		    fsize->nb_files = 0;
		    napr_hash_set(conf->sizes, fsize, hash_value);
		}
		fsize->nb_files++;
	    }
#if HAVE_ARCHIVE
	    if (a) {
		rv = archive_read_next_header(a, &entry);
		if (ARCHIVE_EOF != rv) {
		    if (ARCHIVE_OK == rv) {
			finfosize = archive_entry_size(entry);
			subpath = archive_entry_pathname(entry);
		    }
		    else {
			/*
			 * if this is the first all to read_next_header, we may
			 * be processing a bad file, ignore it silently.
			 */
			if (NULL != subpath) {
			    DEBUG_ERR("error calling archive_read_next_header(%s): %s", fname, archive_error_string(a));
			    return APR_EGENERAL;
			}
			else {
			    break;
			}
		    }
		}
	    }
	} while (a && (ARCHIVE_EOF != rv));
	if (a)
	    archive_read_finish(a);
#endif
    }

    return APR_SUCCESS;
}

#if HAVE_ARCHIVE
static int copy_data(struct archive *ar, struct archive *aw)
{
    const void *buff;
    off_t offset;
    size_t size;
    int rv;

    for (;;) {
	rv = archive_read_data_block(ar, &buff, &size, &offset);
	if (rv == ARCHIVE_EOF) {
	    return ARCHIVE_OK;
	}
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error calling archive_read_data_block(): %s", archive_error_string(ar));
	    return rv;
	}
	rv = archive_write_data_block(aw, buff, size, offset);
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error calling archive_write_data_block(): %s", archive_error_string(aw));
	    return rv;
	}
    }
}

static char *ft_untar_file(ft_file_t *file, apr_pool_t *p)
{
    struct archive *a = NULL;
    struct archive *ext = NULL;
    struct archive_entry *entry = NULL;
    char *tmpfile = NULL;
    int rv;

    a = archive_read_new();
    if (NULL == a) {
	DEBUG_ERR("error calling archive_read_new()");
	return NULL;
    }
    rv = archive_read_support_compression_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_compression_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_support_format_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_format_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_open_file(a, file->path, 10240);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_open_file(%s): %s", file->path, archive_error_string(a));
	return NULL;
    }

    ext = archive_write_disk_new();
    if (NULL == ext) {
	DEBUG_ERR("error calling archive_write_disk_new()");
	return NULL;
    }

    for (;;) {
	rv = archive_read_next_header(a, &entry);
	if (rv == ARCHIVE_EOF) {
	    DEBUG_ERR("subpath [%s] not found in archive [%s]", file->subpath, file->path);
	    return NULL;
	}
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(a));
	    return NULL;
	}

	if (!strcmp(file->subpath, archive_entry_pathname(entry))) {
	    mode_t current_mode = umask(S_IRWXG | S_IRWXO);

	    /*
	     * All I want is only a temporary filename, but gcc outputs me an
	     * ugly warning if I use tempnam...
	     * tmpfile = tempnam("/tmp/", "ftwin");
	     */
	    tmpfile = apr_pstrdup(p, "/tmp/ftwinXXXXXX");
	    rv = mkstemp(tmpfile);
	    if (rv < 0) {
		DEBUG_ERR("error creating tmpfile %s", tmpfile);
		return NULL;
	    }
	    umask(current_mode);
	    close(rv);

	    archive_entry_copy_pathname(entry, tmpfile);

	    rv = archive_write_header(ext, entry);
	    if (rv == ARCHIVE_OK) {
		rv = copy_data(a, ext);
		if (rv != ARCHIVE_OK) {
		    DEBUG_ERR("error while copying data from archive (%s)", file->path);
		    apr_file_remove(tmpfile, p);
		    return NULL;
		}
	    }
	    else {
		DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(a));
		apr_file_remove(tmpfile, p);
		return NULL;
	    }

	    break;
	}
    }

    archive_write_finish(ext);
    archive_read_finish(a);

    return tmpfile;
}

#endif

static apr_status_t ft_conf_process_sizes(ft_conf_t *conf)
{
    char errbuf[128];
    ft_file_t *file;
    ft_fsize_t *fsize;
    napr_heap_t *tmp_heap;
    apr_pool_t *gc_pool;
    apr_uint32_t hash_value;
    apr_status_t status;
    apr_size_t nb_processed, nb_files;

    if (is_option_set(conf->mask, OPTION_VERBO))
	fprintf(stderr, "Referencing files and sizes:\n");

    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, conf->pool))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }
    nb_processed = 0;
    nb_files = napr_heap_size(conf->heap);

    tmp_heap = napr_heap_make(conf->pool, ft_file_cmp);
    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, 1, &hash_value))) {
	    /* More than two files, we will need to checksum because :
	     * - 1 file of a size means no twin.
	     * - 2 files of a size means that anyway we must read the both, so
	     *   we will probably cmp them at that time instead of running CPU
	     *   intensive function like checksum.
	     */
	    if (1 == fsize->nb_files) {
		/* No twin possible, remove the entry */
		/*DEBUG_DBG("only one file of size %"APR_OFF_T_FMT, fsize->val); */
		napr_hash_remove(conf->sizes, fsize, hash_value);
	    }
	    else {
		if (NULL == fsize->chksum_array)
		    fsize->chksum_array = apr_palloc(conf->pool, fsize->nb_files * sizeof(struct ft_chksum_t));

		fsize->chksum_array[fsize->nb_checksumed].file = file;
		/* no multiple check, just a memcmp will be needed, don't call checksum on 0-length file too */
		if ((2 == fsize->nb_files) || (0 == fsize->val)) {
		    /*DEBUG_DBG("two files of size %"APR_OFF_T_FMT, fsize->val); */
		    memset(fsize->chksum_array[fsize->nb_checksumed].val_array, 0, HASHSTATE * sizeof(apr_int32_t));
		}
		else {
		    char *filepath;
#if HAVE_ARCHIVE
		    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != file->subpath)) {
			filepath = ft_untar_file(file, gc_pool);
			if (NULL == filepath) {
			    DEBUG_ERR("error calling ft_untar_file");
			    apr_pool_destroy(gc_pool);
			    return APR_EGENERAL;
			}
		    }
		    else {
			filepath = file->path;
		    }
#else
		    filepath = file->path;
#endif
		    status =
			checksum_file(filepath, file->size, conf->excess_size,
				      fsize->chksum_array[fsize->nb_checksumed].val_array, gc_pool);
#if HAVE_ARCHIVE
		    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != file->subpath))
			apr_file_remove(filepath, gc_pool);
#endif
		    /*
		     * no return status if != APR_SUCCESS , because : 
		     * Fault-check has been removed in case files disappear
		     * between collecting and comparing or special files (like
		     * device or /proc) are tried to access
		     */
		    if ((APR_SUCCESS != status) && (is_option_set(conf->mask, OPTION_VERBO)))
			fprintf(stderr, "\nskipping %s because: %s\n", file->path, apr_strerror(status, errbuf, 128));
		}
		if (APR_SUCCESS == status) {
		    fsize->nb_checksumed++;
		    napr_heap_insert(tmp_heap, file);
		}
	    }
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		fprintf(stderr, "\rProgress [%" APR_SIZE_T_FMT "/%" APR_SIZE_T_FMT "] %d%% ", nb_processed, nb_files,
			(int) ((float) nb_processed / (float) nb_files * 100.0));
		nb_processed++;
	    }
	}
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
	    apr_pool_destroy(gc_pool);
	    return APR_EGENERAL;
	}
    }
    if (is_option_set(conf->mask, OPTION_VERBO)) {
	fprintf(stderr, "\rProgress [%" APR_SIZE_T_FMT "/%" APR_SIZE_T_FMT "] %d%% ", nb_processed, nb_files,
		(int) ((float) nb_processed / (float) nb_files * 100.0));
	fprintf(stderr, "\n");
    }

    apr_pool_destroy(gc_pool);
    conf->heap = tmp_heap;

    return APR_SUCCESS;
}

static int chksum_cmp(const void *chksum1, const void *chksum2)
{
    const ft_chksum_t *chk1 = chksum1;
    const ft_chksum_t *chk2 = chksum2;
    int i;

    if (0 == (i = memcmp(chk1->val_array, chk2->val_array, HASHSTATE))) {
	return chk1->file->prioritized - chk2->file->prioritized;
    }
    else {
	i <<= 1;
    }

    return i;
}

#if HAVE_PUZZLE

static apr_status_t ft_conf_image_twin_report(ft_conf_t *conf)
{
    PuzzleContext context;
    double d;
    ft_file_t *file, *file_cmp;
    int i, heap_size;
    unsigned char already_printed;

    puzzle_init_context(&context);
    puzzle_set_max_width(&context, 5000);
    puzzle_set_max_height(&context, 5000);
    puzzle_set_lambdas(&context, 13);

    heap_size = napr_heap_size(conf->heap);
    for (i = 0; i < heap_size; i++) {

	file = napr_heap_get_nth(conf->heap, i);
	puzzle_init_cvec(&context, &(file->cvec));
	if (0 == puzzle_fill_cvec_from_file(&context, &(file->cvec), file->path)) {
	    file->cvec_ok |= 0x1;
	}
	else {
	    DEBUG_ERR("error calling puzzle_fill_cvec_from_file, ignoring file: %s", file->path);
	}

	if (is_option_set(conf->mask, OPTION_VERBO)) {
	    fprintf(stderr, "\rProgress [%i/%i] %d%% ", i, heap_size, (int) ((float) i / (float) heap_size * 100.0));
	}
    }
    if (is_option_set(conf->mask, OPTION_VERBO)) {
	fprintf(stderr, "\rProgress [%i/%i] %d%% ", i, heap_size, (int) ((float) i / (float) heap_size * 100.0));
	fprintf(stderr, "\n");
    }

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (!(file->cvec_ok & 0x1))
	    continue;
	already_printed = 0;
	heap_size = napr_heap_size(conf->heap);
	for (i = 0; i < heap_size; i++) {
	    file_cmp = napr_heap_get_nth(conf->heap, i);
	    if (!(file_cmp->cvec_ok & 0x1))
		continue;

	    d = puzzle_vector_normalized_distance(&context, &(file->cvec), &(file_cmp->cvec), 0);
	    if (d < conf->threshold) {
		if (!already_printed) {
		    printf("%s%c", file->path, conf->sep);
		    already_printed = 1;
		}
		else {
		    printf("%c", conf->sep);
		}
		printf("%s", file_cmp->path);
	    }
	}

	if (already_printed)
	    printf("\n\n");

	puzzle_free_cvec(&context, &(file->cvec));
    }

    puzzle_free_context(&context);

    return APR_SUCCESS;
}
#endif

static apr_status_t ft_conf_twin_report(ft_conf_t *conf)
{
    char errbuf[128];
    apr_off_t old_size = -1;
    ft_file_t *file;
    ft_fsize_t *fsize;
    apr_uint32_t hash_value;
    apr_size_t i, j;
    int rv;
    apr_status_t status;
    unsigned char already_printed;
    apr_uint32_t chksum_array_sz = 0U;

    if (is_option_set(conf->mask, OPTION_VERBO))
	fprintf(stderr, "Reporting duplicate files:\n");

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (file->size == old_size)
	    continue;

	old_size = file->size;
	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, 1, &hash_value))) {
	    chksum_array_sz = MIN(fsize->nb_files, fsize->nb_checksumed);
	    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), chksum_cmp);
	    for (i = 0; i < fsize->nb_files; i++) {
		if (NULL == fsize->chksum_array[i].file)
		    continue;
		already_printed = 0;
		for (j = i + 1; j < fsize->nb_files; j++) {
		    if (0 == memcmp(fsize->chksum_array[i].val_array, fsize->chksum_array[j].val_array, HASHSTATE)) {
			char *fpathi, *fpathj;
#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath) {
				fpathi = ft_untar_file(fsize->chksum_array[i].file, conf->pool);
				if (NULL == fpathi) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathi = fsize->chksum_array[i].file->path;
			    }
			    if (NULL != fsize->chksum_array[j].file->subpath) {
				fpathj = ft_untar_file(fsize->chksum_array[j].file, conf->pool);
				if (NULL == fpathj) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathj = fsize->chksum_array[j].file->path;
			    }
			}
			else {
			    fpathi = fsize->chksum_array[i].file->path;
			    fpathj = fsize->chksum_array[j].file->path;
			}
#else
			fpathi = fsize->chksum_array[i].file->path;
			fpathj = fsize->chksum_array[j].file->path;
#endif
			status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);
#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath)
				apr_file_remove(fpathi, conf->pool);
			    if (NULL != fsize->chksum_array[j].file->subpath)
				apr_file_remove(fpathj, conf->pool);
			}
#endif
			/*
			 * no return status if != APR_SUCCESS , because : 
			 * Fault-check has been removed in case files disappear
			 * between collecting and comparing or special files (like
			 * device or /proc) are tried to access
			 */
			if (APR_SUCCESS != status) {
			    if (is_option_set(conf->mask, OPTION_VERBO))
				fprintf(stderr, "\nskipping %s and %s comparison because: %s\n",
					fsize->chksum_array[i].file->path, fsize->chksum_array[j].file->path,
					apr_strerror(status, errbuf, 128));
			    rv = 1;
			}

			if (0 == rv) {
			    if (!already_printed) {
				if (is_option_set(conf->mask, OPTION_SIZED))
				    printf("size [%" APR_OFF_T_FMT "]:\n", fsize->val);
#if HAVE_ARCHIVE
				if (is_option_set(conf->mask, OPTION_UNTAR)
				    && (NULL != fsize->chksum_array[i].file->subpath))
				    printf("%s%c%s%c", fsize->chksum_array[i].file->path, (':' != conf->sep) ? ':' : '|',
					   fsize->chksum_array[i].file->subpath, conf->sep);
				else
#endif
				    printf("%s%c", fsize->chksum_array[i].file->path, conf->sep);
				already_printed = 1;
			    }
			    else {
				printf("%c", conf->sep);
			    }
#if HAVE_ARCHIVE
			    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != fsize->chksum_array[j].file->subpath))
				printf("%s%c%s", fsize->chksum_array[j].file->path, (':' != conf->sep) ? ':' : '|',
				       fsize->chksum_array[j].file->subpath);
			    else
#endif
				printf("%s", fsize->chksum_array[j].file->path);
			    /* mark j as a twin ! */
			    fsize->chksum_array[j].file = NULL;
			    fflush(stdout);
			}
		    }
		    else {
			/* hash are ordered, so at first mismatch we check the next */
			break;
		    }
		}
		if (already_printed)
		    printf("\n\n");
	    }
	}
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
	    return APR_EGENERAL;
	}
    }

    return APR_SUCCESS;
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
	gid = napr_hash_search(gids, &(list[i]), 1, &hash_value);
	if (NULL == gid) {
	    gid = apr_palloc(p, sizeof(struct ft_gid_t));
	    gid->val = list[i];
	    napr_hash_set(gids, gid, hash_value);
	}
    }

    return APR_SUCCESS;
}

int main(int argc, const char **argv)
{
    static const apr_getopt_option_t opt_option[] = {
	{"case-unsensitive", 'c', FALSE, "this option applies to regex match."},
	{"display-size", 'd', FALSE, "\tdisplay size before duplicates."},
	{"regex-ignore-file", 'e', TRUE, "filenames that match this are ignored."},
	{"follow-symlink", 'f', FALSE, "follow symbolic links."},
	{"help", 'h', FALSE, "\t\tdisplay usage."},
#if HAVE_PUZZLE
	{"image-cmp", 'I', FALSE, "\twill run ftwin in image cmp mode (using libpuzzle)."},
	{"image-threshold", 'T', TRUE,
	 "will change the image similarity threshold\n\t\t\t\t (default is [1], accepted [2/3/4/5])."},
#endif
	{"ignore-list", 'i', TRUE, "\tcomma-separated list of file names to ignore."},
	{"minimal-length", 'm', TRUE, "minimum size of file to process."},
	{"optimize-memory", 'o', FALSE, "reduce memory usage, but increase process time."},
	{"priority-path", 'p', TRUE, "\tfile in this path are displayed first when\n\t\t\t\tduplicates are reported."},
	{"recurse-subdir", 'r', FALSE, "recurse subdirectories."},
	{"separator", 's', TRUE, "\tseparator character between twins, default: \\n."},
#if HAVE_ARCHIVE
	{"tar-cmp", 't', FALSE, "\twill process files archived in .tar default: off."},
#endif
	{"verbose", 'v', FALSE, "\tdisplay a progress bar."},
	{"version", 'V', FALSE, "\tdisplay version."},
	{"whitelist-regex-file", 'w', TRUE, "filenames that doesn't match this are ignored."},
	{"excessive-size", 'x', TRUE, "excessive size of file that switch off mmap use."},
	{NULL, 0, 0, NULL},	/* end (a.k.a. sentinel) */
    };
    char errbuf[128];
    char *regex = NULL, *wregex = NULL, *arregex = NULL;
    ft_conf_t conf;
    apr_getopt_t *os;
    apr_pool_t *pool, *gc_pool;
    apr_uint32_t hash_value;
    const char *optarg;
    int optch, i;
    apr_status_t status;

    if (APR_SUCCESS != (status = apr_initialize())) {
	DEBUG_ERR("error calling apr_initialize: %s", apr_strerror(status, errbuf, 128));
	return -1;
    }

    if (APR_SUCCESS != (status = apr_pool_create(&pool, NULL))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    if (APR_SUCCESS != (status = apr_getopt_init(&os, pool, argc, argv))) {
	DEBUG_ERR("error calling apr_getopt_init: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    conf.pool = pool;
    conf.heap = napr_heap_make(pool, ft_file_cmp);
    conf.ig_files = napr_hash_str_make(pool, 32, 8);
    conf.sizes = napr_hash_make(pool, 4096, 8, ft_fsize_get_key, get_one, apr_uint32_key_cmp, apr_uint32_key_hash);
    conf.gids = napr_hash_make(pool, 4096, 8, ft_gids_get_key, get_one, apr_uint32_key_cmp, apr_uint32_key_hash);
    /* To avoid endless loop, ignore looping directory ;) */
    napr_hash_search(conf.ig_files, ".", 1, &hash_value);
    napr_hash_set(conf.ig_files, ".", hash_value);
    napr_hash_search(conf.ig_files, "..", 2, &hash_value);
    napr_hash_set(conf.ig_files, "..", hash_value);
    conf.ig_regex = NULL;
    conf.wl_regex = NULL;
    conf.ar_regex = NULL;
    conf.p_path = NULL;
    conf.p_path_len = 0;
    conf.minsize = 0;
    conf.sep = '\n';
    conf.excess_size = 50 * 1024 * 1024;
    conf.mask = 0x0000;
#if HAVE_ARCHIVE
    conf.threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;
#endif

    while (APR_SUCCESS == (status = apr_getopt_long(os, opt_option, &optch, &optarg))) {
	switch (optch) {
	case 'c':
	    set_option(&conf.mask, OPTION_ICASE, 1);
	    break;
	case 'd':
	    set_option(&conf.mask, OPTION_SIZED, 1);
	    break;
	case 'e':
	    regex = apr_pstrdup(pool, optarg);
	    break;
	case 'f':
	    set_option(&conf.mask, OPTION_FSYML, 1);
	    break;
	case 'h':
	    usage(argv[0], opt_option);
	    return 0;
	case 'i':
	    ft_hash_add_ignore_list(conf.ig_files, optarg);
	    break;
#if HAVE_PUZZLE
	case 'I':
	    set_option(&conf.mask, OPTION_ICASE, 1);
	    set_option(&conf.mask, OPTION_PUZZL, 1);
	    wregex = apr_pstrdup(pool, ".*\\.(gif|png|jpe?g)$");
	    break;
	case 'T':
	    switch (*optarg) {
	    case '1':
		conf.threshold = PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD;
		break;
	    case '2':
		conf.threshold = PUZZLE_CVEC_SIMILARITY_LOW_THRESHOLD;
		break;
	    case '3':
		conf.threshold = 0.5;
		break;
	    case '4':
		conf.threshold = PUZZLE_CVEC_SIMILARITY_THRESHOLD;
		break;
	    case '5':
		conf.threshold = PUZZLE_CVEC_SIMILARITY_HIGH_THRESHOLD;
		break;
	    default:
		usage(argv[0], opt_option);
		DEBUG_ERR("invalid threshold: %s", optarg);
		apr_terminate();
		return -1;
	    }
	    break;
#endif
	case 'm':
	    conf.minsize = strtoul(optarg, NULL, 10);
	    if (ULONG_MAX == conf.minsize) {
		DEBUG_ERR("can't parse %s for -m / --minimal-length", optarg);
		apr_terminate();
		return -1;
	    }
	    break;
	case 'o':
	    set_option(&conf.mask, OPTION_OPMEM, 1);
	    break;
	case 'p':
	    conf.p_path = apr_pstrdup(pool, optarg);
	    conf.p_path_len = strlen(conf.p_path);
	    break;
	case 'r':
	    set_option(&conf.mask, OPTION_RECSD, 1);
	    break;
	case 's':
	    conf.sep = *optarg;
	    break;
#if HAVE_ARCHIVE
	case 't':
	    set_option(&conf.mask, OPTION_UNTAR, 1);
	    arregex = apr_pstrdup(pool, ".*(\\.tar)?\\.(gz|Z|bz2)$");
	    break;
#endif
	case 'v':
	    set_option(&conf.mask, OPTION_VERBO, 1);
	    break;
	case 'V':
	    version();
	    return 0;
	case 'w':
	    wregex = apr_pstrdup(pool, optarg);
	    break;
	case 'x':
	    conf.excess_size = strtoul(optarg, NULL, 10);
	    if (ULONG_MAX == conf.minsize) {
		DEBUG_ERR("can't parse %s for -x / --excessive-size", optarg);
		apr_terminate();
		return -1;
	    }
	    break;
	}
    }

    if (APR_SUCCESS != (status = apr_uid_current(&(conf.userid), &(conf.groupid), pool))) {
	DEBUG_ERR("error calling apr_uid_current: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    if (APR_SUCCESS != (status = apr_uid_name_get(&(conf.username), conf.userid, pool))) {
	DEBUG_ERR("error calling apr_uid_name_get: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    if (APR_SUCCESS != (status = fill_gids_ht(conf.username, conf.gids, pool))) {
	DEBUG_ERR("error calling fill_gids_ht: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    if (NULL != regex) {
	conf.ig_regex = ft_pcre_compile(regex, is_option_set(conf.mask, OPTION_ICASE), pool);
	if (NULL == conf.ig_regex) {
	    apr_terminate();
	    return -1;
	}
    }

    if (NULL != wregex) {
	conf.wl_regex = ft_pcre_compile(wregex, is_option_set(conf.mask, OPTION_ICASE), pool);
	if (NULL == conf.wl_regex) {
	    apr_terminate();
	    return -1;
	}
    }

    if (NULL != arregex) {
	conf.ar_regex = ft_pcre_compile(arregex, is_option_set(conf.mask, OPTION_ICASE), pool);
	if (NULL == conf.ar_regex) {
	    apr_terminate();
	    return -1;
	}
    }

    /* Step 1 : Browse the file */
    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, pool))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }
    for (i = os->ind; i < argc; i++) {
	if (APR_SUCCESS != (status = ft_conf_add_file(&conf, argv[i], gc_pool, NULL))) {
	    DEBUG_ERR("error calling ft_conf_add_file: %s", apr_strerror(status, errbuf, 128));
	    apr_terminate();
	    return -1;
	}
    }
    apr_pool_destroy(gc_pool);

    if (0 < napr_heap_size(conf.heap)) {
#if HAVE_PUZZLE
	if (is_option_set(conf.mask, OPTION_PUZZL)) {
	    /* Step 2: Report the image twins */
	    if (APR_SUCCESS != (status = ft_conf_image_twin_report(&conf))) {
		DEBUG_ERR("error calling ft_conf_image_twin_report: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return status;
	    }
	}
	else {
#endif
	    /* Step 2: Process the sizes set */
	    if (APR_SUCCESS != (status = ft_conf_process_sizes(&conf))) {
		DEBUG_ERR("error calling ft_conf_process_sizes: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return -1;
	    }

	    /* Step 3: Report the twins */
	    if (APR_SUCCESS != (status = ft_conf_twin_report(&conf))) {
		DEBUG_ERR("error calling ft_conf_twin_report: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return status;
	    }
#if HAVE_PUZZLE
	}
#endif
    }
    else {
	DEBUG_ERR("Please submit at least two files...");
	usage(argv[0], opt_option);
	return -1;
    }

    apr_terminate();

    return 0;
}
