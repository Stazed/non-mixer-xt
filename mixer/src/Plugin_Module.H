
/*******************************************************************************/
/* Copyright (C) 2009 Jonathan Moore Liles                                     */
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

#pragma once

#include "Module.H"

#include "../../nonlib/Loggable.H"

#ifdef LADSPA_SUPPORT
#include "LADSPAInfo.h"
#endif

class Fl_Menu_Button;
class Thread;

class Plugin_Module : public Module {

    static Thread *plugin_discover_thread;

public:

    class Plugin_Info
    {
    public:
        std::string s_unique_id;
        unsigned long id;
        std::string clap_path;
        std::string name;
        std::string author;
        std::string category;
        int audio_inputs;
        int audio_outputs;
        const std::string type;
        bool favorite;

        Plugin_Info ( const std::string &s_type ) :  // LADSPA, LV2 -- TODO other types
            s_unique_id(),
            id(0),
            clap_path(),
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

private:

    nframes_t _last_latency;
    
    void bbox ( int &X, int &Y, int &W, int &H ) override
        {
            X = x();
            Y = y() + 5;
            W = w();
            H = h() - 10;
        }

public:
#ifdef LADSPA_SUPPORT
    LADSPAInfo *_ladspainfo;
#endif

    int _plugin_ins;
    int _plugin_outs;
    bool _crosswire;

private:
    static void *discover_thread ( void * );

    void connect_ports ( void );

public:

    virtual bool load_plugin ( Module::Picked /* picked */ ) {return false;};

    virtual void update ( void ) override;

    static std::list<Plugin_Info> get_all_plugins ( void );
#ifdef LADSPA_SUPPORT
    void scan_LADSPA_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef LV2_SUPPORT
    void scan_LV2_plugins( std::list<Plugin_Info> & pr );
#endif
#ifdef CLAP_SUPPORT
    void scan_CLAP_plugins( std::list<Plugin_Info> & pr );
#endif
    static void spawn_discover_thread ( void );
    static void join_discover_thread ( void );

    Plugin_Module ( );
    virtual ~Plugin_Module();

    int plugin_ins ( void ) const { return _plugin_ins; }
    int plugin_outs ( void ) const { return _plugin_outs; }

    const char *name ( void ) const override { return "Plugin"; }

    int can_support_inputs ( int ) override;
    bool configure_inputs ( int ) override;

    virtual bool bypass ( void ) const override { return *_bypass == 1.0f; }
    virtual void bypass ( bool /*v*/ ) override {};

    virtual void process ( nframes_t ) override {};

    void resize_buffers ( nframes_t buffer_size ) override;

    LOG_CREATE_FUNC( Plugin_Module );
    MODULE_CLONE_FUNC( Plugin_Module );

protected:

    std::string get_custom_data_location(const std::string &path);
#ifdef LADSPA_SUPPORT
    void set_ladspainfo( void );
#endif

    volatile nframes_t _latency;
    void init ( void ) override;

    void get ( Log_Entry & /*e*/ ) const override {};
    void set ( Log_Entry &e ) override;

};
