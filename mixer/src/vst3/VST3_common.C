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
#include "VST3_common.H"

namespace nmxt_common
{

struct GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

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

std::string
utf16_to_utf8( const std::u16string& utf16 )
{
    std::wstring_convert < std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string utf8 = convert.to_bytes ( utf16 );
    return utf8;
}

std::u16string
utf8_to_utf16( const std::string & utf8String )
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    std::u16string utf16String = converter.from_bytes( utf8String );
    return utf16String;
}

}   // namespace nmxt_common
