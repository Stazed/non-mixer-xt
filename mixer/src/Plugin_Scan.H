/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

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

#ifdef LADSPA_SUPPORT
#include "ladspa/LADSPAInfo.h"
#endif

class Plugin_Info
{
public:
    std::string s_unique_id;
    unsigned long id;
    std::string plug_path;  // CLAP & VST3 binary path
    std::string name;
    std::string author;
    std::string category;
    int audio_inputs;
    int audio_outputs;
    const std::string type;
    bool favorite;

    Plugin_Info ( const std::string &s_type ) :  // LADSPA, LV2, CLAP, VST2, VST3
        s_unique_id("(null)"),  // (null) since we have to have something for favorites save and scan
        id(0),
        plug_path(),
        name(),
        author(),
        category("Unclassified"),
        audio_inputs(0),
        audio_outputs(0),
        type(s_type),
        favorite(false) {}

    bool operator< ( const Plugin_Info &rhs ) {
        return strcmp( name.c_str(), rhs.name.c_str() ) < 1;
    }
};


class Plugin_Scan
{
public:

    void get_all_plugins ( void );

#ifdef LADSPA_SUPPORT
    void scan_LADSPA_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef LV2_SUPPORT
    void scan_LV2_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef CLAP_SUPPORT
    void scan_CLAP_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef VST2_SUPPORT
    void scan_VST2_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef VST3_SUPPORT
    void scan_VST3_plugins( std::list<Plugin_Info> & pr );
#endif

#ifdef LADSPA_SUPPORT
    void set_ladspainfo( LADSPAInfo * linfo );
    LADSPAInfo * get_ladspainfo();
#endif

    Plugin_Scan();
    virtual ~Plugin_Scan();
private:

};

