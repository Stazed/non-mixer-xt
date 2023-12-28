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
 * File:   VST3_Plugin.H
 * Author: sspresto
 *
 * Created on December 20, 2023, 9:24 AM
 */

#pragma once

#ifdef VST3_SUPPORT

#include <unordered_map>

#include "../Mixer_Strip.H"
#include "../Plugin_Module.H"
#include "../x11/X11PluginUI.H"
#include "Vst3_Impl.H"

#include "pluginterfaces/vst/ivsthostapplication.h"
//#include "pluginterfaces/vst/ivstpluginterfacesupport.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

//#include "pluginterfaces/vst/ivstprocesscontext.h"
//#include "pluginterfaces/vst/ivstparameterchanges.h"
//#include "pluginterfaces/vst/ivstmidicontrollers.h"
//#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstunits.h"

//#include "pluginterfaces/gui/iplugview.h"

//#include "pluginterfaces/base/ibstream.h"
//#include "pluginterfaces/base/ipluginbase.h"

using namespace Steinberg;

class VST3_Plugin : public Plugin_Module
{
public:

    virtual bool load_plugin ( Module::Picked picked ) override;

    bool configure_inputs ( int ) override;
    void handle_port_connection_change ( void ) override;
    void handle_chain_name_changed ( void ) override;
    void handle_sample_rate_change ( nframes_t sample_rate ) override;
    void resize_buffers ( nframes_t buffer_size ) override;

    virtual bool bypass ( void ) const override { return *_bypass == 1.0f; }
    virtual void bypass ( bool v ) override;

    void freeze_ports ( void ) override;
    void thaw_ports ( void ) override;
    
    void configure_midi_inputs () override;
    void configure_midi_outputs () override;

    nframes_t get_module_latency ( void ) const override;
    void process ( nframes_t ) override;

    LOG_CREATE_FUNC( VST3_Plugin );
    MODULE_CLONE_FUNC( VST3_Plugin );
    
    std::vector<Port> midi_input;
    std::vector<Port> midi_output;
    
    // Set/add a parameter value/point.
    void setParameter (Vst::ParamID id, Vst::ParamValue value, uint32 offset);
    void set_control_value(unsigned long port_index, float value, bool update_custom_ui);
    
    // Parameter update methods - host to plugin from Module
    void updateParam(Vst::ParamID id, float fValue);
    
    // Parameters update methods - plugin to host
    void updateParamValues(bool update_custom_ui);
    // Get current parameter value.
    Vst::ParamValue getParameter (Vst::ParamID id) const;
    tresult notify (Vst::IMessage *message);
    
    // Open/close editor widget.
    bool try_custom_ui();
    bool init_custom_ui(bool have_timers);
    // Editor controller methods.
    bool openEditor (void);
    void closeEditor (void);

    bool show_custom_ui();
    bool hide_custom_ui();

    void custom_update_ui_x();
    static void custom_update_ui ( void * );
    
    void timer_registered(bool reg) {_timer_registered = reg;}
    void event_handlers_registered(bool reg) {_event_handlers_registered = reg; }

    VST3_Plugin();
    virtual ~VST3_Plugin();
private:

    // Instance variables.
    void *m_module;

    class Handler;  // Forward declaration
    IPtr<Handler> m_handler;

    IPtr<Vst::IComponent> m_component;
    IPtr<Vst::IEditController> m_controller;
    IPtr<Vst::IUnitInfo> m_unitInfos;
    
    Vst::IAudioProcessor *m_processor;

    volatile bool m_processing;

    VST3IMPL::ParamChanges m_params_in;
    VST3IMPL::ParamChanges m_params_out;

    VST3IMPL::EventList m_events_in;
    VST3IMPL::EventList m_events_out;

    // Processor buffers.
    Vst::AudioBusBuffers m_buffers_in;
    Vst::AudioBusBuffers m_buffers_out;

    // Processor data.
    Vst::ProcessData m_process_data;

    std::string _plugin_filename;
    std::string m_sName;

    unsigned short m_iAudioIns;
    unsigned short m_iAudioOuts;
    unsigned short m_iMidiIns;
    unsigned short m_iMidiOuts;
    
    float ** _audio_in_buffers;
    float ** _audio_out_buffers;

    bool _activated;
    bool m_bRealtime;
    bool m_bConfigure;
    bool m_bEditor;
    
    uint32_t  _position;        ///< Transport position in frames
    float     _bpm;             ///< Transport tempo in beats per minute
    bool      _rolling;         ///< Transport speed (0=stop, 1=play)
    
    // GUI Editor stuff...
    bool _bEditorCreated;
    bool _x_is_resizable;
    bool _x_is_visible;
    bool _timer_registered;
    bool _event_handlers_registered;
    IPtr<IPlugView> m_plugView;

    class EditorFrame;
    class RunLoop;
    EditorFrame  *m_pEditorFrame;
    
    // Identififier-parameter map.
    std::unordered_map<int, unsigned long> m_paramIds;
    // Parameter finder (by id).
    unsigned long findParamId ( int id ) const;

    // File loader.
    bool open_file(const std::string& sFilename);
    bool open_descriptor(unsigned long iIndex);

    void close_descriptor();
    
    // Process functions
    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    bool loaded ( void ) const;
    bool process_reset();

    void process_jack_transport ( uint32_t nframes );
    void process_jack_midi_in ( uint32_t nframes, unsigned int port );
    void process_midi_in (unsigned char *data, unsigned int size,
		unsigned long offset, unsigned short port);

    void process_jack_midi_out ( uint32_t nframes, unsigned int port );

    // Initialize create
    void initialize_plugin();
    void clear_plugin();
    int numChannels (Vst::MediaType type, Vst::BusDirection direction ) const;
    
    void create_audio_ports();
    void create_midi_ports();
    void create_control_ports();
    void activate ( void );
    void deactivate ( void );
    void vst3_activate ( Vst::IComponent *component,
	Vst::MediaType type, Vst::BusDirection direction, bool state );
    void add_port ( const Port &p ) override;

    // MIDI controller assignment hash key/map.
    struct MidiMapKey
    {
        MidiMapKey (int16 po = 0, int16 ch = 0, int16 co = 0)
                : port(po), channel(ch), controller(co) {}
        MidiMapKey (const MidiMapKey& key)
                : port(key.port), channel(key.channel), controller(key.controller) {}

        bool operator< (const MidiMapKey& key) const
        {
            if (port != key.port)
                    return (port < key.port);
            else
            if (channel != key.channel)
                    return (channel < key.channel);
            else
                    return (controller < key.controller);
        }

        int16 port;
        int16 channel;
        int16 controller;
    };

    std::map<MidiMapKey, Vst::ParamID> m_midiMap;

protected:

    // Save and restore
    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

};

#endif // VST3_SUPPORT
