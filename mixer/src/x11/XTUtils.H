/*
 * From Carla common utils
 * Copyright (C) 2011-2020 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the doc/GPL.txt file.
 */

#pragma once

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)

#include "../../../nonlib/debug.h"
#include <cstring>

/* Define unlikely */
#ifdef __GNUC__
# define unlikely(x) __builtin_expect(x,0)
#else
# define unlikely(x) x
#endif


#define NON_SAFE_ASSERT_CONTINUE(cond)      if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); continue; }
#define NON_SAFE_ASSERT_RETURN(cond, ret)   if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); return ret; }


#define CARLA_SAFE_ASSERT_CONTINUE(cond)      if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); continue; }
#define CARLA_SAFE_ASSERT_RETURN(cond, ret)   if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); return ret; }

#define CARLA_SAFE_ASSERT(cond)               if (unlikely(!(cond))) non_safe_assert      (#cond, __FILE__, __LINE__);

/*
 * Print a safe assertion error message.
 */
static inline
void non_safe_assert(const char* const assertion, const char* const file, const int line) noexcept
{
    WARNING("Non-mixer assertion failure: \"%s\" in file %s, line %i", assertion, file, line);
}

/*
 * Clear a single struct.
 */
template <typename T>
static inline
void non_zeroStruct(T& s) noexcept
{
    std::memset(&s, 0, sizeof(T));
}

#endif  // #if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)