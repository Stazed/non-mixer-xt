/*******************************************************************************/
/* Copyright (C) 2024- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
/*                                                                             */
/* This program is free software; you can redistribute it and/or modify it     */
/* under the terms of the GNU General Public License as published by the       */
/* Free Software Foundation; either version 2 of the License, or (at your      */
/* option) any later version.                                                  */
/*                                                                             */
/* This program is distributed in the hope that it will be useful, but WITHOUT */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   */
/* more details.                                                               */
/*                                                                             */
/* You should have received a copy of the GNU General Public License along     */
/* with This program; see the file COPYING.  If not,write to the Free Software */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */
/*******************************************************************************/

#include <cstdint>
#include <locale>
#include <codecvt>
#include <cstring>      // strcmp

#include "VST3_common.H"
#include "../../../nonlib/debug.h"

namespace nmxt_common
{

#if defined(__aarch64__) || defined(__arm64__)
#define V3_ARCHITECTURE "aarch64"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64)
#define V3_ARCHITECTURE "x86_64"
#else
#define V3_ARCHITECTURE "unknown"
#endif

#define CARLA_OS_SEP_STR   "/"

#if defined(__linux__) || defined(__linux)
#define V3_PLATFORM "linux"
#endif

#define V3_CONTENT_DIR V3_ARCHITECTURE "-" V3_PLATFORM

struct GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

std::vector<std::filesystem::path>
installedVST3s( )
{
    auto sp = validVST3SearchPaths ( );
    std::vector<std::filesystem::path> vst3s;

    for ( const auto &p : sp )
    {
        DMESSAGE ( "VST3 PLUG PATHS %s", p.u8string ( ).c_str ( ) );
        try
        {
            for ( auto const &dir_entry : std::filesystem::recursive_directory_iterator ( p ) )
            {
                if ( dir_entry.path ( ).extension ( ).u8string ( ) == ".vst3" )
                {
                    if ( std::filesystem::is_directory ( dir_entry.path ( ) ) )
                    {
                        vst3s.emplace_back ( dir_entry.path ( ) );
                    }
                }
            }
        }
        catch (const std::filesystem::filesystem_error &)
        {
            MESSAGE ( "Vst3 path directory not found - %s", p.u8string ( ).c_str ( ) );
        }
    }
    return vst3s;
}

/**
 * This returns all the search paths for VST3 plugin locations.
 * @return
 *      vector of filesystem::path of vst3 locations.
 */

std::vector<std::filesystem::path>
validVST3SearchPaths( )
{
    std::vector<std::filesystem::path> res;

    /* These are the standard locations for linux */
    res.emplace_back ( "/usr/lib/vst3" );
    res.emplace_back ( "/usr/lib/x86_64-linux-gnu/vst3" );

    // some distros make /usr/lib64 a symlink to /usr/lib so don't include it
    // or we get duplicates.
    if ( std::filesystem::is_symlink ( "/usr/lib64" ) )
    {
        if ( !strcmp ( std::filesystem::read_symlink ( "/usr/lib64" ).c_str ( ), "/usr/lib" ) )
            res.emplace_back ( "/usr/lib64/vst3" );
    }
    else
    {
        res.emplace_back ( "/usr/lib64/vst3" );
    }

    res.emplace_back ( "/usr/local/lib/vst3" );

    // some distros make /usr/local/lib64 a symlink to /usr/local/lib so don't include it
    // or we get duplicates.
    if ( std::filesystem::is_symlink ( "/us/local/lib64" ) )
    {
        if ( !strcmp ( std::filesystem::read_symlink ( "/usr/local/lib64" ).c_str ( ), "/usr/local/lib" ) )
            res.emplace_back ( "/usr/local/lib64/vst3" );
    }
    else
    {
        res.emplace_back ( "/usr/local/lib64/vst3" );
    }

    res.emplace_back ( std::filesystem::path ( getenv ( "HOME" ) ) / std::filesystem::path ( ".vst3" ) );

    return res;
}

// Convert filesystem paths for vst3 to binary file name of vst3
std::string
get_vst3_object_file( std::string filename )
{
    std::filesystem::path binaryfilename = filename;

    std::filesystem::path p = filename;

    binaryfilename += CARLA_OS_SEP_STR;

    binaryfilename += "Contents" CARLA_OS_SEP_STR V3_CONTENT_DIR CARLA_OS_SEP_STR;
    binaryfilename += p.stem ( );

    binaryfilename += ".so";

    if ( !std::filesystem::exists ( binaryfilename ) )
    {
        WARNING ( "Failed to find a suitable VST3 bundle binary %s", binaryfilename.c_str ( ) );
        return "";
    }

    return binaryfilename.c_str ( );
}

std::string
UIDtoString( bool comFormat, const char* _data )
{
    std::string result;
    result.reserve ( 32 );
    if ( comFormat )
    {
        const auto& g = reinterpret_cast<const GUID*> ( _data );

        char tmp[21] { };
        snprintf ( tmp, 21, "%08X%04X%04X", g->Data1, g->Data2, g->Data3 );
        result = tmp;

        for ( uint32_t i = 0; i < 8; ++i )
        {
            char s[3] { };
            snprintf ( s, 3, "%02X", g->Data4[i] );
            result += s;
        }
    }
    else
    {
        for ( uint32_t i = 0; i < 16; ++i )
        {
            char s[3] { };
            snprintf ( s, 3, "%02X", static_cast<uint8_t> ( _data[i] ) );
            result += s;
        }
    }
    return result;
}

std::string utf16_to_utf8(const std::u16string& utf16)
{
    std::string utf8;
    utf8.reserve(utf16.size());

    for (size_t i = 0; i < utf16.size(); ++i)
    {
        uint32_t codepoint = utf16[i];

        // Handle surrogate pairs
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
        {
            if (i + 1 >= utf16.size())
                throw std::runtime_error("Invalid UTF-16");

            uint32_t low = utf16[++i];
            if (low < 0xDC00 || low > 0xDFFF)
                throw std::runtime_error("Invalid UTF-16");

            codepoint = ((codepoint - 0xD800) << 10)
                      + (low - 0xDC00)
                      + 0x10000;
        }
        else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
        {
            throw std::runtime_error("Invalid UTF-16");
        }

        // Encode UTF-8
        if (codepoint <= 0x7F)
            utf8.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7FF)
        {
            utf8.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            utf8.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            utf8.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    return utf8;
}

std::u16string utf8_to_utf16(const std::string& utf8)
{
    std::u16string utf16;

    for (size_t i = 0; i < utf8.size();)
    {
        uint32_t codepoint = 0;
        unsigned char c = utf8[i];

        if (c <= 0x7F)
        {
            codepoint = c;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            if (i + 1 >= utf8.size())
                throw std::runtime_error("Invalid UTF-8");

            codepoint = ((c & 0x1F) << 6)
                      | (utf8[i + 1] & 0x3F);
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            if (i + 2 >= utf8.size())
                throw std::runtime_error("Invalid UTF-8");

            codepoint = ((c & 0x0F) << 12)
                      | ((utf8[i + 1] & 0x3F) << 6)
                      | (utf8[i + 2] & 0x3F);
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            if (i + 3 >= utf8.size())
                throw std::runtime_error("Invalid UTF-8");

            codepoint = ((c & 0x07) << 18)
                      | ((utf8[i + 1] & 0x3F) << 12)
                      | ((utf8[i + 2] & 0x3F) << 6)
                      | (utf8[i + 3] & 0x3F);
            i += 4;
        }
        else
        {
            throw std::runtime_error("Invalid UTF-8");
        }

        // Encode UTF-16
        if (codepoint <= 0xFFFF)
        {
            utf16.push_back(static_cast<char16_t>(codepoint));
        }
        else
        {
            codepoint -= 0x10000;
            utf16.push_back(static_cast<char16_t>((codepoint >> 10) + 0xD800));
            utf16.push_back(static_cast<char16_t>((codepoint & 0x3FF) + 0xDC00));
        }
    }

    return utf16;
}
}   // namespace nmxt_common
