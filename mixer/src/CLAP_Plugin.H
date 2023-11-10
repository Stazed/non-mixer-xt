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

#include "Mixer_Strip.H"
#include "Plugin_Module.H"
#include "LinkedList.hpp"

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

    clap_host _host;
    clap_process _process;

    bool _is_processing;
    bool _activated;

    // GUI Editor stuff...
    bool m_bEditorCreated;
    bool m_bEditorVisible;

  /* X window for custom plugin ui */
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

    const clap_plugin_note_name *m_note_names;

    std::unordered_map<unsigned long, clap_id> m_param_ids; // FIXME we not using this
    std::unordered_map<clap_id, const clap_param_info *> m_param_infos;

    void init_x();
    bool isUiResizable() const;
    void show_custom_ui();
    void hide_custom_ui();

    Window getChildWindow() const;
    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild);

    void custom_update_ui_x();
    static void custom_update_ui ( void * );

    int _midi_ins;
    int _midi_outs;

    int _control_ins;
    int _control_outs;

public:
    const clap_plugin_entry_t *entry_from_CLAP_file(const char *f);
    
    static void setup_host( clap_host *host, void *host_data );
    static const void *get_extension(const struct clap_host * host, const char *eid);
    static void request_restart(const struct clap_host * host);
    static void request_process(const struct clap_host * host);
    static void request_callback(const struct clap_host * host);

    // Parameters info/ids (de)initializer.
    void addParamInfos();
    void clearParamInfos();

    // Set/add a parameter value/point.
    void setParameter (clap_id id, double alue);

    // Get current parameter value.
    double getParameter (clap_id id) const;

    class EventList
    {
    public:

            EventList ( uint32_t nsize = 1024, uint32_t ncapacity = 8 )
                    : m_nsize(0), m_eheap(nullptr),
                            m_ehead(nullptr), m_etail(nullptr), m_ihead(0)
            {
                    resize(nsize);

                    m_elist.reserve(ncapacity);

                    ::memset(&m_ins, 0, sizeof(m_ins));
                    m_ins.ctx  = this;
                    m_ins.size = &events_in_size;
                    m_ins.get  = &events_in_get;

                    ::memset(&m_outs, 0, sizeof(m_outs));
                    m_outs.ctx = this;
                    m_outs.try_push = &events_out_push;
            }

            ~EventList () { resize(0); }

            const clap_input_events *ins () const
                    { return &m_ins; }
            const clap_output_events *outs () const
                    { return &m_outs; }

            bool push ( const clap_event_header *eh )
            {
                    const uint32_t ntail = m_etail - m_eheap;
                    const uint32_t nsize = ntail + eh->size;
                    if (m_nsize < nsize)
                            resize(nsize << 1);
                    const uint32_t ncapacity = m_elist.capacity();
                    if (m_elist.size() >= ncapacity)
                            m_elist.reserve(ncapacity << 1);
                    m_elist.push_back(ntail);
                    ::memcpy(m_etail, eh, eh->size);
                    m_etail += eh->size;

                    return true;
            }

            const clap_event_header *get ( uint32_t index ) const
            {
                    const clap_event_header *ret = nullptr;
                    if (index + m_ihead < m_elist.size()) {
                            ret = reinterpret_cast<const clap_event_header *> (
                                    m_eheap + m_elist.at(index + m_ihead));
                    }
                    return ret;
            }

            const clap_event_header *pop ()
            {
                    const clap_event_header *ret = nullptr;
                    if (m_ihead < m_elist.size() && m_ehead < m_etail) {
                            ret = reinterpret_cast<const clap_event_header *> (m_ehead);
                            m_ehead += ret->size;
                            ++m_ihead;
                    }
                    else clear();
                    return ret;
            }

            size_t size () const
                    { return m_elist.size() - m_ihead; }

            bool empty () const
                    { return (m_etail == m_ehead); }

            void clear ()
            {
                    m_ehead = m_eheap;
                    m_etail = m_ehead;
                    m_ihead = 0;
                    m_elist.clear();
            }

    protected:

            void resize ( uint32_t nsize )
            {
                    uint8_t *old_eheap = m_eheap;
                    uint8_t *old_ehead = m_ehead;
                    uint8_t *old_etail = m_etail;
                    m_eheap = nullptr;
                    m_ehead = m_eheap;
                    m_etail = m_ehead;
                    m_nsize = nsize;
                    if (m_nsize > 0) {
                            m_eheap = new uint8_t [m_nsize];
                            m_ehead = m_eheap;
                            m_etail = m_ehead;
                            if (old_etail > old_ehead) {
                                    const uint32_t ntail
                                            = old_etail - old_ehead;
                                    ::memcpy(m_ehead, old_ehead, ntail);
                                    m_etail += ntail;
                            }
                    }
                    if (old_eheap)
                            delete [] old_eheap;
            }

            static uint32_t events_in_size (
                    const clap_input_events *ins )
            {
                    const EventList *elist
                            = static_cast<const EventList *> (ins->ctx);
                    return elist->size();
            }

            static const clap_event_header_t *events_in_get (
                    const struct clap_input_events *ins, uint32_t index )
            {
                    const EventList *elist
                            = static_cast<const EventList *> (ins->ctx);
                    return elist->get(index);
            }

            static bool events_out_push (
                    const clap_output_events *outs,
                    const clap_event_header *eh )
            {
                    EventList *elist = static_cast<EventList *> (outs->ctx);
                    return elist->push(eh);
            }

    private:

            uint32_t m_nsize;
            uint8_t *m_eheap;
            uint8_t *m_ehead;
            uint8_t *m_etail;
            uint32_t m_ihead;

            std::vector<uint32_t> m_elist;

            clap_input_events  m_ins;
            clap_output_events m_outs;
    };

    // Events processor buffers.
    EventList m_events_in;
    EventList m_events_out;

    // Parameters processor queue.
    EventList m_params_out;
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

protected:

    // Transfer parameter changes...
    void process_params_out ();

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

    static bool host_register_timer (
            const clap_host *host, uint32_t period_ms, clap_id *timer_id);
    static bool host_unregister_timer (
            const clap_host *host, clap_id timer_id);

    static const constexpr clap_host_timer_support g_host_timer_support = {
            host_register_timer,
            host_unregister_timer,
    };

    bool clapRegisterTimer(const uint32_t periodInMs, clap_id* const timerId);
    bool clapUnregisterTimer(const clap_id timerId);

    bool plugin_instances ( unsigned int );
    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};

#endif  // CLAP_SUPPORT 

