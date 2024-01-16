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
 * File:   VST2_Plugin.H
 * Author: sspresto
 *
 * Created on January 13, 2024, 5:36 PM
 */

#pragma once

#ifdef VST2_SUPPORT

#include "vestige/vestige.h"
#include "CarlaLibUtils.hpp"
#include "../Plugin_Module.H"
#include "../x11/XTUtils.H"
#include "../x11/X11PluginUI.H"

typedef struct _AEffect AEffect;

// Maximum pre-allocated events for some VST2
constexpr const uint16_t kPluginMaxMidiEvents = 512;

class VST2_Plugin : public Plugin_Module,
        private X11PluginUI::Callback
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

    LOG_CREATE_FUNC( VST2_Plugin );
    MODULE_CLONE_FUNC( VST2_Plugin );

    std::vector<Port> midi_input;
    std::vector<Port> midi_output;

    VST2_Plugin();
    virtual ~VST2_Plugin();

    // Open/close editor widget.
    bool try_custom_ui();

    // Global VST2 plugin lookup.
    static VST2_Plugin *findPlugin(AEffect *pVst2Effect);

    // Parameter update executive - plugin to host.
    void updateParamValue(unsigned long iIndex, float fValue, bool bUpdate);

    // All parameters update method - plugin to host.
    void updateParamValues(bool bUpdate);

    // Host to plugin
    void setParameter(uint32_t iIndex, float value);

    void idleEditor (void);
    void resizeEditor ( int w, int h );

    int ProcessEvents (void *ptr);
    VstTimeInfo& get_time_info() {return fTimeInfo;}
    unsigned long get_unique_id() {return m_iUniqueID;}

private:

    std::string   m_sFilename;
    unsigned long m_iUniqueID;
    lib_t         m_pLibrary;
    AEffect      *m_pEffect;
    unsigned int  m_iFlagsEx;
    std::string   m_sName;

    uint32_t     fMidiEventCount;
    VstMidiEvent fMidiEvents[kPluginMaxMidiEvents*2];
    VstTimeInfo  fTimeInfo;

    struct FixedVstEvents {
        int32_t numEvents;
        intptr_t reserved;
        VstEvent* data[kPluginMaxMidiEvents*2];

        FixedVstEvents() noexcept
            : numEvents(0),
              reserved(0)
        {
            carla_zeroPointers(data, kPluginMaxMidiEvents*2);
        }

       // CARLA_DECLARE_NON_COPYABLE(FixedVstEvents);
    } fEvents;

    // Cached port counts.
    unsigned short m_iControlIns;
    unsigned short m_iControlOuts;
    unsigned short m_iAudioIns;
    unsigned short m_iAudioOuts;
    unsigned short m_iMidiIns;
    unsigned short m_iMidiOuts;

    VstParameterProperties m_props;

    // Cached flags.
    bool m_bRealtime;
    bool m_bConfigure;
    bool m_bEditor;
    bool _activated;

    uint32_t  _position;        ///< Transport position in frames
    float     _bpm;             ///< Transport tempo in beats per minute
    bool      _rolling;         ///< Transport speed (0=stop, 1=play)

     // GUI Editor stuff...
    bool _bEditorCreated;
    X11PluginUI *_X11_UI;
    bool   _x_is_visible;

    float ** _audio_in_buffers;
    float ** _audio_out_buffers;

    bool open_lib ( const std::string& sFilename );
    void close_lib();

    bool open_descriptor ( unsigned long iIndex );
    void close_descriptor (void);
    bool initialize_plugin();

    int vst2_dispatch(long opcode, long index, long value, void *ptr, float opt) const;

    void create_audio_ports();
    void create_midi_ports();
    void create_control_ports();
    void activate ( void );
    void deactivate ( void );
    void add_port ( const Port &p ) override;
    
    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    bool loaded ( void ) const;

    void process_jack_transport ( uint32_t nframes );
    void process_jack_midi_in ( uint32_t nframes, unsigned int port );
    void process_midi_in (unsigned char *data, unsigned int size,
	unsigned long offset, unsigned short port );

    bool show_custom_ui();
    bool hide_custom_ui();

    void custom_update_ui_x();
    static void custom_update_ui ( void * );

protected:

    void handlePluginUIClosed() override;
    void handlePluginUIResized(const uint width, const uint height) override;
    bool vst2_canDo(const char *pszCanDo) const;
    // Save and restore
    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

};

#endif // VST2_SUPPORT

