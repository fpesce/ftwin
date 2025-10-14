#ifndef FT_TRAVERSE_H
#define FT_TRAVERSE_H

#include "ft_types.h"

/**
 * @brief Traverses a given path, adding files to be processed to the configuration.
 *
 * This function recursively walks through directories (if configured to do so),
 * applying ignore rules and filtering files based on the criteria specified
 * in the configuration.
 *
 * @param conf The main configuration structure.
 * @param path The file or directory path to traverse.
 * @return APR_SUCCESS on successful traversal, or an error code.
 */
apr_status_t ft_traverse_path(ft_conf_t *conf, const char *path);

#endif /* FT_TRAVERSE_H */