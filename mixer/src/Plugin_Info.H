/*******************************************************************************/
/* Copyright (C) 2008-2021 Jonathan Moore Liles (as "Non-Mixer")               */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
/*                                                                             */
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
 * File:   Plugin_Info.H
 * Author: sspresto
 *
 * Created on July 17, 2024, 11:07 PM
 */

#pragma once

#include <string>

const char PLUGIN_CACHE[] = "plugin_cache";
const char PLUGIN_CACHE_TEMP[] = "plugin_cache_temp";

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
    int midi_inputs;
    int midi_outputs;
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
        midi_inputs(0),
        midi_outputs(0),
        favorite(false) {}

    bool operator< ( const Plugin_Info &rhs )
    {
        return strcmp( name.c_str(), rhs.name.c_str() ) < 1;
    }
};