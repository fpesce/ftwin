#ifndef FT_FILE_H
#define FT_FILE_H

#include <apr_pools.h>

/* hash is not the same for a same file if it is considered as big or small, so use carefully */
apr_status_t checksum_file(const char *filename, apr_off_t size, apr_off_t excess_size, apr_uint32_t *state,
			   apr_pool_t *gc_pool);

apr_status_t filecmp(apr_pool_t *pool, const char *fname1, const char *fname2, apr_off_t size, apr_off_t excess_size,
		     int *i);

#endif /* FT_FILE_H */
