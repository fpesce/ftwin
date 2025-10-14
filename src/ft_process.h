#ifndef FT_PROCESS_H
#define FT_PROCESS_H

#include "ft_types.h"
#include "checksum.h"

/**
 * @brief Processes all the files that have been collected in the configuration.
 *
 * This function orchestrates the file processing, including setting up a
 * thread pool for parallel hashing, distributing file hashing tasks to worker
 * threads, and managing the overall process.
 *
 * @param conf The main configuration structure, containing the list of files to process.
 * @return APR_SUCCESS on success, or an error code if processing fails.
 */
apr_status_t ft_process_files(ft_conf_t *conf);

#endif /* FT_PROCESS_H */
