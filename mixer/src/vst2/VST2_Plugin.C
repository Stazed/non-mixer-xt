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


VST2_Plugin::VST2_Plugin() :
    Plugin_Module(),
    m_sFilename(),
    m_iUniqueID(0),
    m_pLibrary(nullptr),
    m_pEffect(nullptr),
    m_iFlagsEx(0),
    m_iControlIns(0),
    m_iControlOuts(0),
    m_iAudioIns(0),
    m_iAudioOuts(0),
    m_iMidiIns(0),
    m_iMidiOuts(0),
    m_bRealtime(false),
    m_bConfigure(false),
    m_bEditor(false)
{
    _plug_type = Type_VST2;

    log_create();
}

VST2_Plugin::~VST2_Plugin()
{
    log_destroy();

    g_vst2Plugins.erase (m_pEffect);    // erasing by key

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

    std::pair<AEffect *, VST2_Plugin *> efct ( m_pEffect, this );
    g_vst2Plugins.insert(efct);
    
    initialize_plugin();

    return false;
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
        label(szName);
         //   m_sName = szName;
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
//    m_iFlagsEx = 0;
//    m_bEditor  = false;
//    m_sName.clear();
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
	static VstTimeInfo s_vst2TimeInfo;
//	qtractorSession *pSession = qtractorSession::getInstance();

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
                // FIXME
	//	ret = (VstIntPtr) g_iVst2ShellCurrentId;
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
		DMESSAGE("audioMasterGetTime");
#if 0
		::memset(&s_vst2TimeInfo, 0, sizeof(s_vst2TimeInfo));
		if (pSession) {
			qtractorAudioEngine *pAudioEngine = pSession->audioEngine();
			if (pAudioEngine) {
				const qtractorAudioEngine::TimeInfo& timeInfo
					= pAudioEngine->timeInfo();
				s_vst2TimeInfo.samplePos = double(timeInfo.frame);
				s_vst2TimeInfo.sampleRate = double(timeInfo.sampleRate);
				s_vst2TimeInfo.flags = 0;
				if (timeInfo.playing)
					s_vst2TimeInfo.flags |= (kVstTransportChanged | kVstTransportPlaying);
				s_vst2TimeInfo.flags |= kVstPpqPosValid;
				s_vst2TimeInfo.ppqPos = double(timeInfo.beats);
				s_vst2TimeInfo.flags |= kVstBarsValid;
				s_vst2TimeInfo.barStartPos = double(timeInfo.barBeats);
				s_vst2TimeInfo.flags |= kVstTempoValid;
				s_vst2TimeInfo.tempo  = double(timeInfo.tempo);
				s_vst2TimeInfo.flags |= kVstTimeSigValid;
				s_vst2TimeInfo.timeSigNumerator = timeInfo.beatsPerBar;
				s_vst2TimeInfo.timeSigDenominator = timeInfo.beatType;
			}
		}
		ret = (VstIntPtr) &s_vst2TimeInfo;
#endif
		break;

	case audioMasterProcessEvents:
		DMESSAGE("audioMasterProcessEvents");
#if 0
		pVst2Plugin = VST2_Plugin::findPlugin(effect);
		if (pVst2Plugin) {
			qtractorMidiManager *pMidiManager = nullptr;
			qtractorPluginList *pPluginList = pVst2Plugin->list();
			if (pPluginList)
				pMidiManager = pPluginList->midiManager();
			if (pMidiManager) {
				pMidiManager->vst2_events_copy((VstEvents *) ptr);
				ret = 1; // supported and processed.
			}
		}
#endif
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
VST2_Plugin::activate ( void )
{
    vst2_dispatch(effMainsChanged, 0, 1, nullptr, 0.0f);
}

void
VST2_Plugin::deactivate ( void )
{
    vst2_dispatch(effMainsChanged, 0, 0, nullptr, 0.0f);
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
    // FIXME
   // _audio_in_buffers[n] = static_cast<float*>( buf );
}

void
VST2_Plugin::set_output_buffer ( int n, void *buf )
{
    // FIXME
   // _audio_out_buffers[n] = static_cast<float*>( buf );
}

bool
VST2_Plugin::loaded ( void ) const
{
    // FIXME
   // if ( _pModule )
   //     return true;

    return false;
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