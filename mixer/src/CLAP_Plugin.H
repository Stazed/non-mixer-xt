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
#include <unordered_map>

#include "EventList.H"
#include "Mixer_Strip.H"
#include "Plugin_Module.H"
#include "LinkedList.hpp"

extern std::string project_directory;
extern std::string export_import_strip;
extern std::vector<std::string>remove_custom_data_directories;

typedef void (*EventProcPtr)(XEvent* ev);

struct HostTimerDetails {
    clap_id clapId;
    uint32_t periodInMs;
    uint32_t lastCallTimeInMs;
};

class CLAP_Plugin : public Plugin_Module {

public:

    virtual bool load_plugin ( Module::Picked picked );

    bool configure_inputs ( int );
    void handle_port_connection_change ( void );
    void handle_chain_name_changed ( void );
    void handle_sample_rate_change ( nframes_t sample_rate );
    void resize_buffers ( nframes_t buffer_size );

    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    bool loaded ( void ) const;
    bool process_reset();

    void process_jack_transport ( uint32_t nframes );
    void process_jack_midi_in ( uint32_t nframes, unsigned int port );
    void process_midi_in (unsigned char *data, unsigned int size,
		unsigned long offset, unsigned short port);

    void process_jack_midi_out ( uint32_t nframes, unsigned int port );

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
    
    std::vector<Port> note_input;
    std::vector<Port> note_output;

    CLAP_Plugin();
    virtual ~CLAP_Plugin();

private:

    const clap_plugin_entry *_entry;
    const clap_plugin_factory *_factory;

    const clap_plugin_descriptor *_descriptor;
    
    std::string _clap_path;
    std::string _clap_id;

    clap_host _host;
    clap_process _process;

    // Working state stream buffer.
    void* _last_chunk;
    
    clap_event_transport m_transport;
    uint32_t  _position;        ///< Transport position in frames
    float     _bpm;             ///< Transport tempo in beats per minute
    bool      _rolling;         ///< Transport speed (0=stop, 1=play)

    bool _is_processing;
    bool _activated;
    bool _plug_needs_callback;
    bool _plug_request_restart;

    // GUI Editor stuff...
    bool m_bEditorCreated;
    bool m_bEditorVisible;

    // X window for custom plugin
    Display* _x_display;
    Window   _x_host_window;
    Window   _x_child_window;
    bool     _x_child_window_configured;
    bool     _x_child_window_monitoring;
    bool     _x_is_visible;
    bool     _x_first_show;
    bool     _x_set_size_called_at_least_Once;
    bool     _x_is_idling;
    bool     _x_is_resizable;
    bool     _is_floating;
    EventProcPtr _x_event_proc;
    
    clap_audio_buffer _audio_ins;
    clap_audio_buffer _audio_outs;

    float ** _audio_in_buffers;
    float ** _audio_out_buffers;

  //  int _audioIN_channel_count;
  //  int _audioOUT_channel_count;

    const clap_plugin_t *_plugin;

    const clap_plugin_params *m_params;
    const clap_plugin_timer_support *m_timer_support;
    const clap_plugin_posix_fd_support *m_posix_fd_support;

    const clap_plugin_gui *m_gui;
    const clap_plugin_state *m_state;
    std::string _project_file;

    const clap_plugin_note_name *m_note_names;

    std::unordered_map<clap_id, const clap_param_info *> m_param_infos;
    std::unordered_map<int, double> m_paramValues;
    std::unordered_map<int, unsigned long> m_paramIds;

    void init_x();
    bool isUiResizable() const;
    bool show_custom_ui();
    bool hide_custom_ui();

    Window getChildWindow() const;
    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild);

    void custom_update_ui_x();
    static void custom_update_ui ( void * );

    int _midi_ins;
    int _midi_outs;

    int m_iMidiDialectIns;
    int m_iMidiDialectOuts;

public:
    const clap_plugin_entry_t *entry_from_CLAP_file(const char *f);
    
    static void setup_host( clap_host *host, void *host_data );
    static const void *get_extension(const struct clap_host * host, const char *eid);
    static void request_restart(const struct clap_host * host);
    static void request_process(const struct clap_host * host);
    static void request_callback(const struct clap_host * host);

    void plugin_request_restart ();
    void plugin_request_callback ();

    // Parameters info/ids (de)initializer.
    void addParamInfos();
    void clearParamInfos();

    // Set/add a parameter value/point.
    void setParameter (clap_id id, double alue);

    // Get current parameter value.
    double getParameter (clap_id id) const;
    
    // Parameters update methods.
    void updateParamValues(bool update_custom_ui);

    void addParams();
    void clearParams();
    void rescan_parameters();

    // Events processor buffers.
    EventList m_events_in;
    EventList m_events_out;

    // Parameters processor queue.
    EventList m_params_out;

    void save_CLAP_plugin_state(const std::string filename);
    void restore_CLAP_plugin_state(const std::string filename);

    uint64_t getState (void** const dataPtr);

    LinkedList<HostTimerDetails> fTimers;

    volatile bool m_params_flush;

    // Event buffer accessors.
    EventList& events_in  () { return m_events_in;  }
    EventList& events_out () { return m_events_out; }

    EventList& params_out () { return m_params_out; }

    // Plugin parameters flush.
    void plugin_params_flush ();

    // Open/close editor widget.
    bool try_custom_ui();

private:

    void initialize_plugin();
    void create_audio_ports();
    void create_note_ports();
    void create_control_ports();
    void activate ( void );
    void deactivate ( void );
    void add_port ( const Port &p );
    void init ( void );

    // Transfer parameter changes...
    void process_params_out ();
    static void parameter_update ( void * );
    void update_parameters();
    void set_control_value(unsigned long port_index, float value, bool update_custom_ui);

protected:

    // Transfer parameter changes...
 //   void process_params_out ();
//    void update_parameters();
//    void set_control_value(unsigned long port_index, float value, bool update_custom_ui);

    static void host_gui_resize_hints_changed (
            const clap_host *host);
    static bool host_gui_request_resize (
            const clap_host *host, uint32_t width, uint32_t height);
    static bool host_gui_request_show (
            const clap_host *host);
    static bool host_gui_request_hide (
            const clap_host *host);
    static void host_gui_closed (
            const clap_host *host, bool was_destroyed);
    
    static const constexpr clap_host_gui g_host_gui = {
            host_gui_resize_hints_changed,
            host_gui_request_resize,
            host_gui_request_show,
            host_gui_request_hide,
            host_gui_closed,
    };

    void plugin_gui_resize_hints_changed (void);
    bool plugin_gui_request_resize(uint32_t width, uint32_t height);
    bool plugin_gui_request_show(void);
    bool plugin_gui_request_hide(void);
    void plugin_gui_closed (bool was_destroyed);

    static bool host_register_timer (
            const clap_host *host, uint32_t period_ms, clap_id *timer_id);
    static bool host_unregister_timer (
            const clap_host *host, clap_id timer_id);

    static const constexpr clap_host_timer_support g_host_timer_support = {
            host_register_timer,
            host_unregister_timer,
    };

    // Host Parameters callbacks...
    static void host_params_rescan (
            const clap_host *host, clap_param_rescan_flags flags);
    static void host_params_clear (
            const clap_host *host, clap_id param_id, clap_param_clear_flags flags);
    static void host_params_request_flush (
            const clap_host *host);

    static const constexpr clap_host_params g_host_params = {
            host_params_rescan,
            host_params_clear,
            host_params_request_flush,
    };

    void plugin_params_rescan (clap_param_rescan_flags flags);
    void plugin_params_clear (clap_id param_id, clap_param_clear_flags flags);
    void plugin_params_request_flush ();

    // Host state callbacks...
    static void host_state_mark_dirty (
            const clap_host *host);

    static const constexpr clap_host_state g_host_state = {
            host_state_mark_dirty,
    };

    // Host Audio Ports support callbacks...
    static bool host_audio_ports_is_rescan_flag_supported (
            const clap_host *host, uint32_t flag);
    static void host_audio_ports_rescan (
            const clap_host *host, uint32_t flags);

    static const constexpr clap_host_audio_ports g_host_audio_ports = {
            host_audio_ports_is_rescan_flag_supported,
            host_audio_ports_rescan,
    };

    // Host Note Ports support callbacks...
    static uint32_t host_note_ports_supported_dialects (
            const clap_host *host);
    static void host_note_ports_rescan (
            const clap_host *host, uint32_t flags);

    static const constexpr clap_host_note_ports g_host_note_ports = {
            host_note_ports_supported_dialects,
            host_note_ports_rescan,
    };

    // Host Latency callbacks...
    static void host_latency_changed (
            const clap_host *host);

    static const constexpr clap_host_latency g_host_latency = {
            host_latency_changed,
    };

    // Host thread-check callbacks...
    static bool host_is_main_thread (
            const clap_host *host);
    static bool host_is_audio_thread (
            const clap_host *host);

    static const constexpr clap_host_thread_check g_host_thread_check = {
            host_is_main_thread,
            host_is_audio_thread,
    };

    bool is_main_thread();
    bool is_audio_thread();

    void plugin_latency_changed ();

    bool clapRegisterTimer(const uint32_t periodInMs, clap_id* const timerId);
    bool clapUnregisterTimer(const clap_id timerId);

    bool plugin_instances ( unsigned int );
    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};

#endif  // CLAP_SUPPORT 

