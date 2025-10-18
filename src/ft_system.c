/**
 * @file ft_system.c
 * @brief Implementation of system-related utility functions.
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

#include "ft_system.h"

#if defined(__unix__) || defined(__unix) || defined(unix) || \
    (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

unsigned int ft_get_cpu_cores(void)
{
#if defined(_SC_NPROCESSORS_ONLN)
    /* POSIX systems (Linux, macOS, BSD) */
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs > 0) {
        return (unsigned int) nprocs;
    }
    /* Fall through to default if sysconf fails */
#elif defined(_WIN32)
    /* Windows */
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    if (sysinfo.dwNumberOfProcessors > 0) {
        return (unsigned int) sysinfo.dwNumberOfProcessors;
    }
    /* Fall through to default if GetSystemInfo fails */
#endif

    /* Fallback to a reasonable default */
    return 4;
}
