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
 * File:   Plugin_Scan.H
 * Author: sspresto
 *
 * Created on January 27, 2024, 4:53 PM
 */

#pragma once

#include <cstring>
#include <string>
#include <list>
#include <FL/Fl_Box.H>

#ifdef LADSPA_SUPPORT
#include "ladspa/LADSPAInfo.h"
#endif

#include "Plugin_Info.H"

class Plugin_Scan
{
public:

    void get_all_plugins (std::string s_type, std::string s_path);

#ifdef LADSPA_SUPPORT
    void scan_LADSPA_plugins( std::list<Plugin_Info> & pr);
#endif
#ifdef LV2_SUPPORT
    void scan_LV2_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef CLAP_SUPPORT
    void scan_CLAP_plugins( std::list<Plugin_Info> & pr, std::string clap_path );
#endif
#ifdef VST2_SUPPORT
    void scan_VST2_plugins( std::list<Plugin_Info> & pr, std::string vst2_path );
#endif
#ifdef VST3_SUPPORT
    void scan_VST3_plugins( std::list<Plugin_Info> & pr, std::string vst3_path );
#endif

    Plugin_Scan();
    virtual ~Plugin_Scan();
private:

    Fl_Box *_box;

    void save_plugin_cache ( void );
};

