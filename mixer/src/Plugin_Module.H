
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

#include "LADSPAInfo.h"

class Fl_Menu_Button;
class Thread;

class Plugin_Module : public Module {

    static Thread *plugin_discover_thread;

public:

    class Plugin_Info
    {
    public:
        const char *path;
        unsigned long id;
        std::string name;
        std::string author;
        std::string category;
        int audio_inputs;
        int audio_outputs;
        const char *type;
        bool favorite;

        Plugin_Info ( const char* s_type )  // LADSPA, LV2 -- TODO other types
            {
                path = 0;
                id = 0;

                audio_inputs = 0;
                audio_outputs = 0;
                type = s_type;
                favorite = 0;
            }

        bool operator< ( const Plugin_Info &rhs ) {
            return strcmp( name.c_str(), rhs.name.c_str() ) < 1;
        }
    };

private:

    nframes_t _last_latency;
    
    void bbox ( int &X, int &Y, int &W, int &H )
        {
            X = x();
            Y = y() + 5;
            W = w();
            H = h() - 10;
        }

public:

    LADSPAInfo *_ladspainfo;

    int _plugin_ins;
    int _plugin_outs;
    bool _crosswire;

private:
    static void *discover_thread ( void * );

    void connect_ports ( void );

public:

    virtual bool get_impulse_response ( sample_t * /*buf*/, nframes_t /*nframes*/ ){return false;};

    virtual nframes_t get_module_latency ( void ) const{return 0;};

    virtual void update ( void );

    static std::list<Plugin_Info> get_all_plugins ( void );
    void scan_LADSPA_plugins( std::list<Plugin_Info> & pr );
    void scan_LV2_plugins( std::list<Plugin_Info> & pr );

    static void spawn_discover_thread ( void );
    static void join_discover_thread ( void );

    Plugin_Module ( );
    virtual ~Plugin_Module();

    int plugin_ins ( void ) const { return _plugin_ins; }
    int plugin_outs ( void ) const { return _plugin_outs; }

    const char *name ( void ) const { return "Plugin"; }

    int can_support_inputs ( int );
    bool configure_inputs ( int );

#ifdef LV2_MIDI_SUPPORT
    void configure_midi_inputs ();
    void configure_midi_outputs ();
#endif

    virtual bool bypass ( void ) const { return *_bypass == 1.0f; }
    virtual void bypass ( bool /*v*/ ){};

    virtual void process ( nframes_t ){};

    void handle_port_connection_change ( void ){};
    void handle_sample_rate_change ( nframes_t /*sample_rate*/ ){};
    void resize_buffers ( nframes_t buffer_size );

    LOG_CREATE_FUNC( Plugin_Module );
    MODULE_CLONE_FUNC( Plugin_Module );

protected:

    volatile nframes_t _latency;
    void init ( void );

    void get ( Log_Entry & /*e*/ ) const{};
    void set ( Log_Entry &e );

};
