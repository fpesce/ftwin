/*
 * Copyright (C) 2007-2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#ifndef DEBUG_H
#define DEBUG_H
/**
 * @file debug.h
 * @brief UTIL debug output macros
 */

#include <stdio.h>

/**
 * Display error message at the level error.
 * @param str The format of the string.
 * @param arg The arguments to use while printing the data.
 */
#define DEBUG_ERR(str, arg...) fprintf(stderr, "[DEBUG_ERR]: [%s] " str " at line %d\n", __FUNCTION__, ## arg, __LINE__), fflush(stderr)

/**
 * Display error message at the level debug.
 * @param str The format of the string.
 * @param arg The arguments to use while printing the data.
 */
#define DEBUG_DBG(str, arg...) fprintf(stderr, "[DEBUG_DBG]: [%s] " str " at line %d\n", __FUNCTION__, ## arg, __LINE__), fflush(stderr)

/** @} */

#endif /* DEBUG_H */
