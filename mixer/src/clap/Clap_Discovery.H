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
 * File:   Clap_discovery.h
 * Author: sspresto
 *
 * Created on October 31, 2023, 3:58 PM
 */

#pragma once

#ifdef CLAP_SUPPORT

#include <filesystem>
#include <clap/clap.h>
#include <vector>

namespace clap_discovery
{

/*
 * Traverse the valid CLAP search paths looking for
 * paths to .clap instances
 */
std::vector<std::filesystem::path> installedCLAPs();

/*
 * Return a list of the valid CLAP search paths, per the spec
 * in entry.h.
 */
std::vector<std::filesystem::path> validCLAPSearchPaths();

/*
 * Given the path of a CLAP instance, undertake the code to open the
 * clap and resolve the clap_plugin_entry_t *, which can return
 * null in the case of an invalid CLAP file.
 */
const clap_plugin_entry_t *entryFromCLAPPath(const std::filesystem::path &p);

struct HostConfig
{
  public:
    bool announceQueriedExtensions{true};
};

clap_host_t *createCLAPInfoHost();
HostConfig *getHostConfig();

std::string get_plugin_category(const char* const* const features);

}   // namespace clap_discovery

#endif // CLAP_SUPPORT