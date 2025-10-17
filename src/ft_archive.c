/**
 * @file ft_archive.c
 * @brief Archive handling functions for extracting files from archives.
 * @ingroup Archive
 */
/*
 * Copyright (C) 2007-2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include <archive.h>
#include <archive_entry.h>

/**
 * @brief The block size for archive operations.
 *
 * This constant defines the block size (in bytes) used for reading from
 * and writing to archives.
 */
static const size_t ARCHIVE_BLOCK_SIZE = 10240;

static int copy_data(struct archive *archive_read, struct archive *archive_write)
{
    const void *buff = NULL;
    off_t offset = 0;
    size_t size = 0;

    for (;;) {
        int result_value = archive_read_data_block(archive_read, &buff, &size, &offset);
        if (result_value == ARCHIVE_EOF) {
            return ARCHIVE_OK;
        }
        if (result_value != ARCHIVE_OK) {
            DEBUG_ERR("error calling archive_read_data_block(): %s", archive_error_string(archive_read));
            return result_value;
        }
        result_value = (int) archive_write_data_block(archive_write, buff, size, offset);
        if (result_value != ARCHIVE_OK) {
            DEBUG_ERR("error calling archive_write_data_block(): %s", archive_error_string(archive_write));
            return result_value;
        }
    }
}

char *ft_archive_untar_file(ft_file_t *file, apr_pool_t *pool)
{
    struct archive *archive_handle = NULL;
    struct archive *ext = NULL;
    struct archive_entry *entry = NULL;
    char *tmpfile = NULL;
    int result_value = 0;

    archive_handle = archive_read_new();
    if (NULL == archive_handle) {
        DEBUG_ERR("error calling archive_read_new()");
        return NULL;
    }
    result_value = archive_read_support_filter_all(archive_handle);
    if (0 != result_value) {
        DEBUG_ERR("error calling archive_read_support_filter_all(): %s", archive_error_string(archive_handle));
        return NULL;
    }
    result_value = archive_read_support_format_all(archive_handle);
    if (0 != result_value) {
        DEBUG_ERR("error calling archive_read_support_format_all(): %s", archive_error_string(archive_handle));
        return NULL;
    }
    result_value = archive_read_open_filename(archive_handle, file->path, ARCHIVE_BLOCK_SIZE);
    if (0 != result_value) {
        DEBUG_ERR("error calling archive_read_open_filename(%s): %s", file->path, archive_error_string(archive_handle));
        return NULL;
    }

    ext = archive_write_disk_new();
    if (NULL == ext) {
        DEBUG_ERR("error calling archive_write_disk_new()");
        return NULL;
    }

    for (;;) {
        result_value = archive_read_next_header(archive_handle, &entry);
        if (result_value == ARCHIVE_EOF) {
            DEBUG_ERR("subpath [%s] not found in archive [%s]", file->subpath, file->path);
            return NULL;
        }
        if (result_value != ARCHIVE_OK) {
            DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(archive_handle));
            return NULL;
        }

        if (!strcmp(file->subpath, archive_entry_pathname(entry))) {
            mode_t current_mode = umask(S_IRWXG | S_IRWXO);

            /*
             * All I want is only a temporary filename, but gcc outputs me an
             * ugly warning if I use tempnam...
             * tmpfile = tempnam("/tmp/", "ftwin");
             */
            tmpfile = apr_pstrdup(pool, "/tmp/ftwinXXXXXX");
            result_value = mkstemp(tmpfile);
            if (result_value < 0) {
                DEBUG_ERR("error creating tmpfile %s", tmpfile);
                return NULL;
            }
            umask(current_mode);
            close(result_value);

            archive_entry_copy_pathname(entry, tmpfile);

            result_value = archive_write_header(ext, entry);
            if (result_value == ARCHIVE_OK) {
                result_value = copy_data(archive_handle, ext);
                if (result_value != ARCHIVE_OK) {
                    DEBUG_ERR("error while copying data from archive (%s)", file->path);
                    (void) apr_file_remove(tmpfile, pool);
                    return NULL;
                }
            }
            else {
                DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(archive_handle));
                (void) apr_file_remove(tmpfile, pool);
                return NULL;
            }

            break;
        }
    }

    archive_write_free(ext);
    archive_read_free(archive_handle);

    return tmpfile;
}
