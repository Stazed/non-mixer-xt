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
 * File:   CLAP_Plugin.H
 * Author: sspresto
 *
 * Created on November 1, 2023, 7:10 PM
 */

#pragma once

#ifdef CLAP_SUPPORT

#include <clap/clap.h>
#include "Plugin_Module.H"

class CLAP_Plugin : public Plugin_Module {

public:

    virtual bool load_plugin ( Module::Picked picked );

    bool configure_inputs ( int );
    void handle_port_connection_change ( void );
    void handle_chain_name_changed ( void );
    void handle_sample_rate_change ( nframes_t sample_rate );
    void resize_buffers ( nframes_t buffer_size );

    virtual bool bypass ( void ) const { return *_bypass == 1.0f; }
    virtual void bypass ( bool v );

    void freeze_ports ( void );
    void thaw_ports ( void );
    
    void configure_midi_inputs ();
    void configure_midi_outputs ();

    nframes_t get_module_latency ( void ) const;
    void process ( nframes_t );

    LOG_CREATE_FUNC( CLAP_Plugin );
    MODULE_CLONE_FUNC( CLAP_Plugin );

    CLAP_Plugin();
    virtual ~CLAP_Plugin();

private:

    const clap_plugin_entry *_entry;
    const clap_plugin_factory *_factory;

    const clap_plugin_descriptor *_descriptor;

    clap_host_t *_host;

    const clap_plugin_t *_plugin;

    int _midi_ins;
    int _midi_outs;

    int _control_ins;
    int _control_outs;

public:
    const clap_plugin_entry_t *entry_from_CLAP_file(const char *f);

    static const void *get_extension(const struct clap_host * host, const char *eid);
    static void request_restart(const struct clap_host * host);
    static void request_process(const struct clap_host * host);
    static void request_callback(const struct clap_host * host);

private:

    void create_audio_ports();
    void create_note_ports();
    void create_control_ports();
    void activate ( void );
    void deactivate ( void );
    void add_port ( const Port &p );
    void init ( void );

protected:

    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};

#endif  // CLAP_SUPPORT 

