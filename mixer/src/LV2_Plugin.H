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
 * File:   LV2_Plugin.H
 * Author: sspresto
 *
 * Created on November 24, 2022, 1:36 PM
 */

#pragma once

#include "Module.H"
#include "Plugin_Module.H"
#include "CarlaUtils.H"


class LV2_Plugin : public Plugin_Module {

public:
    
    bool load_plugin ( const char* uri );


private:

    void init ( void );

#ifdef LV2_WORKER_SUPPORT
    void non_worker_init(Plugin_Module* plug,
                 const LV2_Worker_Interface* iface,
                 bool threaded);

    void non_worker_emit_responses( LilvInstance* instance );
    void non_worker_finish( void );
    void non_worker_destroy( void );
    static int send_to_ui(Plugin_Module* plug, uint32_t port_index, uint32_t type, uint32_t size, const void* body );
    void ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer );
    void send_file_to_plugin( int port, const std::string &filename );
    void send_atom_to_plugin( uint32_t port_index, uint32_t buffer_size, const void* buffer);
    void apply_ui_events( uint32_t nframes, unsigned int port );
    void set_lv2_port_properties ( Port * port, bool writable );
    void get_atom_output_events( void );
    int write_atom_event(ZixRing* target, const uint32_t port_index,
                 const uint32_t    size,
                 const LV2_URID    type,
                 const void* const body);
#endif  // LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
    void process_midi_in_events( uint32_t nframes, unsigned int port );
    void process_midi_out_events( uint32_t nframes, unsigned int port );
#endif
#ifdef USE_SUIL
    bool try_custom_ui();
    bool custom_ui_instantiate();
    const LilvUI* try_X11_ui (const char* native_ui_type);
#ifdef LV2_EXTERNAL_UI
    const LilvUI* try_external_ui (const char* native_ui_type);
#endif
    const LilvUI* try_showInterface_ui(const char* native_ui_type);
    bool send_to_custom_ui( uint32_t port_index, uint32_t size, uint32_t type, const void* buf );
    void update_custom_ui();
    void update_ui_settings();
    void custom_update_ui();
    static void custom_update_ui ( void * );
    void init_x();
    void close_custom_ui();
    Window getChildWindow() const;
#ifdef USE_CARLA
    void show_custom_ui();
    void hide_custom_ui();
    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild);
    bool isUiResizable() const;   
#endif  // USE_CARLA
#endif  // USE_SUIL

public:

    LV2_Plugin ( );
    virtual ~LV2_Plugin();

    virtual void process ( nframes_t );

protected:

    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};
