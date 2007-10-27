/*
 *
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * 	http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pcre.h>

#include <stdio.h>		/* fgetgrent */
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

#if HAVE_LIBZ
# include <zlib.h>
#endif

#if HAVE_TAR
#include <fcntl.h>		/* O_RDONLY */
#include <libtar.h>
#include <unistd.h>		/* close() */
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

#if HAVE_TAR
#define OPTION_UNTAR 0x0100
#endif

typedef struct ft_file_t
{
    apr_off_t size;
    char *path;
#if HAVE_TAR
    char *subpath;
#endif
#if HAVE_PUZZLE
    PuzzleCvec cvec;
    int cvec_ok:1;
#endif
    int prioritized:1;
#if HAVE_LIBZ
    int gziped:1;
#endif
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
    apr_pool_t *pool;		/* Always needed somewhere ;) */
    napr_heap_t *heap;		/* Will holds the files */
    napr_hash_t *sizes;		/* will holds the sizes hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    napr_hash_t *gids;		/* will holds the gids hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    napr_hash_t *ig_files;
    pcre *ig_regex;
    pcre *wl_regex;
    char *p_path;		/* priority path */
    char *username;
    apr_size_t p_path_len;
    apr_uid_t userid;
    apr_gid_t groupid;
    unsigned short int mask;
    char sep;
} ft_conf_t;

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
	end = strchr(filename, 'c');
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

#ifdef HAVE_LIBZ
/* 
 * XXX
 * The libtar API don't allow to use pointer as return of openfunc frontend,
 * and on 64 bits architecture the cast proposed in the example libtar.c
 * truncate the pointer (64 bits) returned by gzdopen function by casting it to
 * an int (32 bits).
 *
 * Thus, the solution I choose is to use a global variable (I hate it...) to
 * store the pointer returned by gzdopen.
 *
 * This ugly workaround works because I'm not a multi-threaded app.
 *
 * FIXME
 * because of inconsistent allocation in function th_get_pathname, strdup() ing
 * 2 case / 3, not allowing a correct free in the return function. There's a
 * memory leak introduced by this behavior...
 */
static gzFile global_gzFile;

int gzopen_frontend(const char *pathname, int oflags, int mode)
{
    char *gzoflags;
    int fd;

    switch (oflags & O_ACCMODE) {
    case O_WRONLY:
	gzoflags = "wb";
	break;
    case O_RDONLY:
	gzoflags = "rb";
	break;
    default:
    case O_RDWR:
	errno = EINVAL;
	return -1;
    }

    fd = open(pathname, oflags, mode);
    if (fd == -1)
	return -1;

    if ((oflags & O_CREAT) && fchmod(fd, mode))
	return -1;

    global_gzFile = gzdopen(fd, gzoflags);
    if (!global_gzFile) {
	errno = ENOMEM;
	return -1;
    }

    return fd;
}

int gzclose_frontend(int fd)
{
    int rv;

    rv = gzclose(global_gzFile);
    global_gzFile = NULL;

    return rv;
}

ssize_t gzread_frontend(int fd, void *buf, size_t len)
{
    return (ssize_t) gzread(global_gzFile, buf, (unsigned) len);
}

ssize_t gzwrite_frontend(int fd, const void *buf, size_t len)
{
    return (ssize_t) gzwrite(global_gzFile, buf, (unsigned) len);
}

tartype_t gztype = { (openfunc_t) gzopen_frontend, gzclose_frontend, gzread_frontend, gzwrite_frontend };

#endif /* HAVE_LIBZ */


/** 
 * The function used to add recursively or not file and dirs.
 * @param conf Configuration structure.
 * @param filename name of a file or directory to add to the list of twinchecker.
 * @param gc_pool garbage collecting pool, will be cleaned by the caller.
 * @return APR_SUCCESS if no error occured.
 */
#define MATCH_VECTOR_SIZE 210
static apr_status_t ft_conf_add_file(ft_conf_t *conf, const char *filename, apr_pool_t *gc_pool)
{
    int ovector[MATCH_VECTOR_SIZE];
    char errbuf[128];
    apr_finfo_t finfo;
    apr_dir_t *dir;
    apr_int32_t statmask =
	APR_FINFO_SIZE | APR_FINFO_TYPE | APR_FINFO_USER | APR_FINFO_GROUP | APR_FINFO_UPROT | APR_FINFO_GPROT;
    apr_size_t fname_len;
    apr_uint32_t hash_value;
    apr_status_t status;
    int rc;

    /* Step 1 : Check if it's a directory and get the size if not */
    if (!is_option_set(conf->mask, OPTION_FSYML))
	statmask |= APR_FINFO_LINK;

    if (APR_SUCCESS != (status = apr_stat(&finfo, filename, statmask, gc_pool))) {
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

	    status = ft_conf_add_file(conf, fullname, gc_pool);

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
#if HAVE_TAR
	char *subpath;
	TAR *tar = NULL;
	int rv;
#if HAVE_LIBZ
	int gziped;
#endif
#endif

	fname = apr_pstrdup(conf->pool, filename);
	finfosize = finfo.size;
#if HAVE_TAR
	subpath = NULL;
	fname_len = strlen(filename);
	if (is_option_set(conf->mask, OPTION_UNTAR)) {
	    if ((fname_len > 4) && !strncasecmp(fname + fname_len - 4, ".tar", 4)) {
		if (tar_open(&tar, fname, NULL, O_RDONLY, 0, TAR_VERBOSE))
		    tar = NULL;
		gziped = 0;
	    }
#if HAVE_LIBZ
	    if ((NULL == tar) && (fname_len > 7) && !strncasecmp(fname + fname_len - 7, ".tar.gz", 7)) {
		if (tar_open(&tar, fname, &gztype, O_RDONLY, 0, 0))
		    tar = NULL;
		gziped = 1;
	    }
#endif
	}
	do {
#endif
	    if (finfosize >= conf->minsize
#if HAVE_TAR
		&& ((NULL != tar) && !TH_ISDIR(tar))
#endif
		    ) {
		ft_file_t *file;
		ft_fsize_t *fsize;

		file = apr_palloc(conf->pool, sizeof(struct ft_file_t));
		file->path = fname;
		file->size = finfosize;
#if HAVE_TAR
		if (subpath) {
		    file->subpath = apr_pstrdup(conf->pool, subpath);
#if HAVE_LIBZ
		    if (gziped)
			file->gziped |= 0x1;
		    else
			file->gziped &= 0x0;
#endif
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
#if HAVE_TAR
	    if (tar) {
		if (subpath) {
		    /* following headers */
		    if (TH_ISREG(tar) && (0 != tar_skip_regfile(tar))) {
			DEBUG_ERR("tar_skip_regfile(): %s", strerror(errno));
			return APR_EGENERAL;
		    }
		    subpath = NULL;
		}
		rv = th_read(tar);
		if (0 == rv) {
		    finfosize = th_get_size(tar);
		    subpath = th_get_pathname(tar);
		}
	    }
	} while (tar && (0 == rv));
	if (tar)
	    tar_close(tar);
#endif
    }

    return APR_SUCCESS;
}

static char *ft_untar_file(ft_file_t *file, apr_pool_t *p)
{
    TAR *tar;
    char *tmpfile = NULL, *tmpstr;
    int fd, rv;

#if HAVE_LIBZ
    if (file->gziped & 0x1) {
	rv = tar_open(&tar, file->path, &gztype, O_RDONLY, 0, 0);
    }
    else {
#endif
	rv = tar_open(&tar, file->path, NULL, O_RDONLY, 0, 0);
#if HAVE_LIBZ
    }
#endif

    if (!rv) {
	while (!th_read(tar)) {
	    tmpstr = th_get_pathname(tar);
	    if (!strcmp(tmpstr, file->subpath)) {
		tmpfile = apr_pstrdup(p, "/tmp/ftwin_XXXXXX");
		fd = mkstemp(tmpfile);
		tar_extract_file(tar, tmpfile);
		close(fd);
		break;
	    }
	    if (TH_ISREG(tar) && (0 != tar_skip_regfile(tar))) {
		DEBUG_ERR("tar_skip_regfile(): %s", strerror(errno));
		return NULL;
	    }
	}
	tar_close(tar);
    }
    else {
	DEBUG_ERR("tar_open(): %s", strerror(errno));
    }

    return tmpfile;
}

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
#if HAVE_TAR
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
#if HAVE_TAR
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
	    if (d < PUZZLE_CVEC_SIMILARITY_LOWER_THRESHOLD) {
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

    if (is_option_set(conf->mask, OPTION_VERBO))
	fprintf(stderr, "Reporting duplicate files:\n");

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (file->size == old_size)
	    continue;

	old_size = file->size;
	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, 1, &hash_value))) {
	    qsort(fsize->chksum_array, fsize->nb_files, sizeof(ft_chksum_t), chksum_cmp);
	    for (i = 0; i < fsize->nb_files; i++) {
		if (NULL == fsize->chksum_array[i].file)
		    continue;
		already_printed = 0;
		for (j = i + 1; j < fsize->nb_files; j++) {
		    if (0 == memcmp(fsize->chksum_array[i].val_array, fsize->chksum_array[j].val_array, HASHSTATE)) {
			char *fpathi, *fpathj;
#if HAVE_TAR
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
#if HAVE_TAR
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
#if HAVE_TAR
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
#if HAVE_TAR
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
    FILE *etc_grp;
    struct group *grp_p;
    ft_gid_t *gid;
    apr_uint32_t hash_value;
    int i;

    errno = 0;
    if (NULL == (etc_grp = fopen("/etc/group", "r"))) {
	return errno ? errno : APR_ENOENT;
    }

    while (NULL != (grp_p = fgetgrent(etc_grp))) {
	for (i = 0; NULL != grp_p->gr_mem[i]; i++) {
	    if (!strcmp(username, grp_p->gr_mem[i])) {
		if (NULL == (gid = napr_hash_search(gids, &(grp_p->gr_gid), 1, &hash_value))) {
		    gid = apr_palloc(p, sizeof(struct ft_gid_t));
		    gid->val = grp_p->gr_gid;
		    napr_hash_set(gids, gid, hash_value);
		}
	    }
	}
    }

    fclose(etc_grp);

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
#endif
	{"ignore-list", 'i', TRUE, "\tcomma-separated list of file names to ignore."},
	{"minimal-length", 'm', TRUE, "minimum size of file to process."},
	{"optimize-memory", 'o', FALSE, "reduce memory usage, but increase process time."},
	{"priority-path", 'p', TRUE, "\tfile in this path are displayed first when\n\t\t\t\tduplicates are reported."},
	{"recurse-subdir", 'r', FALSE, "recurse subdirectories."},
	{"separator", 's', TRUE, "\tseparator character between twins, default: \\n."},
#if HAVE_TAR
	{"tar-cmp", 't', FALSE, "\twill process files archived in .tar default: off."},
#endif
	{"verbose", 'v', FALSE, "\tdisplay a progress bar."},
	{"version", 'V', FALSE, "\tdisplay version."},
	{"whitelist-regex-file", 'w', TRUE, "filenames that doesn't match this are ignored."},
	{"excessive-size", 'x', TRUE, "excessive size of file that switch off mmap use."},
	{NULL, 0, 0, NULL},	/* end (a.k.a. sentinel) */
    };
    char errbuf[128];
    char *regex = NULL;
    char *wregex = NULL;
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
    conf.p_path = NULL;
    conf.p_path_len = 0;
    conf.minsize = 0;
    conf.sep = '\n';
    conf.excess_size = 50 * 1024 * 1024;
    conf.mask = 0x00;

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
	    wregex = apr_pstrdup(pool, ".*\\.(gif|png|jpg)$");
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
#if HAVE_TAR
	case 't':
	    set_option(&conf.mask, OPTION_UNTAR, 1);
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

    /* Step 1 : Browse the file */
    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, pool))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }
    for (i = os->ind; i < argc; i++) {
	if (APR_SUCCESS != (status = ft_conf_add_file(&conf, argv[i], gc_pool))) {
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
