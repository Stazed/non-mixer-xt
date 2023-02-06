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
#include "XTUtils.H"
#include "LV2_RDF_Utils.hpp"
#include "ImplementationData.H"
#include "zix/ring.h"
#include "zix/sem.h"
#include "zix/thread.h"
#include "lv2/atom-forge.h"

#define PRESET_SUPPORT 1
#define USE_CARLA 1
#define LV2_STATE_SAVE 1
#define LV2_EXTERNAL_UI 1

#define ATOM_BUFFER_SIZE 16384  // 4096 * 4

/**
   Size factor for UI ring buffers -- JALV

   The ring size is a few times the size of an event output to give the UI a
   chance to keep up.  Experiments with Ingen, which can highly saturate its
   event output, led me to this value.  It really ought to be enough for
   anybody(TM).
*/
#define N_BUFFER_CYCLES 16

extern std::string project_directory;
extern std::string export_import_strip;
extern std::vector<std::string>remove_custom_data_directories;

#ifdef USE_CARLA
typedef void (*EventProcPtr)(XEvent* ev);
#endif


class LV2_Plugin : public Plugin_Module {

public:

    ImplementationData *_idata;
    
    bool load_plugin ( const char* uri );

    bool configure_inputs ( int );
    void handle_port_connection_change ( void );
    void handle_sample_rate_change ( nframes_t sample_rate );
    void resize_buffers ( nframes_t buffer_size );

    virtual bool bypass ( void ) const { return _bypass; }
    virtual void bypass ( bool v );

#ifdef PRESET_SUPPORT
    /* Preset loading stuff */
    std::vector<LV2_RDF_Preset> PresetList;
    const LilvPlugin*     m_plugin;     /**< Plugin "class" (actually just a few strings) */
    LilvWorld* 	m_lilvWorld{};
    const LilvPlugins*	m_lilvPlugins{};
    LilvInstance*   m_instance{};       /**< Plugin "instance" (loaded shared lib) */
    LV2_URID_Map* _uridMapFt;
    LilvWorld* get_lilv_world() {return m_lilvWorld;}
    const LilvPlugin* get_slv2_plugin() const {return m_plugin; }
    const LilvPlugins* get_lilv_plugins() const {return m_lilvPlugins; }

    void set_control_value(unsigned long port_index, float value);

    void update_control_parameters(int choice);
    /* End Preset loading */
#endif
    
#ifdef LV2_STATE_SAVE
    LV2_URID_Unmap* _uridUnmapFt;
    void save_LV2_plugin_state(const std::string directory);
    void restore_LV2_plugin_state(const std::string directory);
    std::string get_plugin_save_directory(const std::string directory);
    std::string m_project_directory;
#endif

#ifdef LV2_WORKER_SUPPORT
    ZixRing* requests;      ///< Requests to the worker
    ZixRing* responses;     ///< Responses from the worker
    ZixRing* plugin_to_ui;  ///< Port events from plugin
    ZixRing* ui_to_plugin;  ///< Port events from UI
    void*    ui_event_buf;  ///< Buffer for reading UI port events
    void*    response;      ///< Worker response buffer
    ZixSem   sem;           ///< Worker semaphore
    ZixThread thread;       ///< Worker thread
    LV2_Atom_Forge m_forge; ///< Atom forge
    bool     threaded;      ///< Run work in another thread
    ZixSem  work_lock;      ///< Lock for plugin work() method
    bool    m_exit;         ///< True iff execution is finished
    bool    m_safe_restore;   ///< Plugin restore() is thread-safe
    size_t  m_atom_buf_size;
#endif
    
#ifdef USE_SUIL
    SuilHost*     m_ui_host;     ///< Plugin UI host support
    SuilInstance* m_ui_instance; ///< Plugin UI instance (shared library)
    bool          m_use_showInterface;
    bool          m_use_X11_interface;
#ifdef LV2_EXTERNAL_UI
    bool          m_use_externalUI;
    LV2UI_Widget  m_lv2_ui_widget;
    LV2_External_UI_Host m_lv2_ui_external_host;
    LV2_Feature          m_lv2_ui_external_feature;
    LV2UI_Handle   m_lv2_ui_handle;
#endif
    LilvUIs*      m_uis;         ///< All plugin UIs (RDF data)
    const LilvUI*    m_ui;       ///< Plugin UI (RDF data)
    const LilvNode*  m_ui_type;  ///< Plugin UI type (unwrapped)

  /* X window for custom plugin ui */
#ifdef USE_CARLA
    Display* fDisplay;
    Window   fHostWindow;
    Window   fChildWindow;
    bool     fChildWindowConfigured;
    bool     fChildWindowMonitoring;
    bool     fIsVisible;
    bool     fFirstShow;
    bool     fSetSizeCalledAtLeastOnce;
    bool     fIsIdling;
    bool     fIsResizable;
    EventProcPtr fEventProc;
#endif  // USE CARLA
#endif  // USE_SUIL

#ifdef LV2_WORKER_SUPPORT
    int _atom_ins;
    int _atom_outs;
    bool _loading_from_file;
#endif
#ifdef LV2_MIDI_SUPPORT
    int _midi_ins;
    int _midi_outs;
    uint32_t  _position;        ///< Transport position in frames
    float     _bpm;             ///< Transport tempo in beats per minute
    bool      _rolling;         ///< Transport speed (0=stop, 1=play)
    bool      _is_instrument;
#endif

#ifdef LV2_WORKER_SUPPORT
    void ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer );
    void send_atom_to_plugin( uint32_t port_index, uint32_t buffer_size, const void* buffer);
    void send_file_to_plugin( int port, const std::string &filename );
#endif
#ifdef USE_SUIL
    bool try_custom_ui();
    bool send_to_custom_ui( uint32_t port_index, uint32_t size, uint32_t type, const void* buf );
#endif
    
private:

    void init ( void );
    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    void activate ( void );
    void deactivate ( void );
    bool loaded ( void ) const;

#ifdef LV2_WORKER_SUPPORT
    void non_worker_init(LV2_Plugin* plug,
                 const LV2_Worker_Interface* iface,
                 bool threaded);

    void non_worker_emit_responses( LilvInstance* instance );
    void non_worker_finish( void );
    void non_worker_destroy( void );
    void apply_ui_events( uint32_t nframes );
    void set_lv2_port_properties ( Port * port, bool writable );
    int write_atom_event(ZixRing* target, const uint32_t port_index,
                 const uint32_t    size,
                 const LV2_URID    type,
                 const void* const body);
    size_t get_atom_buffer_size(int port_index);
#endif  // LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
    void process_atom_in_events( uint32_t nframes, unsigned int port );
    void process_atom_out_events( uint32_t nframes, unsigned int port );
#endif
#ifdef USE_SUIL
    bool custom_ui_instantiate();
    const LilvUI* try_X11_ui (const char* native_ui_type);
#ifdef LV2_EXTERNAL_UI
    const LilvUI* try_external_ui (const char* native_ui_type);
#endif
    const LilvUI* try_showInterface_ui(const char* native_ui_type);
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

    nframes_t get_module_latency ( void ) const;
    void process ( nframes_t );

    LOG_CREATE_FUNC( LV2_Plugin );
    MODULE_CLONE_FUNC( LV2_Plugin );

protected:

    bool plugin_instances ( unsigned int );
    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};
