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

#define CONFIG_VST3

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

#ifdef CONFIG_VST3

//----------------------------------------------------------------------
// class qtractor_vst3_scan -- VST3 plugin (bare bones) interface.
//

class qtractor_vst3_scan
{
public:

	// Constructor.
	qtractor_vst3_scan();

	// destructor.
	~qtractor_vst3_scan();

	// File loader.
        bool open(const std::string& sFilename);
	bool open_descriptor(unsigned long iIndex);
        std::string get_vst3_object_file(std::string filename);
        
	void close_descriptor();
	void close();

	// Properties.
	bool isOpen() const;

	// Properties.
        const std::string& name() const
		{ return m_sName; }

        const std::string& vendor() const
                { return m_sVendor; }

        const std::string& category() const
                { return m_sCategory; }     // FIXME TODO

        const std::string& subcategory() const
                { return m_sSubCategories; }     // FIXME TODO

        unsigned int uniqueID() const
		{ return m_iUniqueID; }

	int controlIns() const
		{ return m_iControlIns; }
	int controlOuts() const
		{ return m_iControlOuts; }

	int audioIns() const
		{ return m_iAudioIns; }
	int audioOuts() const
		{ return m_iAudioOuts; }

	int midiIns() const
		{ return m_iMidiIns; }
	int midiOuts() const
		{ return m_iMidiOuts; }

	bool hasEditor() const
		{ return m_bEditor; }

protected:

	// Forward decls.
	class Impl;

	// Cleaner/wiper.
	void clear();

private:

	// Instance variables.
	Impl         *m_pImpl;

        std::string   m_sName;
        std::string   m_sVendor;
        std::string   m_sCategory;
        std::string   m_sSubCategories;

	unsigned long m_iIndex;
	unsigned int  m_iUniqueID;
	int           m_iControlIns;
	int           m_iControlOuts;
	int           m_iAudioIns;
	int           m_iAudioOuts;
	int           m_iMidiIns;
	int           m_iMidiOuts;
	bool          m_bEditor;
};

void qtractor_vst3_scan_file ( const std::string& sFilename, std::list<Plugin_Module::Plugin_Info> & pr );

#else

bool lib_close(const lib_t lib);

bool do_vst3_check(lib_t& libHandle, const char* const filename, const bool doInit, std::list<Plugin_Module::Plugin_Info> & pr);

#endif	// CONFIG_VST3

}   // namespace vst3_discovery

#endif  // VST3_SUPPORT
