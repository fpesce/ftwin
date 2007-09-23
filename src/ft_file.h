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

/* hash result is not the same for a same file whether it is considered as big or small, so use carefully */
apr_status_t checksum_file(const char *filename, apr_off_t size, apr_off_t excess_size, apr_uint32_t *state,
			   apr_pool_t *gc_pool);

apr_status_t filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, apr_off_t excess_size,
		     int *i);

#endif /* FT_FILE_H */
