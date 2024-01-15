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

# define CARLA_CATCH_UNWIND

#define NON_SAFE_ASSERT_CONTINUE(cond)      if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); continue; }
#define NON_SAFE_ASSERT_RETURN(cond, ret)   if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); return ret; }


#define CARLA_SAFE_ASSERT_CONTINUE(cond)      if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); continue; }
#define CARLA_SAFE_ASSERT_RETURN(cond, ret)   if (__builtin_expect(!(cond),0)) { non_safe_assert(#cond, __FILE__, __LINE__); return ret; }

#define CARLA_SAFE_ASSERT(cond)               if (unlikely(!(cond))) non_safe_assert      (#cond, __FILE__, __LINE__);
#define CARLA_SAFE_EXCEPTION_RETURN(msg, ret) CARLA_CATCH_UNWIND catch(...) { carla_safe_exception(msg, __FILE__, __LINE__); return ret; }

/*
 * Print a safe assertion error message.
 */
static inline
void non_safe_assert(const char* const assertion, const char* const file, const int line) noexcept
{
    WARNING("Non-mixer assertion failure: \"%s\" in file %s, line %i", assertion, file, line);
}

/*
 * Print a safe exception error message.
 */
static inline
void carla_safe_exception(const char* const exception, const char* const file, const int line) noexcept
{
    WARNING("Carla exception caught: \"%s\" in file %s, line %i", exception, file, line);
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

/*
 * Clear a struct array.
 */
template <typename T>
static inline
void non_zeroStructs(T structs[], const std::size_t count) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(structs != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(count > 0,);

    std::memset(structs, 0, count*sizeof(T));
}

/*
 * Clear a pointer array.
 */
template<typename T>
static inline
void carla_zeroPointers(T* ptrs[], const std::size_t count) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(ptrs != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(count > 0,);

    std::memset(ptrs, 0, count*sizeof(T*));
}

#endif  // #if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)