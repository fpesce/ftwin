/**
 * @file ft_archive.c
 * @brief Archive handling functions for extracting files from archives.
 * @ingroup Archive
 */
/*
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

#include "ft_archive.h"

#include <sys/stat.h>
#include <unistd.h>

#include <apr_file_io.h>
#include <apr_strings.h>

#include "config.h"
#include "debug.h"

#if HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>

static int copy_data(struct archive *ar, struct archive *aw)
{
    const void *buff;
    off_t offset;
    size_t size;

    for (;;) {
	int rv = archive_read_data_block(ar, &buff, &size, &offset);
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

char *ft_archive_untar_file(ft_file_t *file, apr_pool_t *p)
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
    rv = archive_read_support_filter_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_filter_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_support_format_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_format_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_open_filename(a, file->path, 10240);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_open_filename(%s): %s", file->path, archive_error_string(a));
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

    archive_write_free(ext);
    archive_read_free(a);

    return tmpfile;
}

#else /* !HAVE_ARCHIVE */

char *ft_archive_untar_file(ft_file_t *file, apr_pool_t *p)
{
    (void) file;
    (void) p;
    return NULL;
}

#endif /* HAVE_ARCHIVE */
