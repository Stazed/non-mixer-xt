/*******************************************************************************/
/* Copyright (C) 2007-2022 David Robillard <d@drobilla.net>                    */
/* Copyright (C) 2005-2022, rncbc aka Rui Nuno Capela.                         */
/* Copyright (C) 2011-2022 Filipe Coelho <falktx@falktx.com>                   */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
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

#ifdef LV2_SUPPORT

#include <zix-0/zix/ring.h>
#include <zix-0/zix/sem.h>
#include <zix-0/zix/thread.h>
#include <suil/suil.h>

#include "../Mixer_Strip.H"
#include "../Module.H"
#include "../Plugin_Module.H"
#include "../x11/XTUtils.H"
#include "../x11/X11PluginUI.H"
#include "LV2_RDF_Utils.hpp"
#include "ImplementationData.H"
#include "atom-forge.h"
#include "LV2_PortBackend.H"


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


class LV2_Plugin : public Plugin_Module,
    private X11PluginUI::Callback
{

public:

    ImplementationData *_idata;

    virtual bool load_plugin ( Module::Picked picked ) override;
    void get_plugin_extensions();
    void create_audio_ports();
    void create_control_ports();
    void create_atom_ports();
    void initialize_presets(const std::string &uri);

    bool configure_inputs ( int ) override;
    void handle_port_connection_change ( void ) override;
    void handle_chain_name_changed ( void ) override;
    void handle_sample_rate_change ( nframes_t sample_rate ) override;
    void resize_buffers ( nframes_t buffer_size ) override;

    virtual bool bypass ( void ) const override
    {
        return *_bypass == 1.0f;
    }
    virtual void bypass ( bool v ) override;

    void freeze_ports ( void ) override;
    void thaw_ports ( void ) override;
    void clear_midi_vectors() override;

    // Preset support
    std::vector<LV2_RDF_Preset> _PresetList;
    const LilvPlugin*     _lilv_plugin;     /**< Plugin "class" (actually just a few strings) */
    LilvWorld* 	_lilvWorld{};
    const LilvPlugins*	_lilvPlugins{};
    LilvInstance*   _lilv_instance{};       /**< Plugin "instance" (loaded shared lib) */
    LV2_URID_Map* _uridMapFt;
    LilvWorld* get_lilv_world()
    {
        return _lilvWorld;
    }
    const LilvPlugin* get_slv2_plugin() const
    {
        return _lilv_plugin;
    }
    const LilvPlugins* get_lilv_plugins() const
    {
        return _lilvPlugins;
    }

    void set_control_value(unsigned long port_index, float value);
    void update_control_parameters(int choice);
    // End preset support

    // State save
    LV2_URID_Unmap* _uridUnmapFt;
    std::string _project_directory;

#ifdef LV2_WORKER_SUPPORT
    struct WorkerContext
    {
        ZixRing*  zix_requests;     ///< Requests to the worker
        ZixRing*  zix_responses;    ///< Responses from the worker
        ZixRing*  plugin_to_ui;     ///< Port events from plugin
        ZixRing*  ui_to_plugin;     ///< Port events from UI
        void*     ui_event_buf;     ///< Buffer for reading UI port events
        void*     worker_response;  ///< Worker response buffer
        ZixSem    zix_sem;          ///< Worker semaphore
        ZixThread zix_thread;       ///< Worker thread
        bool      threaded;         ///< Run work in another thread
        ZixSem    work_lock;        ///< Lock for plugin work() method
        bool      exit_process;     ///< True iff execution is finished

        WorkerContext()
            : zix_requests( nullptr ), zix_responses( nullptr ),
              plugin_to_ui( nullptr ), ui_to_plugin( nullptr ),
              ui_event_buf( nullptr ), worker_response( nullptr ),
              zix_sem(), zix_thread( 0 ), threaded( false ),
              work_lock(), exit_process( false ) {}

        void init( LV2_Plugin& plug,
                   const LV2_Worker_Interface* iface,
                   bool threaded );
        void emit_responses( LV2_Plugin& plug, LilvInstance* instance );
        void finish();
        void destroy();
        LV2_Worker_Status write_packet( ZixRing* target,
                                        uint32_t size,
                                        const void* data );
        LV2_Worker_Status respond( LV2_Plugin& plug,
                                   uint32_t size,
                                   const void* data );
        LV2_Worker_Status schedule( LV2_Plugin& plug, uint32_t size, const void* data );
        void* thread_main( LV2_Plugin& plug );
    } _worker;

    int _atom_ins;
    int _atom_outs;
    bool _loading_from_file;

    LV2_Atom_Forge _atom_forge;     ///< Atom forge
    bool    _safe_restore;          ///< Plugin restore() is thread-safe
    size_t  _atom_buffer_size;

    std::vector<Port> atom_input;
    std::vector<Port> atom_output;

    char *get_file ( int port_index ) const;
    void set_file (const std::string &file, int port_index );
    void ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer );
    void send_atom_to_plugin( uint32_t port_index, uint32_t buffer_size, const void* buffer);
    void send_file_to_plugin( int port, const std::string &filename );

private:
    void apply_ui_events( uint32_t nframes );
    void set_lv2_port_properties ( Port * port, bool writable );
    int write_atom_event(ZixRing* target, const uint32_t port_index,
                         const uint32_t    size,
                         const LV2_URID    type,
                         const void* const body);
    size_t get_atom_buffer_size(int port_index);
#endif  // LV2_WORKER_SUPPORT

public:

    // Use Suil
    SuilHost*     _ui_host;                 ///< Plugin UI host support
    SuilInstance* _ui_instance;             ///< Plugin UI instance (shared library)
    bool          _use_showInterface;
    bool          _use_X11_interface;
    LilvUIs*      _all_uis;                 ///< All plugin UIs (RDF data)
    const LilvUI*    _lilv_user_interface;  ///< Plugin UI (RDF data)
    const LilvNode*  _lilv_ui_type;         ///< Plugin UI type (unwrapped)

    // External UI
    bool          _use_external_ui;
    LV2UI_Widget  _lv2_ui_widget;
    LV2_External_UI_Host _lv2_ui_external_host;
    LV2_Feature          _lv2_ui_external_feature;
    LV2UI_Handle   _lv2_ui_handle;
private:
    const LilvUI* try_external_ui (const char* native_ui_type);
    // End external UI

public:
    /* X window for custom plugin ui */
    X11PluginUI *_X11_UI;
    bool _x_is_resizable;
    bool _x_is_visible;
    uint     _x_width;
    uint     _x_height;
    bool isUiResizable(LilvWorld* const world, const LilvUI* const ui) const;
    void show_custom_ui();
    void hide_custom_ui();
    bool try_custom_ui();
    bool send_to_custom_ui( uint32_t port_index, uint32_t size, uint32_t type, const void* buf );

private:
    bool custom_ui_instantiate();
    const LilvUI* try_X11_ui (const char* native_ui_type);
    const LilvUI* try_showInterface_ui(const char* native_ui_type);
    void update_custom_ui();
    void update_ui_settings();
    void custom_update_ui();
    static void custom_update_ui ( void * );
    void close_custom_ui();
    // End Suil

#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
public:
    void configure_midi_inputs () override;
    void configure_midi_outputs () override;

    int _midi_ins;
    int _midi_outs;
    uint32_t  _position;        ///< Transport position in frames
    float     _bpm;             ///< Transport tempo in beats per minute
    bool      _rolling;         ///< Transport speed (0=stop, 1=play)

private:
    void process_atom_in_events( uint32_t nframes, unsigned int port );
    void process_atom_out_events( uint32_t nframes, unsigned int port );
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT

private:

    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    bool loaded ( void ) const;

    void detect_plugin_extensions( bool& hasOptions,
                                   bool& hasState,
                                   bool& hasWorker ) const;
    void fetch_plugin_extensions( bool hasOptions,
                                  bool hasState,
                                  bool hasWorker );
    void validate_plugin_extensions();
#ifdef LV2_WORKER_SUPPORT
    void initialize_worker_support();
#endif

    void activate ( void );
    void deactivate ( void );
    void add_port ( const Port &p ) override;
    void init ( void ) override;

public:

    LV2_Plugin ( );
    virtual ~LV2_Plugin();

    nframes_t get_current_latency( void ) override;
    nframes_t get_module_latency ( void ) const override;
    void process ( nframes_t ) override;

    LOG_CREATE_FUNC( LV2_Plugin );
    MODULE_CLONE_FUNC( LV2_Plugin );

    // State save
    void save_LV2_plugin_state(const std::string &directory);
    void restore_LV2_plugin_state(const std::string &directory);

protected:

    void handlePluginUIClosed() override;
    void handlePluginUIResized(const uint width, const uint height) override;
    bool plugin_instances ( unsigned int );
    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

};

#endif // LV2_SUPPORT