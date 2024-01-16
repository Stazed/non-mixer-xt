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
 * File:   VST2_Plugin.C
 * Author: sspresto
 * 
 * Created on January 13, 2024, 5:36 PM
 */

#ifdef VST2_SUPPORT

#include "VST2_Plugin.H"
#include "../../../nonlib/dsp.h"
#include "../Chain.H"
#include "../Mixer_Strip.H"

#if !defined(__WIN32__) && !defined(_WIN32) && !defined(WIN32)
#define __cdecl
#endif

#if !defined(VST_2_3_EXTENSIONS)
typedef int32_t  VstInt32;
typedef intptr_t VstIntPtr;
#define VSTCALLBACK
#endif

typedef AEffect* (*VST_GetPluginInstance) (audioMasterCallback);

static VstIntPtr VSTCALLBACK Vst2Plugin_HostCallback (AEffect* effect,
	VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);

// Dynamic singleton list of VST2 plugins.
static std::map<AEffect *, VST2_Plugin *> g_vst2Plugins;

// Current working VST2 Shell identifier.
static int g_iVst2ShellCurrentId = 0;

// Specific extended flags that saves us
// from calling canDo() in audio callbacks.
enum qtractorVst2PluginFlagsEx
{
    effFlagsExCanSendVstEvents          = 1 << 0,
    effFlagsExCanSendVstMidiEvents      = 1 << 1,
    effFlagsExCanSendVstTimeInfo        = 1 << 2,
    effFlagsExCanReceiveVstEvents       = 1 << 3,
    effFlagsExCanReceiveVstMidiEvents   = 1 << 4,
    effFlagsExCanReceiveVstTimeInfo     = 1 << 5,
    effFlagsExCanProcessOffline         = 1 << 6,
    effFlagsExCanUseAsInsert            = 1 << 7,
    effFlagsExCanUseAsSend              = 1 << 8,
    effFlagsExCanMixDryWet              = 1 << 9,
    effFlagsExCanMidiProgramNames       = 1 << 10
};

// Some VeSTige missing opcodes and flags.
const int effSetProgramName = 4;
const int effGetParamLabel = 6;
const int effGetParamDisplay = 7;
const int effGetChunk = 23;
const int effSetChunk = 24;
const int effGetProgramNameIndexed = 29;
const int effFlagsProgramChunks = 32;

static const int32_t kVstMidiEventSize = static_cast<int32_t>(sizeof(VstMidiEvent));

VST2_Plugin::VST2_Plugin() :
    Plugin_Module(),
    m_sFilename(),
    m_iUniqueID(0),
    m_pLibrary(nullptr),
    m_pEffect(nullptr),
    m_iFlagsEx(0),
    m_sName(),
    fMidiEventCount(0),
    fTimeInfo(),
    fEvents(),
    m_iControlIns(0),
    m_iControlOuts(0),
    m_iAudioIns(0),
    m_iAudioOuts(0),
    m_iMidiIns(0),
    m_iMidiOuts(0),
    m_bRealtime(false),
    m_bConfigure(false),
    m_bEditor(false),
    _activated(false),
    _position(0),
    _bpm(120.0f),
    _rolling(false),
    _audio_in_buffers(nullptr),
    _audio_out_buffers(nullptr)
{
    _plug_type = Type_VST2;

    non_zeroStructs(fMidiEvents, kPluginMaxMidiEvents*2);
    non_zeroStruct(fTimeInfo);

    for (ushort i=0; i < kPluginMaxMidiEvents*2; ++i)
        fEvents.data[i] = (VstEvent*)&fMidiEvents[i];


    log_create();
}

VST2_Plugin::~VST2_Plugin()
{
    log_destroy();

    deactivate();

    g_vst2Plugins.erase (m_pEffect);    // erasing by key

    if ( _audio_in_buffers )
    {
        delete []_audio_in_buffers;
        _audio_in_buffers = nullptr;
    }

    if ( _audio_out_buffers )
    {
        delete []_audio_out_buffers;
        _audio_out_buffers = nullptr;
    }

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].disconnect();
            midi_input[i].jack_port()->shutdown();
            delete midi_input[i].jack_port();
        }
    } 
    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].disconnect();
            midi_output[i].jack_port()->shutdown();
            delete midi_output[i].jack_port();
        }
    }

    midi_output.clear();
    midi_input.clear();

}

bool
VST2_Plugin::load_plugin ( Module::Picked picked )
{
    m_sFilename = picked.s_plug_path;
    m_iUniqueID = picked.unique_id;

    if (!open_lib(m_sFilename))
        return false;

    if(!open_descriptor(0))     // FIXME index
        return false;

    if( m_pEffect->uniqueID != (int) m_iUniqueID)
    {
        DMESSAGE("Incorrect ID SB = %ul: IS = %d", m_iUniqueID, m_pEffect->uniqueID);
        close_descriptor();
        return false;
    }

    base_label(m_sName.c_str());

    std::pair<AEffect *, VST2_Plugin *> efct ( m_pEffect, this );
    g_vst2Plugins.insert(efct);
    
    initialize_plugin();

    create_audio_ports();
    create_midi_ports();
    create_control_ports();
    
    vst2_dispatch(effSetSampleRate, 0, 0, nullptr, float(sample_rate()));
    vst2_dispatch(effSetBlockSize,  0, buffer_size(), nullptr, 0.0f);

    activate();

    return true;
}

bool
VST2_Plugin::configure_inputs ( int n )
{
    /* The synth case - no inputs and JACK module has one */
    if( ninputs() == 0 && n == 1)
    {
        _crosswire = false;
    }
    else if ( ninputs() != n )
    {
        _crosswire = false;

        if ( 1 == n && plugin_ins() > 1 )
        {
            DMESSAGE( "Cross-wiring plugin inputs" );
            _crosswire = true;

            audio_input.clear();

            for ( int i = n; i--; )
                audio_input.push_back( Port( this, Port::INPUT, Port::AUDIO ) );
        }

        else if ( n == plugin_ins() )
        {
            DMESSAGE( "Plugin input configuration is a perfect match" );
        }
        else
        {
            DMESSAGE( "Unsupported input configuration" );
            return false;
        }
    }

    return true;

}

void
VST2_Plugin::handle_port_connection_change ( void )
{
    if ( loaded() )
    {
        if ( _crosswire )
        {
            for ( int i = 0; i < plugin_ins(); ++i )
                set_input_buffer( i, audio_input[0].buffer() );
        }
        else
        {
            for ( unsigned int i = 0; i < audio_input.size(); ++i )
                set_input_buffer( i, audio_input[i].buffer() );
        }

        for ( unsigned int i = 0; i < audio_output.size(); ++i )
            set_output_buffer( i, audio_output[i].buffer() );
    }
}

void
VST2_Plugin::handle_chain_name_changed ( void )
{
    Module::handle_chain_name_changed();

    if ( ! chain()->strip()->group()->single() )
    {
        for ( unsigned int i = 0; i < midi_input.size(); i++ )
        {
            if(!(midi_input[i].type() == Port::MIDI))
                continue;

            if(midi_input[i].jack_port())
            {
                midi_input[i].jack_port()->trackname( chain()->name() );
                midi_input[i].jack_port()->rename();
            }
        }
        for ( unsigned int i = 0; i < midi_output.size(); i++ )
        {
            if(!(midi_output[i].type() == Port::MIDI))
                continue;

            if(midi_output[i].jack_port())
            {
                midi_output[i].jack_port()->trackname( chain()->name() );
                midi_output[i].jack_port()->rename();
            }
        }
    }
}

void
VST2_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{

}

void
VST2_Plugin::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );
}

void
VST2_Plugin::bypass ( bool v )
{
    // FIXME CHECK
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

void
VST2_Plugin::freeze_ports ( void )
{
    Module::freeze_ports();

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].jack_port()->freeze();
            midi_input[i].jack_port()->shutdown();
        }
    }

    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].jack_port()->freeze();
            midi_output[i].jack_port()->shutdown();
        }
    } 
}

void
VST2_Plugin::thaw_ports ( void )
{
    Module::thaw_ports();

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for ( unsigned int i = 0; i < midi_input.size(); ++i )
    {   
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        if(midi_input[i].jack_port())
        {
            midi_input[i].jack_port()->client( chain()->client() );
            midi_input[i].jack_port()->trackname( trackname );
            midi_input[i].jack_port()->thaw();
        }
    }

    for ( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        if(midi_output[i].jack_port())
        {
            midi_output[i].jack_port()->client( chain()->client() );
            midi_output[i].jack_port()->trackname( trackname );
            midi_output[i].jack_port()->thaw();
        }
    }
}

void
VST2_Plugin::configure_midi_inputs ()
{
    if(!midi_input.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < midi_input.size(); ++i )
    {
        if(!(midi_input[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += midi_input[i].name();

        DMESSAGE("CONFIGURE MIDI INPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Input, JACK::Port::MIDI );
        midi_input[i].jack_port(jack_port);

        if( !midi_input[i].jack_port()->activate() )
        {
            delete midi_input[i].jack_port();
            midi_input[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI IN port" );
            return;
        }
    }
}

void
VST2_Plugin::configure_midi_outputs ()
{
    if(!midi_output.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < midi_output.size(); ++i )
    {
        if(!(midi_output[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += midi_output[i].name();

        DMESSAGE("CONFIGURE MIDI OUTPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Output, JACK::Port::MIDI );
        midi_output[i].jack_port(jack_port);

        if( !midi_output[i].jack_port()->activate() )
        {
            delete midi_output[i].jack_port();
            midi_output[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI OUT port" );
            return;
        }
    }
}

nframes_t
VST2_Plugin::get_module_latency ( void ) const
{
    return 0;   // FIXME
}

void
VST2_Plugin::process ( nframes_t nframes )
{
    handle_port_connection_change();

    if ( unlikely( bypass() ) )
    {
        /* If this is a mono to stereo plugin, then duplicate the input channel... */
        /* There's not much we can do to automatically support other configurations. */
        if ( ninputs() == 1 && noutputs() == 2 )
        {
            buffer_copy( static_cast<sample_t*>( audio_output[1].buffer() ),
                    static_cast<sample_t*>( audio_input[0].buffer() ), nframes );
        }

        _latency = 0;
    }
    else
    {
        if (m_pEffect == nullptr)
            return;

        process_jack_transport( nframes );

        fMidiEventCount = 0;
        non_zeroStructs(fMidiEvents, kPluginMaxMidiEvents*2);

        for( unsigned int i = 0; i < midi_input.size(); ++i )
        {
            /* JACK MIDI in to plugin MIDI in */
            process_jack_midi_in( nframes, i );
        }

        if (fMidiEventCount > 0)
        {
            fEvents.numEvents = static_cast<int32_t>(fMidiEventCount);
            fEvents.reserved  = 0;
            vst2_dispatch(effProcessEvents, 0, 0, &fEvents, 0.0f);
        }

        // Make it run audio...
        if (m_pEffect->flags & effFlagsCanReplacing)
        {
            m_pEffect->processReplacing(
                m_pEffect, _audio_in_buffers,  _audio_out_buffers, nframes);
        }

        fTimeInfo.samplePos += nframes;
    }
}

bool
VST2_Plugin::try_custom_ui()
{
    return false;   // FIXME
}

bool
VST2_Plugin::open_lib ( const std::string& sFilename )
{
    close_lib();

    m_pLibrary = lib_open(sFilename.c_str());

    if (m_pLibrary == nullptr)
    {
        DMESSAGE("Cannot Open %s", sFilename.c_str());
        return false;
    }

    DMESSAGE("Open %s", sFilename.c_str());

    return true;
}

void
VST2_Plugin::close_lib()
{
    if (m_pLibrary == nullptr)
        return;

    DMESSAGE("close()");

    vst2_dispatch(effClose, 0, 0, 0, 0.0f);

    m_pLibrary = nullptr;
}

bool
VST2_Plugin::open_descriptor ( unsigned long iIndex )
{
    if (m_pLibrary == nullptr)
        return false;

    close_descriptor();

    DMESSAGE("open_descriptor - iIndex = (%lu)",  iIndex);

    VST_GetPluginInstance pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>(m_pLibrary, "VSTPluginMain");

    if (pfnGetPluginInstance == nullptr)
        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> (m_pLibrary, "main");

    if (pfnGetPluginInstance == nullptr)
    {
        DMESSAGE("error", "Not a VST plugin");
        return false;
    }

    m_pEffect = (*pfnGetPluginInstance)(Vst2Plugin_HostCallback);

    if (m_pEffect == nullptr)
    {
        DMESSAGE("plugin instance could not be created.");
        return false;
    }

    // Did VST plugin instantiated OK?
    if (m_pEffect->magic != kEffectMagic)
    {
        DMESSAGE("Plugin is not a valid VST.");
        m_pEffect = nullptr;
        return false;
    }

    // Check whether it's a VST Shell...
    const int categ = vst2_dispatch(effGetPlugCategory, 0, 0, nullptr, 0.0f);
    if (categ == kPlugCategShell)
    {
        int id = 0;
        char buf[40];
        unsigned long i = 0;
        for ( ; iIndex >= i; ++i)
        {
            buf[0] = (char) 0;
            id = vst2_dispatch(effShellGetNextPlugin, 0, 0, (void *) buf, 0.0f);
            if (id == 0 || !buf[0])
                break;
        }
        // Check if we're actually the intended plugin...
        if (i < iIndex || id == 0 || !buf[0])
        {
            DMESSAGE("vst2_shell(%lu) plugin is not a valid VST.", iIndex);
            m_pEffect = nullptr;
            return false;
        }
        // Make it known...
        g_iVst2ShellCurrentId = id;
        // Re-allocate the thing all over again...

        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>(m_pLibrary, "VSTPluginMain");

        if (pfnGetPluginInstance == nullptr)
            pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> (m_pLibrary, "main");

        if (pfnGetPluginInstance == nullptr)
        {
            DMESSAGE("error", "Not a VST plugin");
            m_pEffect = nullptr;
            return false;
	}

        m_pEffect = (*pfnGetPluginInstance)(Vst2Plugin_HostCallback);

        // Not needed anymore, hopefully...
        g_iVst2ShellCurrentId = 0;
        // Don't go further if failed...
        if (m_pEffect == nullptr)
        {
            DMESSAGE("vst2_shell(%lu) plugin instance could not be created.", iIndex);
            return false;
        }

        if (m_pEffect->magic != kEffectMagic)
        {
            DMESSAGE("vst2_shell(%lu) plugin is not a valid VST.",  iIndex);
            m_pEffect = nullptr;
            return false;
        }

        DMESSAGE( "vst2_shell(%lu) id=0x%x name=\"%s\"",  i, id, buf);
    }
    else
    // Not a VST Shell plugin...
    if (iIndex > 0)
    {
        m_pEffect = nullptr;
        return false;
    }

//	vst2_dispatch(effIdentify, 0, 0, nullptr, 0.0f);
    vst2_dispatch(effOpen,     0, 0, nullptr, 0.0f);

    // Get label name...
    char szName[256]; ::memset(szName, 0, sizeof(szName));
    if (vst2_dispatch(effGetEffectName, 0, 0, (void *) szName, 0.0f))
    {
        m_sName = szName;
    }

#if 0
    // Specific inquiries...
    m_iFlagsEx = 0;

    if (vst2_canDo("sendVstEvents"))       m_iFlagsEx |= effFlagsExCanSendVstEvents;
    if (vst2_canDo("sendVstMidiEvent"))    m_iFlagsEx |= effFlagsExCanSendVstMidiEvents;
    if (vst2_canDo("sendVstTimeInfo"))     m_iFlagsEx |= effFlagsExCanSendVstTimeInfo;
    if (vst2_canDo("receiveVstEvents"))    m_iFlagsEx |= effFlagsExCanReceiveVstEvents;
    if (vst2_canDo("receiveVstMidiEvent")) m_iFlagsEx |= effFlagsExCanReceiveVstMidiEvents;
    if (vst2_canDo("receiveVstTimeInfo"))  m_iFlagsEx |= effFlagsExCanReceiveVstTimeInfo;
    if (vst2_canDo("offline"))             m_iFlagsEx |= effFlagsExCanProcessOffline;
    if (vst2_canDo("plugAsChannelInsert")) m_iFlagsEx |= effFlagsExCanUseAsInsert;
    if (vst2_canDo("plugAsSend"))          m_iFlagsEx |= effFlagsExCanUseAsSend;
    if (vst2_canDo("mixDryWet"))           m_iFlagsEx |= effFlagsExCanMixDryWet;
    if (vst2_canDo("midiProgramNames"))    m_iFlagsEx |= effFlagsExCanMidiProgramNames;

    m_bEditor = (m_pEffect->flags & effFlagsHasEditor);
#endif
    return true;
}


// Plugin unloader.
void
VST2_Plugin::close_descriptor (void)
{
    if (m_pEffect == nullptr)
        return;

    DMESSAGE("close_descriptor()");

    vst2_dispatch(effClose, 0, 0, 0, 0.0f);

    m_pEffect  = nullptr;
    m_iFlagsEx = 0;
    m_bEditor  = false;
}

// VST2 flag inquirer.
bool
VST2_Plugin::vst2_canDo ( const char *pszCanDo ) const
{
    return (vst2_dispatch(effCanDo, 0, 0, (void *) pszCanDo, 0.0f) > 0);
}


bool
VST2_Plugin::initialize_plugin()
{
    // Specific inquiries...
    m_iFlagsEx = 0;
//	if (vst2_canDo("sendVstEvents"))       m_iFlagsEx |= effFlagsExCanSendVstEvents;
    if (vst2_canDo("sendVstMidiEvent"))    m_iFlagsEx |= effFlagsExCanSendVstMidiEvents;
//	if (vst2_canDo("sendVstTimeInfo"))     m_iFlagsEx |= effFlagsExCanSendVstTimeInfo;
//	if (vst2_canDo("receiveVstEvents"))    m_iFlagsEx |= effFlagsExCanReceiveVstEvents;
    if (vst2_canDo("receiveVstMidiEvent")) m_iFlagsEx |= effFlagsExCanReceiveVstMidiEvents;
//	if (vst2_canDo("receiveVstTimeInfo"))  m_iFlagsEx |= effFlagsExCanReceiveVstTimeInfo;
//	if (vst2_canDo("offline"))             m_iFlagsEx |= effFlagsExCanProcessOffline;
//	if (vst2_canDo("plugAsChannelInsert")) m_iFlagsEx |= effFlagsExCanUseAsInsert;
//	if (vst2_canDo("plugAsSend"))          m_iFlagsEx |= effFlagsExCanUseAsSend;
//	if (vst2_canDo("mixDryWet"))           m_iFlagsEx |= effFlagsExCanMixDryWet;
//	if (vst2_canDo("midiProgramNames"))    m_iFlagsEx |= effFlagsExCanMidiProgramNames;

    // Compute and cache port counts...
    m_iControlIns  = m_pEffect->numParams;
    m_iControlOuts = 0;
    m_iAudioIns    = m_pEffect->numInputs;
    m_iAudioOuts   = m_pEffect->numOutputs;
    m_iMidiIns     = ((m_iFlagsEx & effFlagsExCanReceiveVstMidiEvents)
            || (m_pEffect->flags & effFlagsIsSynth) ? 1 : 0);
    m_iMidiOuts    = ((m_iFlagsEx & effFlagsExCanSendVstMidiEvents) ? 1 : 0);

    // Cache flags.
    m_bRealtime  = true;
    m_bConfigure = (m_pEffect->flags & effFlagsProgramChunks);
    m_bEditor    = (m_pEffect->flags & effFlagsHasEditor);

    // HACK: Some native VST2 plugins with a GUI editor
    // need to skip explicit shared library unloading,
    // on close, in order to avoid mysterious crashes
    // later on session and/or application exit.
  //  if (m_bEditor) file()->setAutoUnload(false);

    return true;
}


// VST host dispatcher.
int VST2_Plugin::vst2_dispatch (
	long opcode, long index, long value, void *ptr, float opt ) const
{
    if (m_pEffect == nullptr)
            return 0;

 //   DMESSAGE("vst2_dispatch(%ld, %ld, %ld, %p, %g)",
 //           opcode, index, value, ptr, opt);

    return m_pEffect->dispatcher(m_pEffect, opcode, index, value, ptr, opt);
}

// Parameter update executive.
void
VST2_Plugin::updateParamValue (
	unsigned long iIndex, float fValue, bool bUpdate )
{
    // FIXME
}

// All parameters update method.
void
VST2_Plugin::updateParamValues ( bool bUpdate )
{
#if 0
    int nupdate = 0;

    // Make sure all cached parameter values are in sync
    // with plugin parameter values; update cache otherwise.
    AEffect *pVst2Effect = vst2_effect(0);
    if (pVst2Effect) {
            const qtractorPlugin::Params& params = qtractorPlugin::params();
            qtractorPlugin::Params::ConstIterator param = params.constBegin();
            const qtractorPlugin::Params::ConstIterator param_end = params.constEnd();
            for ( ; param != param_end; ++param) {
                    qtractorPlugin::Param *pParam = param.value();
                    const float fValue
                            = pVst2Effect->getParameter(pVst2Effect, pParam->index());
                    if (pParam->value() != fValue) {
                            pParam->setValue(fValue, bUpdate);
                            ++nupdate;
                    }
            }
    }

    if (nupdate > 0)
            updateFormDirtyCount();
#endif
}

// Host to plugin
void
VST2_Plugin::setParameter(uint32_t iIndex, float value)
{
    if (m_pEffect)
    {
        m_pEffect->setParameter(m_pEffect, iIndex, value);
    }
}

void
VST2_Plugin::idleEditor (void)
{
    // FIXME
}

// Our own editor widget size accessor.
void
VST2_Plugin::resizeEditor ( int w, int h )
{
    // FIXME
}



// Global VST2 plugin lookup.
VST2_Plugin *VST2_Plugin::findPlugin ( AEffect *pVst2Effect )
{
    std::map<AEffect *, VST2_Plugin *>::const_iterator got
                    = g_vst2Plugins.find (pVst2Effect);

    if (got == g_vst2Plugins.end())
        return nullptr;
    else
        return got->second;
}

static VstIntPtr VSTCALLBACK Vst2Plugin_HostCallback ( AEffect *effect,
	VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt )
{
	VstIntPtr ret = 0;
	VST2_Plugin *pVst2Plugin = nullptr;
//	static VstTimeInfo s_vst2TimeInfo;

	switch (opcode) {

	// VST 1.0 opcodes...
	case audioMasterVersion:
		DMESSAGE("audioMasterVersion");
		ret = 2; // vst2.x
		break;

	case audioMasterAutomate:
		DMESSAGE("audioMasterAutomate");
		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
			pVst2Plugin->updateParamValue(index, opt, false);
		}
		break;

	case audioMasterCurrentId:
		DMESSAGE("audioMasterCurrentId");
                pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
                    ret = (VstIntPtr)pVst2Plugin->get_unique_id();
                }

		break;

	case audioMasterIdle:
		DMESSAGE("audioMasterIdle");
		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
			pVst2Plugin->updateParamValues(false);
			pVst2Plugin->idleEditor();
		//	QApplication::processEvents();
		}
		break;

	case audioMasterGetTime:
	//	DMESSAGE("audioMasterGetTime");
                pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin)
                {
                    VstTimeInfo& fTimeInfo = pVst2Plugin->get_time_info();
                    ret = (intptr_t)&fTimeInfo;
                }

		break;

	case audioMasterProcessEvents:
		DMESSAGE("audioMasterProcessEvents");

                pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin)
                {
                    ret = pVst2Plugin->ProcessEvents(ptr);
                }
                break;

	case audioMasterIOChanged:
		DMESSAGE("audioMasterIOChanged");
		break;

	case audioMasterSizeWindow:
		DMESSAGE("audioMasterSizeWindow");
		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
			pVst2Plugin->resizeEditor(int(index), int(value));
			ret = 1; // supported.
		}
		break;

	case audioMasterGetSampleRate:
		DMESSAGE("audioMasterGetSampleRate");

		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin)
                {
                    ret = (VstIntPtr)pVst2Plugin->sample_rate();
		}

		break;

	case audioMasterGetBlockSize:
		DMESSAGE("audioMasterGetBlockSize");

		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin)
                {
                    ret = (VstIntPtr) pVst2Plugin->buffer_size();
		}

		break;

	case audioMasterGetInputLatency:
		DMESSAGE("audioMasterGetInputLatency");
		break;

	case audioMasterGetOutputLatency:
		DMESSAGE("audioMasterGetOutputLatency");
		break;

	case audioMasterGetCurrentProcessLevel:
	//	DMESSAGE("audioMasterGetCurrentProcessLevel");
		break;

	case audioMasterGetAutomationState:
		DMESSAGE("audioMasterGetAutomationState");
		ret = 1; // off.
		break;

#if !defined(VST_2_3_EXTENSIONS) 
	case audioMasterGetSpeakerArrangement:
		DMESSAGE("audioMasterGetSpeakerArrangement");
		break;
#endif

	case audioMasterGetVendorString:
		DMESSAGE("audioMasterGetVendorString");
		//::strcpy((char *) ptr, QTRACTOR_DOMAIN);
                ::strcpy((char *) ptr, WEBSITE);
		ret = 1; // ok.
		break;

	case audioMasterGetProductString:
		DMESSAGE("audioMasterGetProductString");
                //::strcpy((char *) ptr, QTRACTOR_TITLE);
		::strcpy((char *) ptr, PACKAGE);
		ret = 1; // ok.
		break;

	case audioMasterGetVendorVersion:
		DMESSAGE("audioMasterGetVendorVersion");
		break;

	case audioMasterVendorSpecific:
		DMESSAGE("audioMasterVendorSpecific");
		break;

	case audioMasterCanDo:
		DMESSAGE("audioMasterCanDo");
		if (::strcmp("receiveVstMidiEvent", (char *) ptr) == 0 ||
			::strcmp("sendVstMidiEvent",    (char *) ptr) == 0 ||
			::strcmp("sendVstTimeInfo",     (char *) ptr) == 0 ||
			::strcmp("midiProgramNames",    (char *) ptr) == 0 ||
			::strcmp("sizeWindow",          (char *) ptr) == 0) {
			ret = 1; // can do.
		}
		break;

	case audioMasterGetLanguage:
		DMESSAGE("audioMasterGetLanguage");
		ret = (VstIntPtr) kVstLangEnglish;
		break;

#if 0 // !VST_FORCE_DEPRECATED
	case audioMasterPinConnected:
		VST2_HC_DEBUG("audioMasterPinConnected");
		break;

	// VST 2.0 opcodes...
	case audioMasterWantMidi:
		VST2_HC_DEBUG("audioMasterWantMidi");
		break;

	case audioMasterSetTime:
		VST2_HC_DEBUG("audioMasterSetTime");
		break;

	case audioMasterTempoAt:
		VST2_HC_DEBUG("audioMasterTempoAt");
		if (pSession)
			ret = (VstIntPtr) (pSession->tempo() * 10000.0f);
		break;

	case audioMasterGetNumAutomatableParameters:
		VST2_HC_DEBUG("audioMasterGetNumAutomatableParameters");
		break;

	case audioMasterGetParameterQuantization:
		VST2_HC_DEBUG("audioMasterGetParameterQuantization");
		ret = 1; // full single float precision
		break;

	case audioMasterNeedIdle:
		VST2_HC_DEBUG("audioMasterNeedIdle");
		break;

	case audioMasterGetPreviousPlug:
		VST2_HC_DEBUG("audioMasterGetPreviousPlug");
		break;

	case audioMasterGetNextPlug:
		VST2_HC_DEBUG("audioMasterGetNextPlug");
		break;

	case audioMasterWillReplaceOrAccumulate:
		VST2_HC_DEBUG("audioMasterWillReplaceOrAccumulate");
		ret = 1;
		break;

	case audioMasterSetOutputSampleRate:
		VST2_HC_DEBUG("audioMasterSetOutputSampleRate");
		break;

	case audioMasterSetIcon:
		VST2_HC_DEBUG("audioMasterSetIcon");
		break;

	case audioMasterOpenWindow:
		VST2_HC_DEBUG("audioMasterOpenWindow");
		break;

	case audioMasterCloseWindow:
		VST2_HC_DEBUG("audioMasterCloseWindow");
		break;
#endif

	case audioMasterGetDirectory:
		DMESSAGE("audioMasterGetDirectory");
		break;

	case audioMasterUpdateDisplay:
		DMESSAGE("audioMasterUpdateDisplay");
		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
			pVst2Plugin->updateParamValues(false);
		//	QApplication::processEvents();
			ret = 1; // supported.
		}
		break;

	case audioMasterBeginEdit:
		DMESSAGE("audioMasterBeginEdit");
		break;

	case audioMasterEndEdit:
		DMESSAGE("audioMasterEndEdit");
		break;

	default:
		DMESSAGE("audioMasterUnknown");
		break;
	}

	return ret;
}

void
VST2_Plugin::create_audio_ports()
{
    _plugin_ins = 0;
    _plugin_outs = 0;
    for (int32_t i = 0; i < m_iAudioIns; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::AUDIO, "input" ) );
        audio_input[i].hints.plug_port_index = i;
        _plugin_ins++;
    }

    for (int32_t i = 0; i < m_iAudioOuts; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::AUDIO, "output" ) );
        audio_output[i].hints.plug_port_index = i;
        _plugin_outs++;
    }

    _audio_in_buffers = new float * [_plugin_ins]();
    _audio_out_buffers = new float * [_plugin_outs]();

    MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);
}

void
VST2_Plugin::create_midi_ports()
{
    for (int32_t i = 0; i < m_iMidiIns; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::MIDI, "midi_in" ) );
    }

    for (int32_t i = 0; i < m_iMidiOuts; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::MIDI, "midi_out" ) );
    }

    MESSAGE( "Plugin has %i MIDI ins and %i MIDI outs", m_iMidiIns, m_iMidiOuts);
}

void
VST2_Plugin::create_control_ports()
{
    for (unsigned long iIndex = 0; iIndex < m_iControlIns; ++iIndex)
    {
        Port::Direction d = Port::INPUT;

        ::memset(&m_props, 0, sizeof(m_props));

        if (vst2_dispatch(effGetParameterProperties, iIndex, 0, (void *) &m_props, 0.0f))
        {
            Port p( this, d, Port::CONTROL, strdup( m_props.label ) );

            /* Used for OSC path creation unique symbol */
            std::string osc_symbol = strdup( m_props.shortLabel );
            osc_symbol.erase(std::remove(osc_symbol.begin(), osc_symbol.end(), ' '), osc_symbol.end());
            osc_symbol += std::to_string( iIndex );

            p.set_symbol(osc_symbol.c_str());

            p.hints.ranged = true;
            p.hints.minimum = (float) 0.0;
            p.hints.maximum = (float) 1.0;

#if 0
            DMESSAGE("  VstParamProperties(%lu) {", iIndex);
            DMESSAGE("    .label                   = \"%s\"", m_props.label);
            DMESSAGE("    .shortLabel              = \"%s\"", m_props.shortLabel);
            DMESSAGE("    .category                = %d", m_props.category);
            DMESSAGE("    .categoryLabel           = \"%s\"", m_props.categoryLabel);
            DMESSAGE("    .minInteger              = %d", int(m_props.minInteger));
            DMESSAGE("    .maxInteger              = %d", int(m_props.maxInteger));
            DMESSAGE("    .stepInteger             = %d", int(m_props.stepInteger));
            DMESSAGE("    .stepFloat               = %g", m_props.stepFloat);

            DMESSAGE("    .smallStepFloat          = %g", m_props.smallStepFloat);
            DMESSAGE("    .largeStepFloat          = %g", m_props.largeStepFloat);
            DMESSAGE("    .largeStepInteger        = %d", int(m_props.largeStepInteger));
            DMESSAGE("    >IsSwitch                = %d", (m_props.flags & kVstParameterIsSwitch ? 1 : 0));
            DMESSAGE("    >UsesIntegerMinMax       = %d", (m_props.flags & kVstParameterUsesIntegerMinMax ? 1 : 0));
            DMESSAGE("    >UsesFloatStep           = %d", (m_props.flags & kVstParameterUsesFloatStep ? 1 : 0));
            DMESSAGE("    >UsesIntStep             = %d", (m_props.flags & kVstParameterUsesIntStep ? 1 : 0));
            DMESSAGE("    >SupportsDisplayIndex    = %d", (m_props.flags & kVstParameterSupportsDisplayIndex ? 1 : 0));
            DMESSAGE("    >SupportsDisplayCategory = %d", (m_props.flags & kVstParameterSupportsDisplayCategory ? 1 : 0));
            DMESSAGE("    >CanRamp                 = %d", (m_props.flags & kVstParameterCanRamp ? 1 : 0));
            DMESSAGE("    .displayIndex            = %d", m_props.displayIndex);
            DMESSAGE("    .numParametersInCategory = %d", m_props.numParametersInCategory);
            DMESSAGE("}");
#endif
            if ((m_props.flags & kVstParameterUsesIntegerMinMax))
            {
                p.hints.type = Port::Hints::INTEGER;
                p.hints.minimum = (float(m_props.minInteger));
                p.hints.maximum = (float(m_props.maxInteger));
            }

            if ((m_props.flags & kVstParameterIsSwitch))
                p.hints.type = Port::Hints::BOOLEAN;

            // ATTN: Set default value as initial one...
            if (m_pEffect)
            {
                p.hints.default_value = (float) m_pEffect->getParameter(m_pEffect, iIndex);
            }

            float *control_value = new float;

            *control_value = p.hints.default_value;

            p.connect_to( control_value );

            p.hints.plug_port_index = iIndex;

            add_port( p );
        }
    }

    if (bypassable()) {
    Port pb( this, Port::INPUT, Port::CONTROL, "dsp/bypass" );
    pb.hints.type = Port::Hints::BOOLEAN;
    pb.hints.ranged = true;
    pb.hints.maximum = 1.0f;
    pb.hints.minimum = 0.0f;
    pb.hints.dimensions = 1;
    pb.hints.visible = false;
    pb.hints.invisible_with_signals = true;
    pb.connect_to( _bypass );
    add_port( pb );
    }
}

void
VST2_Plugin::activate ( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );
    
    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 0.0f;

    if ( ! _activated )
    {
        _activated = true;
        vst2_dispatch(effMainsChanged, 0, 1, nullptr, 0.0f);
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST2_Plugin::deactivate ( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

    if ( _activated )
    {
        _activated = false;
        vst2_dispatch(effMainsChanged, 0, 0, nullptr, 0.0f);
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST2_Plugin::add_port ( const Port &p )
{
    Module::add_port(p);

    if ( p.type() == Port::MIDI && p.direction() == Port::INPUT )
        midi_input.push_back( p );
    else if ( p.type() == Port::MIDI && p.direction() == Port::OUTPUT )
        midi_output.push_back( p );
}

void
VST2_Plugin::set_input_buffer ( int n, void *buf )
{
    _audio_in_buffers[n] = static_cast<float*>( buf );
}

void
VST2_Plugin::set_output_buffer ( int n, void *buf )
{
    _audio_out_buffers[n] = static_cast<float*>( buf );
}

bool
VST2_Plugin::loaded ( void ) const
{
    if ( m_pEffect )
        return true;

    return false;
}

void
VST2_Plugin::process_jack_transport ( uint32_t nframes )
{
    // Get Jack transport position
    jack_position_t pos;
    const bool rolling =
        (chain()->client()->transport_query(&pos) == JackTransportRolling);

    // If transport state is not as expected, then something has changed
    const bool has_bbt = (pos.valid & JackPositionBBT);
    const bool xport_changed =
      (rolling != _rolling || pos.frame != _position ||
       (has_bbt && pos.beats_per_minute != _bpm));
    
    fTimeInfo.flags = 0;

    if ( xport_changed )
    {
        if ( has_bbt )
        {
            const double positionBeats = static_cast<double>(pos.frame)
                            / (sample_rate() * 60 / pos.beats_per_minute);

            const double ppqBar  = static_cast<double>(pos.beats_per_bar) * (pos.bar - 1);

            fTimeInfo.flags |= kVstTransportChanged;
            fTimeInfo.samplePos  = double(pos.frame);
            fTimeInfo.sampleRate = sample_rate();

            // PPQ Pos
            fTimeInfo.ppqPos = positionBeats;
            fTimeInfo.flags |= kVstPpqPosValid;

            // Tempo
            fTimeInfo.tempo  = pos.beats_per_minute;
            fTimeInfo.flags |= kVstTempoValid;

            // Bars
            fTimeInfo.barStartPos = ppqBar;
            fTimeInfo.flags |= kVstBarsValid;

            // Time Signature
            fTimeInfo.timeSigNumerator = static_cast<int32_t>(pos.beats_per_bar + 0.5f);
            fTimeInfo.timeSigDenominator = static_cast<int32_t>(pos.beat_type + 0.5f);
            fTimeInfo.flags |= kVstTimeSigValid;
        }
        else
        {
            // Tempo
            fTimeInfo.tempo = 120.0;
            fTimeInfo.flags |= kVstTempoValid;

            // Time Signature
            fTimeInfo.timeSigNumerator = 4;
            fTimeInfo.timeSigDenominator = 4;
            fTimeInfo.flags |= kVstTimeSigValid;

            // Missing info
            fTimeInfo.ppqPos = 0.0;
            fTimeInfo.barStartPos = 0.0;
        }
    }

    // Update transport state to expected values for next cycle
    _position = rolling ? pos.frame + nframes : pos.frame;
    _bpm      = has_bbt ? pos.beats_per_minute : _bpm;
    _rolling  = rolling;
}

void
VST2_Plugin::process_jack_midi_in ( uint32_t nframes, unsigned int port )
{
    /* Process any MIDI events from jack */
    if ( midi_input[port].jack_port() )
    {
        void *buf = midi_input[port].jack_port()->buffer( nframes );

        for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i)
        {
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, i);

            process_midi_in(ev.buffer, ev.size, ev.time, 0);
        }
    }
}

void
VST2_Plugin::process_midi_in (unsigned char *data, unsigned int size,
	unsigned long offset, unsigned short port )
{
    for (unsigned int i = 0; i < size; ++i)
    {
        // channel status
        const int channel = (data[i] & 0x0f);// + 1;
        const int status  = (data[i] & 0xf0);

        // all system common/real-time ignored
        if (status == 0xf0)
            continue;

        // check data size (#1)
        if (++i >= size)
            break;

        // channel key
        const int key = (data[i] & 0x7f);

        // channel value (normalized)
        const int value = (data[i] & 0x7f);

        // note on
        if (status == 0x90)
        {
            VstMidiEvent& vstMidiEvent(fMidiEvents[fMidiEventCount++]);

            vstMidiEvent.type        = kVstMidiType;
            vstMidiEvent.byteSize    = kVstMidiEventSize;
            vstMidiEvent.midiData[0] = char(status | channel);
            vstMidiEvent.midiData[1] = char(key);
            vstMidiEvent.midiData[2] = char(value);
        }
        else
        // note off
        if (status == 0x80)
        {
            VstMidiEvent& vstMidiEvent(fMidiEvents[fMidiEventCount++]);

            vstMidiEvent.type        = kVstMidiType;
            vstMidiEvent.byteSize    = kVstMidiEventSize;
            vstMidiEvent.midiData[0] = char(status | channel);
            vstMidiEvent.midiData[1] = char(key);
            vstMidiEvent.midiData[2] = char(value);
        }
    }
}

int
VST2_Plugin::ProcessEvents (void *ptr)
{
    if (fMidiEventCount >= kPluginMaxMidiEvents*2-1)
        return 0;

    if (const VstEvents* const vstEvents = (const VstEvents*)ptr)
    {
        for (int32_t i=0; i < vstEvents->numEvents && i < kPluginMaxMidiEvents*2; ++i)
        {
            if (vstEvents->events[i] == nullptr)
                break;

            const VstMidiEvent* const vstMidiEvent((const VstMidiEvent*)vstEvents->events[i]);

            if (vstMidiEvent->type != kVstMidiType)
                continue;

            // reverse-find first free event, and put it there
            for (uint32_t j=(kPluginMaxMidiEvents*2)-1; j >= fMidiEventCount; --j)
            {
                if (fMidiEvents[j].type == 0)
                {
                    std::memcpy(&fMidiEvents[j], vstMidiEvent, sizeof(VstMidiEvent));
                    break;
                }
            }
        }
    }
    return 1;
}

void
VST2_Plugin::get ( Log_Entry &e ) const
{
    // FIXME
}

void
VST2_Plugin::set ( Log_Entry &e )
{
    // FIXME
}

#endif  // VST2_SUPPORT