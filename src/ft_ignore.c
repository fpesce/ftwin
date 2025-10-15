/**
 * @file ft_ignore.c
 * @brief Implementation of the hierarchical .gitignore-style pattern matching logic.
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

#include "ft_ignore.h"
#include "debug.h"
#include <apr_file_io.h>
#include <apr_strings.h>
#include <string.h>
#include <ctype.h>

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
    char *result = apr_pcalloc(pool, MAX_PATTERN_LEN);
    const char *pattern_ptr = pattern;
    char *result_ptr = result;
    int starts_with_slash = 0;

    /* Reset flags and indicators */
    *flags = 0;
    *starts_with_slash = 0;

    /* Skip negation marker */
    if (*pattern_ptr == '!') {
	*flags |= FT_IGNORE_NEGATE;
	pattern_ptr++;
	while (isspace(*pattern_ptr)) {
	    pattern_ptr++;
	}
    }

    /* Check if pattern starts with / */
    if (*pattern_ptr == '/') {
	starts_with_slash = 1;
	pattern_ptr++;
    }

    /* Check if pattern ends with / (directory only) */
    const char *end = pattern_ptr + strlen(pattern_ptr) - 1;
    while (end > pattern_ptr && isspace(*end)) {
	end--;
    }
    if (*end == '/') {
	*flags |= FT_IGNORE_DIR_ONLY;
    }

    return p;
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
static void set_pcre_anchors(char **r, int starts_with_slash, unsigned int flags)
{
    char *cursor = *r;

    if (starts_with_slash) {
	*result_ptr++ = '^';
    }
    else {
	/* Pattern can match at any level */
	*result_ptr++ = '(';
	*result_ptr++ = '^';
	*result_ptr++ = '|';
	*result_ptr++ = '/';
	*result_ptr++ = ')';
    }

    /* Convert pattern to regex */
    while (*pattern_ptr) {
	/* Skip trailing slash for directory-only patterns */
	if (*pattern_ptr == '/' && *flags & FT_IGNORE_DIR_ONLY && *(pattern_ptr + 1) == '\0') {
	    break;
	}
    }
    else {
	/* Single star *: matches anything except a slash */
	append_pcre_sequence(r, "[^/]*");
	*p += 1;
    }
}

	if (*pattern_ptr == '\\' && *(pattern_ptr + 1)) {
	    /* Escaped character */
	    pattern_ptr++;
	    *result_ptr++ = '\\';
	    *result_ptr++ = *pattern_ptr++;
	}
	else if (*pattern_ptr == '*') {
	    if (*(pattern_ptr + 1) == '*') {
		/* Double star matches any number of directories */
		if (*(pattern_ptr + 2) == '/') {
		    /* Double star with slash pattern */
		    const char *pcre_pattern = "(.*/)?";
		    strcpy(result_ptr, pcre_pattern);
		    result_ptr += strlen(pcre_pattern);
		    pattern_ptr += 3;
		}
		else if (*(pattern_ptr - 1) == '/' || pattern_ptr == pattern) {
		    /* Slash double star at end or beginning */
		    const char *pcre_pattern = ".*";
		    strcpy(result_ptr, pcre_pattern);
		    result_ptr += strlen(pcre_pattern);
		    pattern_ptr += 2;
		}
		else {
		    /* Treat as single * */
		    const char *pcre_pattern = "[^/]*";
		    strcpy(result_ptr, pcre_pattern);
		    result_ptr += strlen(pcre_pattern);
		    pattern_ptr++;
		}
	    }
	    else {
		/* * matches anything except / */
		const char *pcre_pattern = "[^/]*";
		strcpy(result_ptr, pcre_pattern);
		result_ptr += strlen(pcre_pattern);
		pattern_ptr++;
	    }
	}
	else if (*pattern_ptr == '?') {
	    /* ? matches any single character except / */
	    const char *pcre_pattern = "[^/]";
	    strcpy(result_ptr, pcre_pattern);
	    result_ptr += strlen(pcre_pattern);
	    pattern_ptr++;
	}
	else if (*pattern_ptr == '[') {
	    /* Character class */
	    *result_ptr++ = '[';
	    pattern_ptr++;
	    if (*pattern_ptr == '!') {
		*result_ptr++ = '^';
		pattern_ptr++;
	    }
	    while (*pattern_ptr && *pattern_ptr != ']') {
		if (*pattern_ptr == '\\' && *(pattern_ptr + 1)) {
		    pattern_ptr++;
		    *result_ptr++ = '\\';
		}
		*result_ptr++ = *pattern_ptr++;
	    }
	    if (*pattern_ptr == ']')
		*result_ptr++ = *pattern_ptr++;
	}
	else if (*pattern_ptr == '/') {
	    *result_ptr++ = '/';
	    pattern_ptr++;
	}
	else if (strchr(".^$+{}()|", *pattern_ptr)) {
	    /* Escape regex metacharacters */
	    *result_ptr++ = '\\';
	    *result_ptr++ = *pattern_ptr++;
	}
	else {
	    *result_ptr++ = *pattern_ptr++;
	}
    }
}

static char *ft_glob_to_pcre(const char *pattern, apr_pool_t *pool, unsigned int *flags)
{
    char *result = apr_pcalloc(pool, MAX_PATTERN_LEN);
    char *r = result;
    int starts_with_slash = 0;
    const char *p = handle_negation_and_slashes(pattern, flags, &starts_with_slash);

    /* Set start anchor based on whether the pattern had a leading slash */
    set_pcre_anchors(&r, starts_with_slash, *flags);

    /* Convert the main body of the glob pattern */
    convert_glob_body_to_pcre(&p, &r, *flags);

    /* Set end anchor */
    if (*flags & FT_IGNORE_DIR_ONLY) {
	strcpy(result_ptr, "/?$");
    }
    else {
	*result_ptr++ = '$';
	*result_ptr = '\0';
    }

    *r = '\0';
    return result;
}

ft_ignore_context_t *ft_ignore_context_create(apr_pool_t *pool, ft_ignore_context_t * parent, const char *base_dir)
{
    ft_ignore_context_t *ctx = apr_pcalloc(pool, sizeof(ft_ignore_context_t));

    ctx->pool = pool;
    ctx->parent = parent;
    ctx->base_dir = apr_pstrdup(pool, base_dir);
    ctx->base_dir_len = strlen(base_dir);
    ctx->patterns = apr_array_make(pool, 16, sizeof(ft_ignore_pattern_t *));

    return ctx;
}

apr_status_t ft_ignore_add_pattern_str(ft_ignore_context_t * ctx, const char *pattern_str)
{
    const char *trimmed;
    unsigned int flags = 0;
    char *regex_str;
    pcre *regex;
    const char *error;
    int erroffset;
    ft_ignore_pattern_t *pattern;

    /* Trim whitespace */
    trimmed = pattern_str;
    while (isspace(*trimmed))
	trimmed++;

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

apr_status_t ft_ignore_load_file(ft_ignore_context_t * ctx, const char *filepath)
{
    apr_file_t *file;
    apr_status_t status;
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

ft_ignore_match_result_t ft_ignore_match(ft_ignore_context_t * ctx, const char *fullpath, int is_dir)
{
    ft_ignore_context_t *current_ctx;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;
    const char *relative_path;

    if (!ctx || !fullpath) {
	return FT_IGNORE_MATCH_NONE;
    }

    /* Walk up the context hierarchy */
    for (current_ctx = ctx; current_ctx != NULL; current_ctx = current_ctx->parent) {
	int i;

	/* Calculate relative path from this context's base_dir */
	if (strncmp(fullpath, current_ctx->base_dir, current_ctx->base_dir_len) == 0) {
	    relative_path = fullpath + current_ctx->base_dir_len;
	    /* Skip leading slash */
	    while (*relative_path == '/')
		relative_path++;
	}
	else {
	    /* Path not under this context's base, try parent */
	    continue;
	}

	/* Check patterns in order (last match wins) */
	for (i = 0; i < current_ctx->patterns->nelts; i++) {
	    ft_ignore_pattern_t *pattern = APR_ARRAY_IDX(current_ctx->patterns, i, ft_ignore_pattern_t *);
	    int match;

	    /* Skip directory-only patterns if this is not a directory */
	    if ((pattern->flags & FT_IGNORE_DIR_ONLY) && !is_dir) {
		continue;
	    }

	    /* Try to match */
	    match = pcre_exec(pattern->regex, NULL, relative_path, strlen(relative_path), 0, 0, NULL, 0);

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
