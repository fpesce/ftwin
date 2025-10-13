/**
 * @file ft_system.h
 * @brief System-related utility functions.
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

#ifndef FT_SYSTEM_H
#define FT_SYSTEM_H

/**
 * @brief Get the number of available CPU cores on the current system.
 *
 * This function attempts to determine the number of online processors.
 * It serves as a sensible default for setting the number of worker threads.
 *
 * @return The number of CPU cores, or a reasonable fallback (e.g., 4) if detection fails.
 */
unsigned int ft_get_cpu_cores(void);

#endif /* FT_SYSTEM_H */
