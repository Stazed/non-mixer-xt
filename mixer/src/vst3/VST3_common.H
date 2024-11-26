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
/* 
 * File:   VST3_common.H
 * Author: sspresto
 *
 * Created on November 25, 2024, 4:28 PM
 */

#ifndef VST3_COMMON_H
#define VST3_COMMON_H

#include <string>
#include <vector>
#include <filesystem>

namespace nmxt_common
{

/*
 * Traverse the valid VST3 search paths looking for
 * paths to .vst3 instances
 */
std::vector<std::filesystem::path> installedVST3s();

/*
 * Return a list of the valid VST3 search paths, per the spec
 * in entry.h.
 */
std::vector<std::filesystem::path> validVST3SearchPaths();

std::string get_vst3_object_file(std::string filename);

std::string UIDtoString (bool comFormat, const char* _data);
std::string utf16_to_utf8(const std::u16string& utf16);
std::u16string utf8_to_utf16( const std::string & utf8String );

}

#endif /* STRING_FORMAT_H */

