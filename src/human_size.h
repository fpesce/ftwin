/**
 * @file human_size.h
 * @brief Utilities for parsing and formatting human-readable file sizes.
 * @ingroup Utilities
 */
#ifndef HUMAN_SIZE_H
#define HUMAN_SIZE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <apr_general.h>

/**
 * @brief Parses a human-readable size string (e.g., "10M", "2.5G") into bytes.
 *
 * Supports suffixes: B (bytes), K (kilobytes), M (megabytes), G (gigabytes),
 * T (terabytes). If no suffix is provided, the value is treated as bytes.
 *
 * @param[in] size_str The string to parse.
 * @return The size in bytes as an apr_off_t, or -1 on parsing error.
 */
apr_off_t parse_human_size(const char *size_str);

/**
 * @brief Formats a size in bytes into a human-readable string.
 *
 * The function selects the most appropriate unit (B, KB, MB, GB, TB) to
 * produce a concise representation.
 *
 * @param[in] size The size in bytes.
 * @param[in] pool The APR pool to allocate the resulting string from.
 * @return A pointer to the formatted, human-readable size string.
 */
const char *format_human_size(apr_off_t size, apr_pool_t *pool);

#endif /* HUMAN_SIZE_H */
