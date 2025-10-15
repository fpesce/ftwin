/**
 * @file ft_file.c
 * @brief Functions for file comparison, checksum calculation (XXH128), and I/O handling.
 * @ingroup CoreLogic
 */
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

#include <apr_file_io.h>
#include <apr_mmap.h>

#include "checksum.h"
#include "debug.h"
#include "ft_file.h"
#include "ft_config.h"

static apr_status_t checksum_big_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool);
static apr_status_t big_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *result_out);

/**
 * @brief The chunk size for processing large files.
 *
 * This constant defines the size of the data chunk (in bytes) used for
 * reading and comparing large files. Using a larger chunk size can improve
 * I/O performance but will increase memory usage.
 */
static const size_t HUGE_LEN = 64L * 1024L;

static apr_status_t checksum_small_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool)
{
    char errbuf[CHAR_MAX_VAL];
    apr_file_t *file_descriptor = NULL;
    apr_mmap_t *memory_map = NULL;
    memset(errbuf, 0, sizeof(errbuf));
    apr_status_t status = APR_SUCCESS;

    status = apr_file_open(&file_descriptor, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT, gc_pool);
    if (APR_SUCCESS != status) {
	return status;
    }

    status = apr_mmap_create(&memory_map, file_descriptor, 0, (apr_size_t) size, APR_MMAP_READ, gc_pool);
    if (APR_SUCCESS != status) {
	(void) apr_file_close(file_descriptor);
	return checksum_big_file(filename, size, hash_out, gc_pool);
    }

    *hash_out = XXH3_128bits(memory_map->mm, (size_t) size);

    status = apr_mmap_delete(memory_map);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	(void) apr_file_close(file_descriptor);
	return status;
    }
    status = apr_file_close(file_descriptor);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	return status;
    }

    return APR_SUCCESS;
}

static apr_status_t checksum_big_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool)
{
    unsigned char data_chunk[HUGE_LEN];
    char errbuf[CHAR_MAX_VAL];
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t rbytes;
    apr_file_t *file_descriptor = NULL;
    memset(data_chunk, 0, sizeof(data_chunk));
    memset(errbuf, 0, sizeof(errbuf));
    apr_status_t status = APR_SUCCESS;
    XXH3_state_t *const state = XXH3_createState();

    if (state == NULL) {
	return APR_ENOMEM;
    }
    XXH3_128bits_reset(state);

    status = apr_file_open(&file_descriptor, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT, gc_pool);
    if (APR_SUCCESS != status) {
	XXH3_freeState(state);
	return status;
    }

    do {
	rbytes = HUGE_LEN;
	status = apr_file_read(file_descriptor, data_chunk, &rbytes);
	if ((APR_SUCCESS == status || (APR_EOF == status && rbytes > 0))) {
	    if (XXH3_128bits_update(state, data_chunk, rbytes) == XXH_ERROR) {
		DEBUG_ERR("Error during hash update for file: %s", filename);
		XXH3_freeState(state);
		(void) apr_file_close(file_descriptor);
		return APR_EGENERAL;
	    }
	}
    } while (APR_SUCCESS == status);

    if (APR_EOF != status) {
	DEBUG_ERR("unable to read(%s, O_RDONLY), skipping: %s", filename, apr_strerror(status, errbuf, sizeof(errbuf)));
	XXH3_freeState(state);
	(void) apr_file_close(file_descriptor);
	return status;
    }

    *hash_out = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    status = apr_file_close(file_descriptor);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	return status;
    }

    return APR_SUCCESS;
}

extern apr_status_t checksum_file(const char *filename, apr_off_t size, apr_off_t excess_size, ft_hash_t *hash_out,
				  apr_pool_t *gc_pool)
{
    if (size < excess_size) {
	return checksum_small_file(filename, size, hash_out, gc_pool);
    }

    return checksum_big_file(filename, size, hash_out, gc_pool);
}

static apr_status_t small_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *result_out)
{
    char errbuf[CHAR_MAX_VAL];
    apr_file_t *fd1 = NULL;
    apr_file_t *fd2 = NULL;
    apr_mmap_t *mm1 = NULL;
    apr_mmap_t *mm2 = NULL;
    memset(errbuf, 0, sizeof(errbuf));
    apr_status_t status = APR_SUCCESS;

    if (0 == size) {
	*result_out = 0;
	return APR_SUCCESS;
    }

    status = apr_file_open(&fd1, fname1, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status) {
	return status;
    }

    status = apr_mmap_create(&mm1, fd1, 0, (apr_size_t) size, APR_MMAP_READ, pool);
    if (APR_SUCCESS != status) {
	(void) apr_file_close(fd1);
	return big_filecmp(pool, fname1, fname2, size, result_out);
    }

    status = apr_file_open(&fd2, fname2, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status) {
	apr_mmap_delete(mm1);
	(void) apr_file_close(fd1);
	return status;
    }

    status = apr_mmap_create(&mm2, fd2, 0, size, APR_MMAP_READ, pool);
    if (APR_SUCCESS != status) {
	(void) apr_file_close(fd2);
	(void) apr_file_close(fd2);
	(void) apr_file_close(fd1);
	return big_filecmp(pool, fname1, fname2, size, result_out);
    }

    *result_out = memcmp(mm1->mm, mm2->mm, size);

    status = apr_mmap_delete(mm2);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	(void) apr_file_close(fd2);
	(void) apr_mmap_delete(mm1);
	(void) apr_file_close(fd1);
	return status;
    }
    status = apr_file_close(fd2);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	(void) apr_mmap_delete(mm1);
	(void) apr_file_close(fd1);
	return status;
    }

    status = apr_mmap_delete(mm1);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	(void) apr_file_close(fd1);
	return status;
    }
    status = apr_file_close(fd1);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, sizeof(errbuf)));
	return status;
    }

    return APR_SUCCESS;
}

static apr_status_t big_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *result_out)
{
    unsigned char data_chunk1[HUGE_LEN];
    unsigned char data_chunk2[HUGE_LEN];
    char errbuf[CHAR_MAX_VAL];
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t rbytes1;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t rbytes2;
    apr_file_t *fd1 = NULL;
    apr_file_t *fd2 = NULL;
    memset(data_chunk1, 0, sizeof(data_chunk1));
    memset(data_chunk2, 0, sizeof(data_chunk2));
    memset(errbuf, 0, sizeof(errbuf));
    apr_status_t status1 = APR_SUCCESS;
    apr_status_t status2 = APR_SUCCESS;

    if (0 == size) {
	*result_out = 0;
	return APR_SUCCESS;
    }

    status1 = apr_file_open(&fd1, fname1, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status1) {
	return status1;
    }

    status1 = apr_file_open(&fd2, fname2, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status1) {
	(void) apr_file_close(fd1);
	return status1;
    }

    do {
	rbytes1 = HUGE_LEN;
	status1 = apr_file_read(fd1, data_chunk1, &rbytes1);
	rbytes2 = rbytes1;
	status2 = apr_file_read(fd2, data_chunk2, &rbytes2);
	if ((APR_SUCCESS == status1) && (APR_SUCCESS == status2) && (rbytes2 == rbytes1)) {
	    *result_out = memcmp(data_chunk1, data_chunk2, rbytes1);
	}
    } while ((APR_SUCCESS == status1) && (APR_SUCCESS == status2) && (0 == *result_out) && (rbytes2 == rbytes1));

    if ((APR_EOF != status1) && (APR_EOF != status2) && (0 == *result_out)) {
	DEBUG_ERR("1:unable to read %s (%" APR_SIZE_T_FMT "): %s", fname1, rbytes1,
		  apr_strerror(status1, errbuf, sizeof(errbuf)));
	DEBUG_ERR("2:unable to read %s (%" APR_SIZE_T_FMT "): %s", fname2, rbytes2,
		  apr_strerror(status2, errbuf, sizeof(errbuf)));
	return status1;
    }

    status1 = apr_file_close(fd2);
    if (APR_SUCCESS != status1) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status1, errbuf, sizeof(errbuf)));
	(void) apr_file_close(fd1);
	return status1;
    }

    status1 = apr_file_close(fd1);
    if (APR_SUCCESS != status1) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status1, errbuf, sizeof(errbuf)));
	return status1;
    }

    return APR_SUCCESS;
}

extern apr_status_t filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, apr_off_t excess_size,
			    int *result_out)
{
    if (size < excess_size) {
	return small_filecmp(pool, fname1, fname2, size, result_out);
    }

    return big_filecmp(pool, fname1, fname2, size, result_out);
}
