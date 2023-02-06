/*
 * Carla common utils
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

#ifndef CARLA_UTILS_HPP_INCLUDED
#define CARLA_UTILS_HPP_INCLUDED

#include "../../nonlib/debug.h"
#include <cstring>


#define CARLA_SAFE_ASSERT_CONTINUE(cond)      if (__builtin_expect(!(cond),0)) { carla_safe_assert(#cond, __FILE__, __LINE__); continue; }
#define CARLA_SAFE_ASSERT_RETURN(cond, ret)   if (__builtin_expect(!(cond),0)) { carla_safe_assert(#cond, __FILE__, __LINE__); return ret; }

/*
 * Print a safe assertion error message.
 */
static inline
void carla_safe_assert(const char* const assertion, const char* const file, const int line) noexcept
{
    WARNING("Non-mixer assertion failure: \"%s\" in file %s, line %i", assertion, file, line);
}

/*
 * Clear a single struct.
 */
template <typename T>
static inline
void carla_zeroStruct(T& s) noexcept
{
    std::memset(&s, 0, sizeof(T));
}

#endif // CARLA_UTILS_HPP_INCLUDED
