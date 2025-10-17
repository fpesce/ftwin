#ifndef FT_CONSTANTS_H
#define FT_CONSTANTS_H

/**
 * @file ft_constants.h
 * @brief This file contains shared constants used across the ftwin project.
 * @ingroup Core
 */

/**
 * @brief Defines the standard buffer size for error messages.
 *
 * This enum is used to ensure that buffers for apr_strerror and other
 * error reporting functions are consistently sized.
 */
enum ft_error_buffer_size
{
    ERR_BUF_SIZE = 128
};

#endif /* FT_CONSTANTS_H */