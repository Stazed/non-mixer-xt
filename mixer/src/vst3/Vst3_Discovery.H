/*******************************************************************************/
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
 * File:   Vst3_Discovery.H
 * Author: sspresto
 *
 * Created on December 16, 2023, 5:44 AM
 */

#pragma once

#ifdef VST3_SUPPORT

#include <filesystem>
#include <vector>

#include "../Plugin_Module.H"
#include "utils/CarlaVst3Utils.hpp"

namespace vst3_discovery
{
/*
 * Traverse the valid VST3 search paths looking for
 * paths to .vst3 instances
 */
std::vector<std::filesystem::path> installedVST3s();

/*
 * Return a list of the valid CLAP search paths, per the spec
 * in entry.h.
 */
std::vector<std::filesystem::path> validVST3SearchPaths();


std::string get_plugin_category(const char* const* const features);

bool lib_close(const lib_t lib);

bool do_vst3_check(lib_t& libHandle, const char* const filename, const bool doInit, std::list<Plugin_Module::Plugin_Info> & pr);


}   // namespace vst3_discovery

#endif  // VST3_SUPPORT
