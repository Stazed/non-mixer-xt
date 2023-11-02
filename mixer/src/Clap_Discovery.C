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
 * File:   Clap_discovery.cpp
 * Author: sspresto
 * 
 * Created on October 31, 2023, 3:58 PM
 */

#ifdef CLAP_SUPPORT

#include <iostream>     // cerr
#include <dlfcn.h>      // dlopen, dlerror, dlsym

#include "Clap_Discovery.H"
#include "../../nonlib/debug.h"

namespace clap_discovery
{

std::vector<std::filesystem::path>
installedCLAPs()
{
    auto sp = validCLAPSearchPaths();
    std::vector<std::filesystem::path> claps;

    for (const auto &p : sp)
    {
        try
        {
            for (auto const &dir_entry : std::filesystem::recursive_directory_iterator(p))
            {
                if (dir_entry.path().extension().u8string() == ".clap")
                {
                    if (!std::filesystem::is_directory(dir_entry.path()))
                    {
                        claps.emplace_back(dir_entry.path());
                    }
                }
            }
        }

        catch (const std::filesystem::filesystem_error &)
        {
            DMESSAGE("filesystem_error!");
        }
    }
    return claps;
}

/**
 * This returns all the search paths for CLAP plugin locations.
 * @return 
 *      vector of filesystem::path of clap locations.
 */

std::vector<std::filesystem::path>
validCLAPSearchPaths()
{
    std::vector<std::filesystem::path> res;

    /* These are the standard locations for linux */
    res.emplace_back("/usr/lib/clap");
    res.emplace_back(std::filesystem::path(getenv("HOME")) / std::filesystem::path(".clap"));

    /* This is possibly set for individual computers */
    auto p = getenv("CLAP_PATH");

    DMESSAGE("GET ENV CLAP_PATH = %s", p);

    if (p)
    {
        auto sep = ':';
        auto cp = std::string(p);

        size_t pos;
        while ((pos = cp.find(sep)) != std::string::npos)
        {
            auto item = cp.substr(0, pos);
            cp = cp.substr(pos + 1);
            res.emplace_back(std::filesystem::path{item});
        }

        if (cp.size())
        {
            res.emplace_back(std::filesystem::path{cp});
        }
    }

    return res;
}

const clap_plugin_entry_t *entryFromCLAPPath(const std::filesystem::path &p)
{
    void *handle;
    int *iptr;

    handle = dlopen(p.u8string().c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!handle)
    {
        DMESSAGE("dlopen failed on Linux: %s", dlerror());
	return nullptr;
    }

    iptr = (int *)dlsym(handle, "clap_entry");

    return (clap_plugin_entry_t *)iptr;
}

static std::unique_ptr<HostConfig> static_host_config;

const void *get_extension(const struct clap_host * /* host */, const char *eid)
{
    if (static_host_config->announceQueriedExtensions)
    {
        DMESSAGE("Plugin->Host : Requesting Extension %s", eid);
    }

    return nullptr;
}

void request_restart(const struct clap_host * /* host */) {}

void request_process(const struct clap_host * /* host */) {}

void request_callback(const struct clap_host * /* host */) {}

static clap_host clap_info_host_static{CLAP_VERSION_INIT,
                                       nullptr,
                                       PACKAGE,
                                       "Non-Mixer-XT team",
                                       WEBSITE,
                                       VERSION,
                                       get_extension,
                                       request_restart,
                                       request_process,
                                       request_callback};

clap_host_t *createCLAPInfoHost()
{
    if (!static_host_config)
    {
        static_host_config = std::make_unique<HostConfig>();
    }

    return &clap_info_host_static;
}

HostConfig *getHostConfig()
{
    if (!static_host_config)
    {
        DMESSAGE("Please call createCLAPInfoHost before getHostConfig()");
        return nullptr;
    }
    return static_host_config.get();
}

}   // namespace clap_discovery

#endif   // CLAP_SUPPORT
