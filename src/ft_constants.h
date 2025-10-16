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

#ifndef FT_CONSTANTS_H
#define FT_CONSTANTS_H

/** @def ERROR_BUFFER_SIZE
 * @brief Default size for error message buffers.
 *
 * This constant defines a standard size for character arrays used to store
 * error messages, ensuring consistency across the application.
 */
#define ERROR_BUFFER_SIZE 128

/** @def XXH32_SEED
 * @brief Seed for the XXH32 hashing algorithm.
 *
 * This constant defines the seed value used for the XXH32 string hashing
 * function to ensure consistent hash results.
 */
#define XXH32_SEED 0

#endif /* FT_CONSTANTS_H */