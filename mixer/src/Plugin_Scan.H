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

class Plugin_Info
{
public:
    const std::string type;
    std::string s_unique_id;
    unsigned long id;
    std::string plug_path;  // CLAP & VST3 binary path
    std::string name;
    std::string author;
    std::string category;
    int audio_inputs;
    int audio_outputs;
    bool favorite;

    Plugin_Info ( const std::string &s_type ) :  // LADSPA, LV2, CLAP, VST2, VST3
        type(s_type),
        s_unique_id("(null)"),  // (null) since we have to have something for favorites save and scan
        id(0),
        plug_path("(null)"),
        name("(null)"),
        author("(null)"),
        category("Unclassified"),
        audio_inputs(0),
        audio_outputs(0),
        favorite(false) {}

    bool operator< ( const Plugin_Info &rhs ) {
        return strcmp( name.c_str(), rhs.name.c_str() ) < 1;
    }
};


class Plugin_Scan
{
public:

    void get_all_plugins ( bool rescan );

    void close_scanner_window();

#ifdef LADSPA_SUPPORT
    void scan_LADSPA_plugins( std::list<Plugin_Info> & pr, bool rescan );
#endif
#ifdef LV2_SUPPORT
    void scan_LV2_plugins( std::list<Plugin_Info> & pr, bool rescan );
#endif
#ifdef CLAP_SUPPORT
    void scan_CLAP_plugins( std::list<Plugin_Info> & pr, bool rescan );
#endif
#ifdef VST2_SUPPORT
    void scan_VST2_plugins( std::list<Plugin_Info> & pr, bool rescan );
#endif
#ifdef VST3_SUPPORT
    void scan_VST3_plugins( std::list<Plugin_Info> & pr, bool rescan );
#endif

#ifdef LADSPA_SUPPORT
    void set_ladspainfo( LADSPAInfo * linfo );
    LADSPAInfo * get_ladspainfo();
#endif

    Plugin_Scan();
    virtual ~Plugin_Scan();
private:

    Fl_Box *_box;

    void show_scanner_window();
    bool load_plugin_cache ( void );
    void save_plugin_cache ( void );
};

