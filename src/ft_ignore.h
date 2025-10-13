/**
 * @file ft_ignore.h
 * @brief Interface for handling hierarchical ignore patterns, similar to .gitignore.
 * @ingroup Utilities
 */
/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
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

#ifndef FT_IGNORE_H
#define FT_IGNORE_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <pcre.h>

/* Flags for ignore patterns */
#define FT_IGNORE_NEGATE    0x01	/* Starts with '!' */
#define FT_IGNORE_DIR_ONLY  0x02	/* Ends with '/' */

/**
 * @brief Represents a single compiled ignore pattern.
 */
typedef struct
{
    pcre *regex;                /**< The compiled PCRE pattern. */
    const char *pattern_str;    /**< The original, uncompiled pattern string for debugging. */
    unsigned int flags;         /**< Flags for the pattern (e.g., FT_IGNORE_NEGATE). */
} ft_ignore_pattern_t;

/**
 * @brief Represents the ignore rules for a specific directory and its descendants.
 *
 * An ignore context is tied to a base directory and contains patterns loaded
 * from a .gitignore file within that directory. Contexts are linked to their
 * parent's context, forming a hierarchy that mirrors the filesystem.
 */
typedef struct ft_ignore_context_t
{
    struct ft_ignore_context_t *parent; /**< Pointer to the parent directory's context, or NULL if root. */
    apr_array_header_t *patterns;       /**< Array of ft_ignore_pattern_t pointers defined at this level. */
    const char *base_dir;               /**< The absolute path to the directory this context is anchored to. */
    apr_size_t base_dir_len;            /**< The length of the base directory path. */
    apr_pool_t *pool;                   /**< The memory pool used for allocations within this context. */
} ft_ignore_context_t;

/**
 * @brief Result codes for an ignore match operation.
 */
typedef enum
{
    FT_IGNORE_MATCH_NONE,       /**< The path is not matched by any pattern. */
    FT_IGNORE_MATCH_IGNORED,    /**< The path is matched by an ignore pattern. */
    FT_IGNORE_MATCH_WHITELISTED /**< The path is matched by a negation (whitelist) pattern. */
} ft_ignore_match_result_t;

/**
 * @brief Creates a new ignore context.
 *
 * @param[in] pool The APR pool to allocate the new context from.
 * @param[in] parent The parent context, or NULL to create a root context.
 * @param[in] base_dir The absolute path to the directory this context represents.
 * @return A pointer to the newly created context.
 */
ft_ignore_context_t *ft_ignore_context_create(apr_pool_t *pool, ft_ignore_context_t * parent, const char *base_dir);

/**
 * @brief Loads and parses an ignore file (like .gitignore) into a context.
 *
 * @param[in] ctx The context to load the patterns into.
 * @param[in] filepath The path to the ignore file.
 * @return APR_SUCCESS on success, or an error code on failure.
 */
apr_status_t ft_ignore_load_file(ft_ignore_context_t * ctx, const char *filepath);

/**
 * @brief Adds a single pattern string to a context.
 *
 * The pattern is parsed, compiled into a regex, and added to the context's list.
 *
 * @param[in] ctx The context to add the pattern to.
 * @param[in] pattern_str The raw pattern string to add.
 * @return APR_SUCCESS on success, or an error code on failure.
 */
apr_status_t ft_ignore_add_pattern_str(ft_ignore_context_t * ctx, const char *pattern_str);

/**
 * @brief Checks if a given path should be ignored based on the hierarchical context.
 *
 * It traverses the context hierarchy from the current context up to the root,
 * applying rules from each level. The last matching pattern determines the outcome.
 *
 * @param[in] ctx The starting context for matching (usually the one for the file's direct parent).
 * @param[in] fullpath The absolute path of the file or directory to check.
 * @param[in] is_dir A flag indicating if the path is a directory.
 * @return The match result (ignored, whitelisted, or no match).
 */
ft_ignore_match_result_t ft_ignore_match(ft_ignore_context_t * ctx, const char *fullpath, int is_dir);

#endif /* FT_IGNORE_H */
