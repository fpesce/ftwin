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

static apr_status_t checksum_big_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool);
static apr_status_t big_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *i);

#define HUGE_LEN (64 * 1024)

static apr_status_t checksum_small_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool)
{
    char errbuf[128];
    apr_file_t *fd = NULL;
    apr_mmap_t *mm;
    apr_status_t status;

    status = apr_file_open(&fd, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT, gc_pool);
    if (APR_SUCCESS != status) {
	return status;
    }

    status = apr_mmap_create(&mm, fd, 0, (apr_size_t) size, APR_MMAP_READ, gc_pool);
    if (APR_SUCCESS != status) {
	apr_file_close(fd);
	return checksum_big_file(filename, size, hash_out, gc_pool);
    }

    *hash_out = XXH3_128bits(mm->mm, (size_t) size);

    if (APR_SUCCESS != (status = apr_mmap_delete(mm))) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, 128));
	apr_file_close(fd);
	return status;
    }
    if (APR_SUCCESS != (status = apr_file_close(fd))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

static apr_status_t checksum_big_file(const char *filename, apr_off_t size, ft_hash_t *hash_out, apr_pool_t *gc_pool)
{
    unsigned char data_chunk[HUGE_LEN];
    char errbuf[128];
    apr_size_t rbytes;
    apr_file_t *fd = NULL;
    apr_status_t status;
    XXH3_state_t *const state = XXH3_createState();

    if (state == NULL) {
	return APR_ENOMEM;
    }
    XXH3_128bits_reset(state);

    status = apr_file_open(&fd, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT, gc_pool);
    if (APR_SUCCESS != status) {
	XXH3_freeState(state);
	return status;
    }

    do {
	rbytes = HUGE_LEN;
	status = apr_file_read(fd, data_chunk, &rbytes);
	if ((APR_SUCCESS == status || (APR_EOF == status && rbytes > 0))) {
	    if (XXH3_128bits_update(state, data_chunk, rbytes) == XXH_ERROR) {
		DEBUG_ERR("Error during hash update for file: %s", filename);
		XXH3_freeState(state);
		apr_file_close(fd);
		return APR_EGENERAL;
	    }
	}
    } while (APR_SUCCESS == status);

    if (APR_EOF != status) {
	DEBUG_ERR("unable to read(%s, O_RDONLY), skipping: %s", filename, apr_strerror(status, errbuf, 128));
	XXH3_freeState(state);
	apr_file_close(fd);
	return status;
    }

    *hash_out = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    if (APR_SUCCESS != (status = apr_file_close(fd))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

extern apr_status_t checksum_file(const char *filename, apr_off_t size, apr_off_t excess_size, ft_hash_t *hash_out,
				  apr_pool_t *gc_pool)
{
    if (size < excess_size)
	return checksum_small_file(filename, size, hash_out, gc_pool);

    return checksum_big_file(filename, size, hash_out, gc_pool);
}

static apr_status_t small_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *i)
{
    char errbuf[128];
    apr_file_t *fd1 = NULL, *fd2 = NULL;
    apr_mmap_t *mm1, *mm2;
    apr_status_t status;

    if (0 == size) {
	*i = 0;
	return APR_SUCCESS;
    }

    status = apr_file_open(&fd1, fname1, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status) {
	return status;
    }

    status = apr_mmap_create(&mm1, fd1, 0, (apr_size_t) size, APR_MMAP_READ, pool);
    if (APR_SUCCESS != status) {
	apr_file_close(fd1);
	return big_filecmp(pool, fname1, fname2, size, i);
    }

    status = apr_file_open(&fd2, fname2, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status) {
	apr_mmap_delete(mm1);
	apr_file_close(fd1);
	return status;
    }

    status = apr_mmap_create(&mm2, fd2, 0, size, APR_MMAP_READ, pool);
    if (APR_SUCCESS != status) {
	apr_file_close(fd2);
	apr_mmap_delete(mm1);
	apr_file_close(fd1);
	return big_filecmp(pool, fname1, fname2, size, i);
    }

    *i = memcmp(mm1->mm, mm2->mm, size);

    if (APR_SUCCESS != (status = apr_mmap_delete(mm2))) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, 128));
	apr_file_close(fd2);
	apr_mmap_delete(mm1);
	apr_file_close(fd1);
	return status;
    }
    if (APR_SUCCESS != (status = apr_file_close(fd2))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, 128));
	apr_mmap_delete(mm1);
	apr_file_close(fd1);
	return status;
    }

    if (APR_SUCCESS != (status = apr_mmap_delete(mm1))) {
	DEBUG_ERR("error calling apr_mmap_delete: %s", apr_strerror(status, errbuf, 128));
	apr_file_close(fd1);
	return status;
    }
    if (APR_SUCCESS != (status = apr_file_close(fd1))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

static apr_status_t big_filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, int *i)
{
    unsigned char data_chunk1[HUGE_LEN], data_chunk2[HUGE_LEN];
    char errbuf[128];
    apr_size_t rbytes1, rbytes2;
    apr_file_t *fd1 = NULL, *fd2 = NULL;
    apr_status_t status1, status2;

    if (0 == size) {
	*i = 0;
	return APR_SUCCESS;
    }

    status1 = apr_file_open(&fd1, fname1, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status1) {
	return status1;
    }

    status1 = apr_file_open(&fd2, fname2, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool);
    if (APR_SUCCESS != status1) {
	apr_file_close(fd1);
	return status1;
    }

    do {
	rbytes1 = HUGE_LEN;
	status1 = apr_file_read(fd1, data_chunk1, &rbytes1);
	rbytes2 = rbytes1;
	status2 = apr_file_read(fd2, data_chunk2, &rbytes2);
	if ((APR_SUCCESS == status1) && (APR_SUCCESS == status2) && (rbytes2 == rbytes1)) {
	    *i = memcmp(data_chunk1, data_chunk2, rbytes1);
	}
    } while ((APR_SUCCESS == status1) && (APR_SUCCESS == status2) && (0 == *i) && (rbytes2 == rbytes1));

    if ((APR_EOF != status1) && (APR_EOF != status2) && (0 == *i)) {
	DEBUG_ERR("1:unable to read %s (%" APR_SIZE_T_FMT "): %s", fname1, rbytes1, apr_strerror(status1, errbuf, 128));
	DEBUG_ERR("2:unable to read %s (%" APR_SIZE_T_FMT "): %s", fname2, rbytes2, apr_strerror(status2, errbuf, 128));
	return status1;
    }

    if (APR_SUCCESS != (status1 = apr_file_close(fd2))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status1, errbuf, 128));
	apr_file_close(fd1);
	return status1;
    }

    if (APR_SUCCESS != (status1 = apr_file_close(fd1))) {
	DEBUG_ERR("error calling apr_file_close: %s", apr_strerror(status1, errbuf, 128));
	return status1;
    }

    return APR_SUCCESS;
}

extern apr_status_t filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, apr_off_t excess_size,
			    int *i)
{
    if (size < excess_size)
	return small_filecmp(pool, fname1, fname2, size, i);

    return big_filecmp(pool, fname1, fname2, size, i);
}
