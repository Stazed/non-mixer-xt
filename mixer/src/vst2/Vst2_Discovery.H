/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

/* 
 * File:   Vst2_Discovery.H
 * Author: sspresto
 *
 * Created on January 12, 2024, 5:41 PM
 */

#pragma once

#ifdef VST2_SUPPORT

#include <string>
#include <vector>
#include <filesystem>
#include <list>

#include "vestige/vestige.h"
#include "CarlaLibUtils.hpp"
#include "../Plugin_Info.H"

/**
 * The controller stuff is only necessary for querying parameters, UI and MIDI which
 * we currently do not need.
 */
//#define VST2_CONTROLLER 1

namespace vst2_discovery
{

/*
 * Traverse the valid VST2 search paths looking for
 * paths to .vst2 instances
 */
std::vector<std::filesystem::path> installedVST2s();

/*
 * Return a list of the valid VST2 search paths.
 */
std::vector<std::filesystem::path> validVST2SearchPaths();

    
typedef struct _AEffect AEffect;


class vst2_discovery_scan
{
public:

    // Constructor.
    vst2_discovery_scan();

    // destructor.
    ~vst2_discovery_scan();

    // File loader.
    bool open(const std::string& sFilename);
    bool open_descriptor(unsigned long iIndex = 0);
    void close_descriptor();
    void close();

    // Properties.
    bool isOpen() const;

    const std::string& name() const { return m_sName; }
    const std::string& vendor() const { return m_sVendor; }
    const std::string& category() const { return m_sCategory; }

    unsigned int uniqueID() const;

    int numInputs() const;
    int numOutputs() const;

#ifdef VST2_CONTROLLER
    int numPrograms() const;
    int numParams() const;
    int numMidiInputs() const;
    int numMidiOutputs() const;
    bool hasEditor() const;
    bool hasProgramChunks() const;
#endif

    // VST2 host dispatcher.
    int vst2_dispatch(
            long opcode, long index, long value, void *ptr, float opt) const;

protected:

    // VST2 flag inquirer.
    bool vst2_canDo(const char *pszCanDo) const;

private:

    // Instance variables.
    lib_t         m_pLibrary;
    AEffect      *m_pEffect;
    unsigned int  m_iFlagsEx;
    bool          m_bEditor;
    std::string   m_sName;
    std::string   m_sVendor;
    std::string   m_sCategory;
};

void vst2_discovery_scan_file ( const std::string& sFilename, std::list<Plugin_Info> & vst2pr );
    
}   // namespace vst2_discovvery


#endif  // VST3_SUPPORT