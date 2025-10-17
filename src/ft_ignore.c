/**
 * @file ft_ignore.c
 * @brief Implementation of the hierarchical .gitignore-style pattern matching logic.
 * @ingroup Utilities
 */
/*
 * Copyright (C) 2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include "ft_ignore.h"
#include "debug.h"
#include <apr_file_io.h>
#include <apr_strings.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_PATTERNS_CAPACITY 16

/**
 * @brief The maximum length of a pattern string.
 *
 * This constant defines the maximum buffer size for reading and processing
 * a single pattern from a .gitignore file.
 */
static const size_t MAX_PATTERN_LEN = 4096;

/**
 * Convert Git glob pattern to PCRE regex
 * Handles: *, **, ?, [abc], !, /, escapes
 */
/**
 * @brief Handles negation and leading/trailing slashes in a Git glob pattern.
 *
 * This function processes the beginning of a glob pattern to identify
 * important structural markers:
 * - `!`: Negates the pattern.
 * - `/`: Anchors the pattern to the beginning of the path.
 * - Trailing `/`: Marks the pattern as matching directories only.
 *
 * It updates the provided flags and returns a pointer to the start of the
 * main pattern content.
 *
 * @param pattern The raw glob pattern string.
 * @param flags A pointer to an unsigned int where flags (FT_IGNORE_NEGATE, FT_IGNORE_DIR_ONLY) are stored.
 * @param starts_with_slash A pointer to an int that is set to 1 if the pattern is anchored to the root.
 * @return A pointer to the beginning of the pattern after processing prefixes.
 */
static const char *handle_negation_and_slashes(const char *pattern, unsigned int *flags, int *starts_with_slash)
{
    const char *pattern_cursor = pattern;

    /* Reset flags and indicators */
    *flags = 0;
    *starts_with_slash = 0;

    /* Handle negation: Skip '!' and any leading whitespace */
    if (*pattern_cursor == '!') {
        *flags |= FT_IGNORE_NEGATE;
        pattern_cursor++;
        while (isspace((unsigned char) *pattern_cursor)) {
            pattern_cursor++;
        }
    }

    /* Handle root anchor: Check for a leading '/' */
    if (*pattern_cursor == '/') {
        *starts_with_slash = 1;
        pattern_cursor++;
    }

    /* Handle directory-only: Check for a trailing '/' */
    const char *end = pattern_cursor + strlen(pattern_cursor) - 1;
    while (end > pattern_cursor && isspace((unsigned char) *end)) {
        end--;
    }
    if (*end == '/') {
        *flags |= FT_IGNORE_DIR_ONLY;
    }

    return pattern_cursor;
}

/**
 * @brief Appends a PCRE sequence to the result buffer safely.
 *
 * This helper function copies a given string `sequence` into the `result`
 * buffer at the position indicated by the `cursor` pointer. It then advances
 * the cursor to the end of the newly appended string.
 *
 * @param cursor A pointer to the current position in the result buffer.
 * @param sequence The string to append.
 */
static void append_pcre_sequence(char **cursor, const char *sequence)
{
    (void) strcpy(*cursor, sequence);
    *cursor += strlen(sequence);
}

/**
 * @brief Sets the start and end anchors for the PCRE regex.
 *
 * Based on whether the pattern was anchored with a leading slash or is a
 * directory-only pattern, this function adds the correct PCRE anchors (`^`, `$`)
 * to the beginning and end of the regex string.
 *
 * @param r A pointer to the current position in the result buffer.
 * @param starts_with_slash Indicates if the pattern began with '/'.
 * @param flags Flags that may include FT_IGNORE_DIR_ONLY.
 */
/**
 * @brief Parameters for setting PCRE anchors.
 */
struct pcre_anchor_params_t
{
    int starts_with_slash;
    unsigned int flags;
};

static void set_pcre_anchors(char **result_cursor, const struct pcre_anchor_params_t *params)
{
    char *cursor = *result_cursor;

    if (params->starts_with_slash) {
        append_pcre_sequence(&cursor, "^");
    }
    else {
        /* Pattern can match at any level unless it contains a slash */
        append_pcre_sequence(&cursor, "(^|/)");
    }

    *result_cursor = cursor;
}

/**
 * @brief Handles the conversion of a glob star (`*`) or double-star (`**`)
 * into its PCRE equivalent.
 *
 * @param p A pointer to the current position in the glob pattern.
 * @param r A pointer to the current position in the result buffer.
 */
static void handle_star(const char **pattern_cursor, char **result_cursor)
{
    if (*(*pattern_cursor + 1) == '*') {
        /* Double star: ** */
        if (*(*pattern_cursor + 2) == '/') {
            /* * *//* pattern: matches zero or more directory levels */
            append_pcre_sequence(result_cursor, "(.*/)?");
            *pattern_cursor += 3;
        }
        else {
            /* ** at the end: matches everything */
            append_pcre_sequence(result_cursor, ".*");
            *pattern_cursor += 2;
        }
    }
    else {
        /* Single star *: matches anything except a slash */
        append_pcre_sequence(result_cursor, "[^/]*");
        *pattern_cursor += 1;
    }
}

/**
 * @brief Converts a glob question mark (`?`) into its PCRE equivalent.
 *
 * @param p A pointer to the current position in the glob pattern.
 * @param r A pointer to the current position in the result buffer.
 */
static void handle_question_mark(const char **pattern_cursor, char **result_cursor)
{
    append_pcre_sequence(result_cursor, "[^/]");
    *pattern_cursor += 1;
}

/**
 * @brief Converts a glob character class (`[abc]`) into its PCRE equivalent.
 *
 * @param p A pointer to the current position in the glob pattern.
 * @param r A pointer to the current position in the result buffer.
 */
static void handle_char_class(const char **pattern_cursor, char **result_cursor)
{
    char *cursor = *result_cursor;
    *cursor++ = '[';
    (*pattern_cursor)++;        // Move past opening `[`

    if (**pattern_cursor == '!' || **pattern_cursor == '^') {
        *cursor++ = '^';
        (*pattern_cursor)++;
    }

    while (**pattern_cursor && **pattern_cursor != ']') {
        if (**pattern_cursor == '\\' && *(*pattern_cursor + 1)) {
            *cursor++ = '\\';
            (*pattern_cursor)++;
        }
        *cursor++ = *(*pattern_cursor)++;
    }

    if (**pattern_cursor == ']') {
        *cursor++ = ']';
        (*pattern_cursor)++;
    }

    *result_cursor = cursor;
}

/**
 * @brief Main loop to convert the body of a glob pattern to a PCRE regex.
 *
 * This function iterates through the glob pattern character by character and
 * dispatches to specialized handlers for glob metacharacters like `*`, `?`,
 * and `[`.
 *
 * @param p The glob pattern string (with prefixes like `!` already handled).
 * @param r A pointer to the current position in the result buffer.
 * @param flags Flags that may include FT_IGNORE_DIR_ONLY.
 */
static void convert_glob_body_to_pcre(const char **pattern_cursor, char **result_cursor, unsigned int flags)
{
    while (**pattern_cursor) {
        /* Stop if we hit a trailing slash for a directory-only pattern */
        if (**pattern_cursor == '/' && (flags & FT_IGNORE_DIR_ONLY) && *(*pattern_cursor + 1) == '\0') {
            break;
        }

        if (**pattern_cursor == '\\' && *(*pattern_cursor + 1)) {
            /* Handle escaped characters */
            char *cursor = *result_cursor;
            *cursor++ = '\\';
            *cursor++ = *(*pattern_cursor + 1);
            *result_cursor = cursor;
            *pattern_cursor += 2;
        }
        else if (**pattern_cursor == '*') {
            handle_star(pattern_cursor, result_cursor);
        }
        else if (**pattern_cursor == '?') {
            handle_question_mark(pattern_cursor, result_cursor);
        }
        else if (**pattern_cursor == '[') {
            handle_char_class(pattern_cursor, result_cursor);
        }
        else if (strchr(".^$+{}()|", **pattern_cursor)) {
            /* Escape PCRE metacharacters */
            char *cursor = *result_cursor;
            *cursor++ = '\\';
            *cursor++ = **pattern_cursor;
            *result_cursor = cursor;
            (*pattern_cursor)++;
        }
        else {
            /* Copy literal characters */
            *(*result_cursor)++ = *(*pattern_cursor)++;
        }
    }
}

static char *ft_glob_to_pcre(const char *pattern, apr_pool_t *pool, unsigned int *flags)
{
    char *result = apr_pcalloc(pool, MAX_PATTERN_LEN);
    char *result_cursor = result;
    struct pcre_anchor_params_t anchor_params = { 0 };
    const char *pattern_cursor = handle_negation_and_slashes(pattern, flags, &anchor_params.starts_with_slash);

    anchor_params.flags = *flags;

    /* Set start anchor based on whether the pattern had a leading slash */
    set_pcre_anchors(&result_cursor, &anchor_params);

    /* Convert the main body of the glob pattern */
    convert_glob_body_to_pcre(&pattern_cursor, &result_cursor, *flags);

    /* Set end anchor */
    if (*flags & FT_IGNORE_DIR_ONLY) {
        /* Match a directory with an optional trailing slash */
        append_pcre_sequence(&result_cursor, "(/|$)");
    }
    else {
        /* Match the end of the string */
        append_pcre_sequence(&result_cursor, "$");
    }

    *result_cursor = '\0';
    return result;
}

ft_ignore_context_t *ft_ignore_context_create(apr_pool_t *pool, ft_ignore_context_t *parent, const char *base_dir)
{
    ft_ignore_context_t *ctx = apr_pcalloc(pool, sizeof(ft_ignore_context_t));

    ctx->pool = pool;
    ctx->parent = parent;
    ctx->base_dir = apr_pstrdup(pool, base_dir);
    ctx->base_dir_len = strlen(base_dir);
    ctx->patterns = apr_array_make(pool, INITIAL_PATTERNS_CAPACITY, sizeof(ft_ignore_pattern_t *));

    return ctx;
}

apr_status_t ft_ignore_add_pattern_str(ft_ignore_context_t *ctx, const char *pattern_str)
{
    const char *trimmed = NULL;
    unsigned int flags = 0;
    char *regex_str = NULL;
    pcre *regex = NULL;
    const char *error = NULL;
    int erroffset = 0;
    ft_ignore_pattern_t *pattern = NULL;

    /* Trim whitespace */
    trimmed = pattern_str;
    while (isspace(*trimmed)) {
        trimmed++;
    }

    /* Skip empty lines and comments */
    if (*trimmed == '\0' || *trimmed == '#') {
        return APR_SUCCESS;
    }

    /* Convert glob to regex */
    regex_str = ft_glob_to_pcre(trimmed, ctx->pool, &flags);

    /* Compile regex */
    regex = pcre_compile(regex_str, 0, &error, &erroffset, NULL);
    if (!regex) {
        DEBUG_ERR("Failed to compile pattern '%s': %s", trimmed, error);
        return APR_EGENERAL;
    }

    /* Create pattern struct */
    pattern = apr_pcalloc(ctx->pool, sizeof(ft_ignore_pattern_t));
    pattern->regex = regex;
    pattern->pattern_str = apr_pstrdup(ctx->pool, trimmed);
    pattern->flags = flags;

    /* Add to context */
    APR_ARRAY_PUSH(ctx->patterns, ft_ignore_pattern_t *) = pattern;

    return APR_SUCCESS;
}

apr_status_t ft_ignore_load_file(ft_ignore_context_t *ctx, const char *filepath)
{
    apr_file_t *file = NULL;
    apr_status_t status = APR_SUCCESS;
    char line[MAX_PATTERN_LEN];

    status = apr_file_open(&file, filepath, APR_READ, APR_OS_DEFAULT, ctx->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    while (apr_file_gets(line, sizeof(line), file) == APR_SUCCESS) {
        /* Remove newline */
        apr_size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        ft_ignore_add_pattern_str(ctx, line);
    }

    (void) apr_file_close(file);
    return APR_SUCCESS;
}

ft_ignore_match_result_t ft_ignore_match(ft_ignore_context_t *ctx, const char *fullpath, int is_dir)
{
    ft_ignore_context_t *current_ctx = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;
    const char *relative_path = NULL;

    if (!ctx || !fullpath) {
        return FT_IGNORE_MATCH_NONE;
    }

    /* Walk up the context hierarchy */
    for (current_ctx = ctx; current_ctx != NULL; current_ctx = current_ctx->parent) {
        int index = 0;

        /* Calculate relative path from this context's base_dir */
        if (strncmp(fullpath, current_ctx->base_dir, current_ctx->base_dir_len) == 0) {
            relative_path = fullpath + current_ctx->base_dir_len;
            /* Skip leading slash */
            while (*relative_path == '/') {
                relative_path++;
            }
        }
        else {
            /* Path not under this context's base, try parent */
            continue;
        }

        /* Check patterns in order (last match wins) */
        for (index = 0; index < current_ctx->patterns->nelts; index++) {
            ft_ignore_pattern_t *pattern = APR_ARRAY_IDX(current_ctx->patterns, index, ft_ignore_pattern_t *);
            int match = 0;

            /* Skip directory-only patterns if this is not a directory */
            if ((pattern->flags & FT_IGNORE_DIR_ONLY) && !is_dir) {
                continue;
            }

            /* Try to match */
            match = pcre_exec(pattern->regex, NULL, relative_path, (int) strlen(relative_path), 0, 0, NULL, 0);

            if (match >= 0) {
                /* Pattern matched */
                if (pattern->flags & FT_IGNORE_NEGATE) {
                    result = FT_IGNORE_MATCH_WHITELISTED;
                }
                else {
                    result = FT_IGNORE_MATCH_IGNORED;
                }
                /* Don't break - continue checking for later patterns */
            }
        }
    }

    return result;
}
