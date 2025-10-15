/**
 * @file ft_file.h
 * @brief Interface for file comparison and checksum calculation.
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

#ifndef FT_FILE_H
#define FT_FILE_H

#include <apr_pools.h>

#define FTWIN_MIN(a,b) ((a)<(b)) ? (a) : (b)

#include "checksum.h"

/**
 * @brief Calculates the XXH128 checksum of a file.
 *
 * This function handles both small files (using memory-mapping for efficiency if possible)
 * and large files (using buffered reading). The determination is based on the `excess_size`
 * parameter.
 *
 * @param[in] filename The path to the file.
 * @param[in] size The size of the file.
 * @param[in] excess_size The file size threshold above which buffered reading is used instead of mmap.
 * @param[out] hash_out Pointer to a ft_hash_t structure to store the resulting checksum.
 * @param[in] gc_pool An APR pool for temporary allocations during the operation.
 * @return APR_SUCCESS on success, or an APR error code on failure.
 */
apr_status_t checksum_file(const char *filename, apr_off_t size, apr_off_t excess_size, ft_hash_t *hash_out,
			   apr_pool_t *gc_pool);

/**
 * @brief Compares two files byte-by-byte to determine if they are identical.
 *
 * This function intelligently chooses between memory-mapping for smaller files and
 * buffered reading for larger files, based on the `excess_size` threshold.
 *
 * @param[in] pool The APR pool to use for allocations.
 * @param[in] fname1 The path to the first file.
 * @param[in] fname2 The path to the second file.
 * @param[in] size The size of the files (assumed to be identical).
 * @param[in] excess_size The file size threshold to switch from mmap to buffered I/O.
 * @param[out] i Pointer to an integer that will be set to 0 if the files are identical,
 *               or a non-zero value otherwise.
 * @return APR_SUCCESS on successful comparison, or an APR error code if a file cannot be read.
 */
apr_status_t filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, apr_off_t excess_size,
		     int *result_out);

#endif /* FT_FILE_H */
