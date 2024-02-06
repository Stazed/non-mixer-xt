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
 * File:   VST3_Plugin.C
 * Author: sspresto
 * 
 * Created on December 20, 2023, 9:24 AM
 */

#ifdef VST3_SUPPORT

#include <filesystem>
#include <dlfcn.h>      // dlopen, dlerror, dlsym
#include <unordered_map>
#include <FL/fl_ask.H>  // fl_alert()

#include "../../../nonlib/dsp.h"
#include "EditorFrame.h"
#include "VST3_Plugin.H"
#include "../Chain.H"
#include "Vst3_Discovery.H"
#include "runloop.h"

const unsigned char  EVENT_NOTE_OFF         = 0x80;
const unsigned char  EVENT_NOTE_ON          = 0x90;
const unsigned char  EVENT_CHANNEL_PRESSURE = 0xa0;

const int DEFAULT_MSECS = 33;

using namespace Steinberg;
using namespace Linux;

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::ParamQueue, Vst::IParamValueQueue, Vst::IParamValueQueue::iid)

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::ParamChanges, Vst::IParameterChanges, Vst::IParameterChanges::iid)

IMPLEMENT_FUNKNOWN_METHODS (VST3IMPL::EventList, IEventList, IEventList::iid)

//----------------------------------------------------------------------
// class VST3_Plugin::Handler -- VST3 plugin interface handler.
// Plugin uses this to send messages, update to the host. Plugin to host.

class VST3_Plugin::Handler
    : public Vst::IComponentHandler
    , public Vst::IConnectionPoint
{
public:

    // Constructor.
    Handler (VST3_Plugin *pPlugin)
        : m_pPlugin(pPlugin) { FUNKNOWN_CTOR }

    // Destructor.
    virtual ~Handler () { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IComponentHandler ---
    //
    tresult PLUGIN_API beginEdit (Vst::ParamID id) override
    {
        DMESSAGE("Handler[%p]::beginEdit(%d)", this, int(id));
        return kResultOk;
    }

    tresult PLUGIN_API performEdit (Vst::ParamID id, Vst::ParamValue value) override
    {
        DMESSAGE("Handler[%p]::performEdit(%d, %g)", this, int(id), float(value));

        m_pPlugin->setParameter(id, value, 0);

        unsigned long index = m_pPlugin->findParamId(id);

        // false means don't update custom UI cause that is were it came from
        m_pPlugin->set_control_value( index, value, false );

        return kResultOk;
    }

    tresult PLUGIN_API endEdit (Vst::ParamID id) override
    {
        DMESSAGE("Handler[%p]::endEdit(%d)", this, int(id));
        return kResultOk;
    }

    tresult PLUGIN_API restartComponent (int32 flags) override
    {
        DMESSAGE("Handler[%p]::restartComponent(0x%08x)", this, flags);

        if (flags & Vst::kParamValuesChanged)
        {
            m_pPlugin->updateParamValues(false);
        }
        else
        if (flags & Vst::kReloadComponent)
        {
            m_pPlugin->deactivate();
            m_pPlugin->activate();
        }

        return kResultOk;
    }

    //--- IConnectionPoint ---
    //
    tresult PLUGIN_API connect (Vst::IConnectionPoint *other) override
        { return (other ? kResultOk : kInvalidArgument); }

    tresult PLUGIN_API disconnect (Vst::IConnectionPoint *other) override
        { return (other ? kResultOk : kInvalidArgument); }

    tresult PLUGIN_API notify (Vst::IMessage *message) override
        { return m_pPlugin->notify(message); }

private:

    // Instance client.
    VST3_Plugin *m_pPlugin;
};

tresult PLUGIN_API VST3_Plugin::Handler::queryInterface (
	const char *_iid, void **obj )
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IConnectionPoint::iid, IConnectionPoint)

    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API VST3_Plugin::Handler::addRef (void)
    { return 1000; }

uint32 PLUGIN_API VST3_Plugin::Handler::release (void)
    { return 1000; }

//------------------------------------------------------------------------
// VST3_Plugin::Stream - Memory based stream for IBStream impl.

class VST3_Plugin::Stream : public IBStream
{
public:

    // Constructors.
    Stream () : m_data(nullptr), m_size(0), m_pos(0)
            { FUNKNOWN_CTOR }
    Stream (void * data, int64 data_size) : m_data(data), m_size(data_size), m_pos(0)
            { FUNKNOWN_CTOR }

    // Destructor.
    virtual ~Stream () { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IBStream ---
    //
    tresult PLUGIN_API read (void *buffer, int32 nbytes, int32 *nread) override
    {
        if (m_pos + nbytes > m_size)
        {
            const int32 nsize = int32(m_size - m_pos);
            if (nsize > 0)
            {
                nbytes = nsize;
            } else
            {
                nbytes = 0;
                m_pos = int64(m_size);
            }
        }

        if (nbytes > 0)
        {
            std::memcpy(buffer, static_cast<const uint8_t*>(m_data) + m_pos, nbytes);
            m_pos += nbytes;
        }

        if (nread)
            *nread = nbytes;

        return kResultOk;
    }

    tresult PLUGIN_API write (void *buffer, int32 nbytes, int32 *nwrite) override
    {
        if (buffer == nullptr)
            return kInvalidArgument;

        const int32 nsize = m_pos + nbytes;
        if (nsize > m_size)
        {
            m_data = std::realloc(m_data, nsize);
            m_size = nsize;
        }

        if (m_pos >= 0 && nbytes > 0)
        {
            std::memcpy(static_cast<uint8_t*>(m_data) + m_pos, buffer, nbytes);
            m_pos += nbytes;
        }
        else nbytes = 0;

        if (nwrite)
            *nwrite = nbytes;

        return kResultOk;
    }

    tresult PLUGIN_API seek (int64 pos, int32 mode, int64 *npos) override
    {
        if (mode == kIBSeekSet)
            m_pos = pos;
        else
        if (mode == kIBSeekCur)
            m_pos += pos;
        else
        if (mode == kIBSeekEnd)
            m_pos = m_size - pos;

        if (m_pos < 0)
            m_pos = 0;
        else
        if (m_pos > m_size)
            m_pos = m_size;

        if (npos)
            *npos = m_pos;

        return kResultTrue;
    }

    tresult PLUGIN_API tell (int64 *npos) override
    {
        if (npos)
        {
            *npos = m_pos;
            return kResultOk;
        } else
        {
            return kInvalidArgument;
        }
    }

    // Other accessors.
    //
    void * data() const { return m_data; }
    int64  size() { return m_pos; }

protected:

    // Instance members.
    void*           m_data;
    int64           m_size;
    int64           m_pos;
};

IMPLEMENT_FUNKNOWN_METHODS (VST3_Plugin::Stream, IBStream, IBStream::iid)


VST3_Plugin::VST3_Plugin() :
    Plugin_Module(),
    _pHostContext(nullptr),
    _plugin_filename(),
    _sUniqueID(),
    _sName(),
    _last_chunk(nullptr),
    _project_file(),
    _found_plugin(false),
    _pModule(nullptr),
    _pHandler(nullptr),
    _pComponent(nullptr),
    _pController(nullptr),
    m_unitInfos(nullptr),
    _pProcessor(nullptr),
    _bProcessing(false),
    _vst_buffers_in(nullptr),
    _vst_buffers_out(nullptr),
    _iAudioInBuses(0),
    _iAudioOutBuses(0),
    _iMidiIns(0),
    _iMidiOuts(0),
    _audio_in_buffers(nullptr),
    _audio_out_buffers(nullptr),
    _activated(false),
    _bEditor(false),
    _position(0),
    _bpm(120.0f),
    _rolling(false),
    _bEditorCreated(false),
    _x_is_resizable(false),
    _x_is_visible(false),
    _timer_registered(false),
    _f_miliseconds(float(DEFAULT_MSECS) * .001),
    _i_miliseconds(DEFAULT_MSECS),
    _event_handlers_registered(false),
    _iPlugView(nullptr),
    _pEditorFrame(nullptr),
    _pRunloop(nullptr)
{
    _plug_type = Type_VST3;
    _pHostContext = new VST3PluginHost(this);
    _pRunloop = new Vst::EditorHost::RunLoop(this);

    log_create();
}

VST3_Plugin::~VST3_Plugin()
{
    log_destroy();

    if(_x_is_visible)
        hide_custom_ui();

    deactivate();

    _pProcessor = nullptr;
    _pHandler = nullptr;

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

    if(_vst_buffers_in != nullptr)
    {
        for(int i = 0; i < _iAudioInBuses; i++)
        {
            delete[] _vst_buffers_in[i].channelBuffers32;
        }
        delete[] _vst_buffers_in;
    }

    if(_vst_buffers_out != nullptr)
    {
        for(int i = 0; i < _iAudioOutBuses; i++)
        {
            delete[] _vst_buffers_out[i].channelBuffers32;
        }
        delete[] _vst_buffers_out;
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
    _pHostContext->clear();

    if ( _last_chunk )
        std::free(_last_chunk);

    delete _pRunloop;

    /* This is the case when the user manually removes a Plugin. We set the
     _is_removed = true, and add any custom data directory to the remove directories
     vector. If the user saves the project then we remove any items in the vector.
     We also clear the vector. If the user abandons any changes on exit, then any
     items added to the vector since the last save will not be removed */
    if(_is_removed)
    {
        if(!_project_file.empty())
        {
            remove_custom_data_directories.push_back(_project_file);
        }
    }

    // Causes mumap_chunk(): invalid pointer
    //if(_pHostContext)
    //    delete _pHostContext;
}

bool
VST3_Plugin::load_plugin ( Module::Picked picked )
{
    _plugin_filename = picked.s_plug_path;
    _sUniqueID = picked.s_unique_id;

    if (!find_vst_binary())
    {
        DMESSAGE("Failed to find a suitable VST3 bundle binary %s", _plugin_filename.c_str());
        return false;
    }

    if (!open_file(_plugin_filename))
    {
        DMESSAGE("Could not open file %s", _plugin_filename.c_str());
        return false;
    }

    _found_plugin = false;
    unsigned long i = 0;
    while (open_descriptor(i))
    {
        if(_found_plugin)
            break;

        ++i;
    }

    if (!_found_plugin)
    {
        DMESSAGE("Could not find %s: ID = (%s)", _plugin_filename.c_str(), _sUniqueID.c_str());
        return false;
    }

    base_label(_sName.c_str());

    _plugin_ins  = numChannels(Vst::kAudio, Vst::kInput);
    _plugin_outs = numChannels(Vst::kAudio, Vst::kOutput);
    _iMidiIns   = numChannels(Vst::kEvent, Vst::kInput);
    _iMidiOuts  = numChannels(Vst::kEvent, Vst::kOutput);

    initialize_plugin();

    Vst::IEditController *controller = _pController;
    if (controller)
    {
        IPtr<IPlugView> editor =
                owned(controller->createView(Vst::ViewType::kEditor));
        _bEditor = (editor != nullptr);
    }

    create_audio_ports();
    create_midi_ports();
    create_control_ports();

    if ( !process_reset() )
    {
        DMESSAGE("Process reset failed!");
        return false;
    }

    if(!_plugin_ins)
        is_zero_input_synth(true);

    _use_custom_data = true;

    return true;
}

bool
VST3_Plugin::configure_inputs ( int n )
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
VST3_Plugin::handle_port_connection_change ( void )
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
VST3_Plugin::handle_chain_name_changed ( void )
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
VST3_Plugin::handle_sample_rate_change ( nframes_t /*sample_rate*/ )
{
    process_reset();
}

void
VST3_Plugin::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );
}

void
VST3_Plugin::bypass ( bool v )
{
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

void
VST3_Plugin::freeze_ports ( void )
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
VST3_Plugin::thaw_ports ( void )
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
VST3_Plugin::clear_midi_vectors()
{
    midi_input.clear();
    midi_output.clear();
}

void
VST3_Plugin::configure_midi_inputs ()
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
VST3_Plugin::configure_midi_outputs ()
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
VST3_Plugin::get_module_latency ( void ) const
{
    if (_pProcessor)
        return _pProcessor->getLatencySamples();
    else
        return 0; 
}

void
VST3_Plugin::process ( nframes_t nframes )
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
        if (!_pProcessor)
            return;

        if (!_bProcessing)
            return;

        process_jack_transport( nframes );

        for( unsigned int i = 0; i < midi_input.size(); ++i )
        {
            /* JACK MIDI in to plugin MIDI in */
            process_jack_midi_in( nframes, i );
        }

        _cParams_out.clear();
        _cEvents_out.clear();

        int j = 0;
        for (int i = 0; i < _iAudioInBuses; i++)
        {
            for (int k = 0; k < _vst_buffers_in[i].numChannels; k++)
            {
               // DMESSAGE("III = %d: KKK = %d: JJJ = %d", i, k, j);
                _vst_buffers_in[i].channelBuffers32[k] = _audio_in_buffers[j] ;
                j++;
            }
        }

        j = 0;
        for (int i = 0; i < _iAudioOutBuses; i++)
        {
            for (int k = 0; k < _vst_buffers_out[i].numChannels; k++)
            {
               // DMESSAGE("III = %d: KKK = %d: JJJ = %d", i, k, j);
                _vst_buffers_out[i].channelBuffers32[k] = _audio_out_buffers[j] ;
                j++;
            }
        }

        _vst_process_data.numSamples = nframes;

        if (_pProcessor->process(_vst_process_data) != kResultOk)
        {
            WARNING("[%p]::process() FAILED!", this);
        }

        for ( unsigned int i = 0; i < midi_output.size(); ++i)
        {
            /* Plugin to JACK MIDI out */
            process_jack_midi_out( nframes, i);
        }

        _cEvents_in.clear();
        _cParams_in.clear();
    }
}

// Set/add a parameter value/point.
void
VST3_Plugin::setParameter (
	Vst::ParamID id, Vst::ParamValue value, uint32 offset )
{
    int32 index = 0;
    Vst::IParamValueQueue *queue = _cParams_in.addParameterData(id, index);
    if (queue && (queue->addPoint(offset, value, index) != kResultOk))
    {
        WARNING("setParameter(%u, %g, %u) FAILED!", this, id, value, offset);
    }
}

void
VST3_Plugin::set_control_value(unsigned long port_index, float value, bool update_custom_ui)
{
    if( port_index >= control_input.size())
    {
        DMESSAGE("Invalid Port Index = %d: Value = %f", port_index, value);
        return;
    }

    _is_from_custom_ui = !update_custom_ui;

    float normalized_value = value;

    if (control_input[port_index].hints.type == Port::Hints::INTEGER)
    {
        normalized_value = value * control_input[port_index].hints.maximum;
    }

    control_input[port_index].control_value(normalized_value);

    if (!dirty())
        set_dirty();
}

/**
 From Host to plugin - set parameter values.
 */
void
VST3_Plugin::updateParam(Vst::ParamID id, float fValue)
{
    Vst::IEditController *controller = _pController;
    if (!controller)
        return;

    DMESSAGE("UpdateParam ID = %u: Value = %f", id, fValue);

    const Vst::ParamValue value = Vst::ParamValue(fValue);

    setParameter(id, value, 0); // sends to plugin
    controller->setParamNormalized(id, value);  // For gui ???
}

// Parameters update methods.
void
VST3_Plugin::updateParamValues(bool update_custom_ui)
{
    for ( unsigned int i = 0; i < control_input.size(); ++i)
    {
        float value = (float) getParameter(control_input[i].hints.parameter_id);

        if( control_input[i].control_value() != value)
        {
            set_control_value(i, value, update_custom_ui);
        }
    }
}

// Get current parameter value.
Vst::ParamValue
VST3_Plugin::getParameter ( Vst::ParamID id ) const
{
    Vst::IEditController *controller = _pController;
    if (controller)
    {
        return controller->getParamNormalized(id);
    }
    else
        return 0.0;
}

tresult
VST3_Plugin::notify ( Vst::IMessage *message )
{
    DMESSAGE("[%p]::notify(%p)", this, message);

    Vst::IComponent *component = _pComponent;
    FUnknownPtr<Vst::IConnectionPoint> component_cp(component);
    if (component_cp)
        component_cp->notify(message);

    Vst::IEditController *controller = _pController;
    FUnknownPtr<Vst::IConnectionPoint> controller_cp(controller);
    if (controller_cp)
        controller_cp->notify(message);

    return kResultOk;
}

bool
VST3_Plugin::try_custom_ui()
{
    /* Toggle show and hide */
    if(_bEditorCreated)
    {
        if (_x_is_visible)
        {
            hide_custom_ui();
            return true;
        }
    }

    if( !init_custom_ui() )
        return false;

    _bEditorCreated = show_custom_ui();

    return _bEditorCreated;
}

bool
VST3_Plugin::init_custom_ui()
{
    if (!openEditor())
    {
        DMESSAGE("No custom UI is available for %s", label());
        return false;
    }

    IPlugView *plugView = _iPlugView;
    if (!plugView)
        return false;

    if (plugView->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultOk)
    {
        DMESSAGE("[%p]::openEditor"
                " *** X11 Window platform is not supported (%s).", this,
                kPlatformTypeX11EmbedWindowID);
        return false;
    }
    
    if (_iPlugView->canResize() == kResultOk)
        _x_is_resizable = true;

    _pEditorFrame = new EditorFrame(this, plugView, _x_is_resizable);
    _pEditorFrame->setTitle(label());

    void *wid = _pEditorFrame->getPtr();
    
    if (plugView->attached(wid, kPlatformTypeX11EmbedWindowID) != kResultOk)
    {
        DMESSAGE(" *** Failed to create/attach editor window - %s.", label());
        closeEditor();
        return false;
    }

    return true;
}

// Editor controller methods.
bool
VST3_Plugin::openEditor (void)
{
    closeEditor();

    Vst::IEditController *controller = _pController;
    if (controller)
        _iPlugView = owned(controller->createView(Vst::ViewType::kEditor));

    return (_iPlugView != nullptr);
}

void
VST3_Plugin::closeEditor (void)
{
    _pHostContext->stopTimer();

    if (_pEditorFrame != nullptr)
        _pEditorFrame->hide();

    IPlugView *plugView = _iPlugView;
    if (plugView && plugView->removed() != kResultOk)
    {
        DMESSAGE(" *** Failed to remove/detach window.");
    }

    if (_pEditorFrame)
    {
        delete _pEditorFrame;
        _pEditorFrame = nullptr;
    }

    _iPlugView = nullptr;

    _pRunloop->stop();
}

bool
VST3_Plugin::show_custom_ui()
{
    _pRunloop->setDisplay( (Display*) _pEditorFrame->getDisplay());
    _pEditorFrame->show();
    _pEditorFrame->focus();

    _x_is_visible = true;
    
    _pRunloop->registerWindow (
        (XID) _pEditorFrame->getparentwin(),
        [this] (const XEvent& e) { return _pEditorFrame->handlePlugEvent (e); });
                
    _pRunloop->registerWindow (
        (XID) _pEditorFrame->getPtr(),
        [this] (const XEvent& e) { return _pEditorFrame->handlePlugEvent (e); });

    _pRunloop->start(_event_handlers_registered );

    _pHostContext->startTimer(DEFAULT_MSECS);

    return true;
}

void
VST3_Plugin::add_ntk_timer(int i_msecs)
{
    DMESSAGE("ADD TIMER i_msecs = %i: - %s", i_msecs, label());
    
    _i_miliseconds = i_msecs;
    _f_miliseconds = float(_i_miliseconds) * .001;

    Fl::add_timeout( _f_miliseconds, &VST3_Plugin::custom_update_ui, this );
}

void
VST3_Plugin::remove_ntk_timer()
{
    DMESSAGE("REMOVE TIMER %s", label());
    Fl::remove_timeout(&VST3_Plugin::custom_update_ui, this);
}

/**
 Callback for custom ui idle interface
 */
void 
VST3_Plugin::custom_update_ui ( void *v )
{
    ((VST3_Plugin*)v)->custom_update_ui_x();
}

/**
 The idle callback to update_custom_ui()
 */
void
VST3_Plugin::custom_update_ui_x()
{
    _pEditorFrame->idle();

    _pRunloop->proccess_timers(_timer_registered, _event_handlers_registered );

    if(_x_is_visible)
    {
        Fl::repeat_timeout( _f_miliseconds, &VST3_Plugin::custom_update_ui, this );
    }
    else
    {
        hide_custom_ui();
    }
}

bool
VST3_Plugin::hide_custom_ui()
{
    DMESSAGE("Closing Custom Interface");

    closeEditor();

    _x_is_visible = false;

    if (_bEditorCreated)
    {
        _bEditorCreated = false;
    }

    return true;
}

// Parameter finder (by id).
unsigned long 
VST3_Plugin::findParamId ( uint32_t id ) const
{
    std::unordered_map<uint32_t, unsigned long>::const_iterator got
        = _mParamIds.find (id);

    if ( got == _mParamIds.end() )
    {
        // probably a control out - we don't do anything with these
         // DMESSAGE("Param Id not found = %d", id);
        return 0;
    }

    unsigned long index = got->second;
    return index;
}

bool
VST3_Plugin::find_vst_binary()
{
    // First check the path from the snapshot
    if ( std::filesystem::exists(_plugin_filename) )
        return true;

    /*  We did not find the plugin from the snapshot path so lets try
        a different path. The case is if the project was copied to a
        different computer in which the plugins are installed in a different
        location - i.e. - /usr/lib vs /usr/local/lib.
    */

    std::string file(_plugin_filename);
    std::string restore;
    // Find the base plugin name
    std::size_t found = file.find_last_of("/\\");
    restore = file.substr(found);
    DMESSAGE("Restore = %s", restore.c_str());

    auto sp = vst3_discovery::installedVST3s();   // This to get paths
    vst3_discovery::vst3_discovery_scan plugin;

    for (const auto &q : sp)
    {
        std::string path = plugin.get_vst3_object_file(q.u8string().c_str());
        DMESSAGE("PATH = %s", path.c_str());

        found = path.find_last_of("/\\");
        std::string base = path.substr(found);

        // Compare the base names and if they match, then use the path
        if (strcmp( restore.c_str(), base.c_str() ) == 0 )
        {
            if ( std::filesystem::exists(path) )
            {
                _plugin_filename = path;
                return true;    // We found it
            }
            else
                return false;   // If it still does not exist then abandon
        }
        else
        {
            // keep trying until all available paths are checked
            continue;
        }

    }
    // We never found it
    return false;
}

// File loader.
bool
VST3_Plugin::open_file ( const std::string& sFilename )
{
    DMESSAGE("Open %s", sFilename.c_str());

    _pModule = ::dlopen(sFilename.c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!_pModule)
        return false;

    typedef bool (*VST3_ModuleEntry)(void *);
    const VST3_ModuleEntry module_entry
            = VST3_ModuleEntry(::dlsym(_pModule, "ModuleEntry"));

    if (module_entry)
        module_entry(_pModule);

    return true;
}

bool
VST3_Plugin::open_descriptor(unsigned long iIndex)
{
    close_descriptor();

    typedef IPluginFactory *(PLUGIN_API *VST3_GetPluginFactory)();
    const VST3_GetPluginFactory get_plugin_factory
            = reinterpret_cast<VST3_GetPluginFactory> (::dlsym(_pModule, "GetPluginFactory"));

    if (!get_plugin_factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to resolve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory *factory = get_plugin_factory();
    if (!factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    PFactoryInfo factoryInfo;
    if (factory->getFactoryInfo(&factoryInfo) != kResultOk)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory information.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory2 *factory2 = FUnknownPtr<IPluginFactory2> (factory);
    IPluginFactory3 *factory3 = FUnknownPtr<IPluginFactory3> (factory);

    if (factory3)
    {
        factory3->setHostContext(_pHostContext->get());
    }

    const int32 nclasses = factory->countClasses();

    unsigned long i = 0;

    for (int32 n = 0; n < nclasses; ++n)
    {
        PClassInfo classInfo;
        if (factory->getClassInfo(n, &classInfo) != kResultOk)
            continue;

        if (::strcmp(classInfo.category, kVstAudioEffectClass))
            continue;

        if (iIndex == i)
        {
            PClassInfoW classInfoW;
            if (factory3 && factory3->getClassInfoUnicode(n, &classInfoW) == kResultOk)
            {
                _sName = utf16_to_utf8(classInfoW.name);
            } else
            {
                PClassInfo2 classInfo2;
                if (factory2 && factory2->getClassInfo2(n, &classInfo2) == kResultOk)
                {
                    _sName = classInfo2.name;
                } else 
                {
                    _sName = classInfo.name;
                }
            }

            std::string iUniqueID = vst3_discovery::UIDtoString(false, classInfo.cid);
            if(_sUniqueID != iUniqueID)
            {
                continue;
            }
            else
            {
                _found_plugin = true;
            }

            Vst::IComponent *component = nullptr;
            if (factory->createInstance(
                    classInfo.cid, Vst::IComponent::iid,
                    (void **) &component) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to create plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                return false;
            }

            _pComponent = owned(component);

            if (_pComponent->initialize(_pHostContext->get()) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to initialize plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                close_descriptor();
                return false;
            }

            Vst::IEditController *controller = nullptr;
            if (_pComponent->queryInterface(
                    Vst::IEditController::iid,
                    (void **) &controller) != kResultOk)
            {
                TUID controller_cid;
                if (_pComponent->getControllerClassId(controller_cid) == kResultOk)
                {
                    if (factory->createInstance(
                            controller_cid, Vst::IEditController::iid,
                            (void **) &controller) != kResultOk)
                    {
                        DMESSAGE("[%p]::open(\"%s\", %lu)"
                                " *** Failed to create plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);
                    }

                    if (controller &&
                            controller->initialize(_pHostContext->get()) != kResultOk)
                    {
                        DMESSAGE("[%p]::open(\"%s\", %lu)"
                                " *** Failed to initialize plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                        controller = nullptr;
                    }
                }
            }

            if (controller) _pController = owned(controller);

            Vst::IUnitInfo *unitInfos = nullptr;
            if (_pComponent->queryInterface(
                            Vst::IUnitInfo::iid,
                            (void **) &unitInfos) != kResultOk)
            {
                if (_pController &&
                        _pController->queryInterface(
                                Vst::IUnitInfo::iid,
                                (void **) &unitInfos) != kResultOk)
                {
                    DMESSAGE("[%p]::open(\"%s\", %lu)"
                            " *** Failed to create plug-in units information.", this,
                            _plugin_filename.c_str(), iIndex);
                }
            }

            if (unitInfos) m_unitInfos = owned(unitInfos);

            // Connect components...
            if (_pComponent && _pController)
            {
                FUnknownPtr<Vst::IConnectionPoint> component_cp(_pComponent);
                FUnknownPtr<Vst::IConnectionPoint> controller_cp(_pController);
                if (component_cp && controller_cp)
                {
                        component_cp->connect(controller_cp);
                        controller_cp->connect(component_cp);
                }
            }

            return true;
        }

        ++i;
    }

    return false;
}

void
VST3_Plugin::close_descriptor()
{
    if (_pComponent && _pController)
    {
        FUnknownPtr<Vst::IConnectionPoint> component_cp(_pComponent);
        FUnknownPtr<Vst::IConnectionPoint> controller_cp(_pController);
        if (component_cp && controller_cp)
        {
            component_cp->disconnect(controller_cp);
            controller_cp->disconnect(component_cp);
        }
    }

    m_unitInfos = nullptr;

    if (_pComponent && _pController &&
            FUnknownPtr<Vst::IEditController> (_pComponent).getInterface())
    {
        _pController->terminate();
    }

    _pController = nullptr;

    if (_pComponent)
    {
        _pComponent->terminate();
        _pComponent = nullptr;
        typedef bool (PLUGIN_API *VST3_ModuleExit)();
        const VST3_ModuleExit module_exit
                = reinterpret_cast<VST3_ModuleExit> (::dlsym(_pModule, "ModuleExit"));

        if (module_exit)
            module_exit();
    }
}

void
VST3_Plugin::set_input_buffer ( int n, void *buf )
{
    _audio_in_buffers[n] = static_cast<float*>( buf );
}

void
VST3_Plugin::set_output_buffer ( int n, void *buf )
{
    _audio_out_buffers[n] = static_cast<float*>( buf );
}

bool
VST3_Plugin::loaded ( void ) const
{
    if ( _pModule )
        return true;

    return false;
}

bool
VST3_Plugin::process_reset()
{
    if (!_pProcessor)
        return false;

    deactivate();
    
    _position = 0;
    _bpm = 120.0f;
    _rolling = false;

    // Initialize running state...
    _cParams_in.clear();
    _cParams_out.clear();

    _cEvents_in.clear();
    _cEvents_out.clear();

    Vst::ProcessSetup setup;
    setup.processMode        = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = buffer_size();
    setup.sampleRate         = float(sample_rate());

    if (_pProcessor->setupProcessing(setup) != kResultOk)
        return false;

    // Setup processor data struct...
    _vst_process_data.numSamples             = buffer_size();
    _vst_process_data.symbolicSampleSize     = Vst::kSample32;

    if (_plugin_ins > 0) 
    {
        _vst_process_data.numInputs          = _iAudioInBuses;
        _vst_process_data.inputs             = _vst_buffers_in;
    } else 
    {
        _vst_process_data.numInputs          = 0;
        _vst_process_data.inputs             = nullptr;
    }

    if (_plugin_outs > 0)
    {
        _vst_process_data.numOutputs         = _iAudioOutBuses;
        _vst_process_data.outputs            = _vst_buffers_out;
    } else
    {
        _vst_process_data.numOutputs         = 0;
        _vst_process_data.outputs            = nullptr;
    }

    _vst_process_data.processContext         = _pHostContext->processContext();
    _vst_process_data.inputEvents            = &_cEvents_in;
    _vst_process_data.outputEvents           = &_cEvents_out;
    _vst_process_data.inputParameterChanges  = &_cParams_in;
    _vst_process_data.outputParameterChanges = &_cParams_out;

    activate();

    return true;
}

void
VST3_Plugin::process_jack_transport ( uint32_t nframes )
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
    
    _pHostContext->updateProcessContext(pos, xport_changed, has_bbt);

    // Update transport state to expected values for next cycle
    _position = rolling ? pos.frame + nframes : pos.frame;
    _bpm      = has_bbt ? pos.beats_per_minute : _bpm;
    _rolling  = rolling;
}

void
VST3_Plugin::process_jack_midi_in ( uint32_t nframes, unsigned int port )
{
    /* Process any MIDI events from jack */
    if ( midi_input[port].jack_port() )
    {
        void *buf = midi_input[port].jack_port()->buffer( nframes );

        for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i)
        {
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, i);

            process_midi_in(ev.buffer, ev.size, ev.time, port);
        }
    }
}

void
VST3_Plugin::process_midi_in (
	unsigned char *data, unsigned int size,
	unsigned long offset, unsigned short port )
{
    for (uint32_t i = 0; i < size; ++i)
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

        // program change
        if (status == 0xc0)
        {
            // TODO: program-change...
            continue;
        }

        // after-touch
        if (status == 0xd0)
        {
            const MidiMapKey mkey(port, channel, Vst::kAfterTouch);

            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = _mMidiMap.find (mkey);

            if (got == _mMidiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float pre = float(key) / 127.0f;
                setParameter(id, Vst::ParamValue(pre), offset);
            }
            continue;
        }

        // check data size (#2)
        if (++i >= size)
            break;

        // channel value (normalized)
        const int value = (data[i] & 0x7f);

        Vst::Event event;
        ::memset(&event, 0, sizeof(Vst::Event));
        event.busIndex = port;
        event.sampleOffset = offset;
        event.flags = Vst::Event::kIsLive;

        // note on
        if (status == 0x90)
        {
            event.type = Vst::Event::kNoteOnEvent;
            event.noteOn.noteId = -1;
            event.noteOn.channel = channel;
            event.noteOn.pitch = key;
            event.noteOn.velocity = float(value) / 127.0f;
            _cEvents_in.addEvent(event);
        }
        // note off
        else if (status == 0x80)
        {
            event.type = Vst::Event::kNoteOffEvent;
            event.noteOff.noteId = -1;
            event.noteOff.channel = channel;
            event.noteOff.pitch = key;
            event.noteOff.velocity = float(value) / 127.0f;
            _cEvents_in.addEvent(event);
        }
        // key pressure/poly.aftertouch
        else if (status == 0xa0)
        {
            event.type = Vst::Event::kPolyPressureEvent;
            event.polyPressure.channel = channel;
            event.polyPressure.pitch = key;
            event.polyPressure.pressure = float(value) / 127.0f;
            _cEvents_in.addEvent(event);
        }
        // control-change
        else if (status == 0xb0)
        {
            const MidiMapKey mkey(port, channel, key);

            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = _mMidiMap.find (mkey);

            if (got == _mMidiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float val = float(value) / 127.0f;
                setParameter(id, Vst::ParamValue(val), offset);
            }
        }
        // pitch-bend
        else if (status == 0xe0)
        {
            const MidiMapKey mkey(port, channel, Vst::kPitchBend);

            std::map<MidiMapKey, Vst::ParamID>::const_iterator got
                    = _mMidiMap.find (mkey);

            if (got == _mMidiMap.end())
                continue;

            const Vst::ParamID id = got->second;

            if (id != Vst::kNoParamId)
            {
                const float pitchbend
                        = float(key + (value << 7)) / float(0x3fff);
                setParameter(id, Vst::ParamValue(pitchbend), offset);
            }
        }
    }
}

void
VST3_Plugin::process_jack_midi_out ( uint32_t nframes, unsigned int port )
{
    void* buf = NULL;

    if ( midi_output[port].jack_port() )
    {
        buf = midi_output[port].jack_port()->buffer( nframes );
        jack_midi_clear_buffer(buf);

        // Process MIDI output stream, if any...
        VST3IMPL::EventList& events_out = _cEvents_out;
        const int32 nevents = events_out.getEventCount();
        for (int32 i = 0; i < nevents; ++i)
        {
            Vst::Event event;
            if (events_out.getEvent(i, event) == kResultOk)
            {
                switch (event.type)
                {
                case Vst::Event::kNoteOnEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_NOTE_ON + event.noteOn.channel;
                    midi_note[1] = event.noteOn.pitch;
                    midi_note[2] = (unsigned char)(event.noteOn.velocity * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI note on error = %d", ret);

                    break;
                }
                case Vst::Event::kNoteOffEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_NOTE_OFF + event.noteOff.channel;
                    midi_note[1] = event.noteOff.pitch;
                    midi_note[2] = (unsigned char)(event.noteOff.velocity * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI note off error = %d", ret);

                    break;
                }
                case Vst::Event::kPolyPressureEvent:
                {
                    unsigned char  midi_note[3];
                    midi_note[0] = EVENT_CHANNEL_PRESSURE + event.polyPressure.channel;
                    midi_note[1] = event.polyPressure.pitch;
                    midi_note[2] = (unsigned char)(event.polyPressure.pressure * 127);

                    size_t size = 3;
                    int nBytes = static_cast<int> (size);
                    int ret =  jack_midi_event_write(buf, event.sampleOffset,
                            static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                    if ( ret )
                        WARNING("Jack MIDI polyPressure error = %d", ret);

                    break;
                }
                }
            }
        }
    }
}


void
VST3_Plugin::initialize_plugin()
{
    clear_plugin();

    Vst::IComponent *component = _pComponent;
    if (!component)
            return;

    Vst::IEditController *controller = _pController;
    if (controller)
    {
        _pHandler = owned(NEW VST3_Plugin::Handler(this));
        controller->setComponentHandler(_pHandler);
    }

    _pProcessor = FUnknownPtr<Vst::IAudioProcessor> (component);

    if (controller)
    {
        const int32 nparams = controller->getParameterCount();
        for (int32 i = 0; i < nparams; ++i)
        {
            Vst::ParameterInfo paramInfo;
            ::memset(&paramInfo, 0, sizeof(Vst::ParameterInfo));
            if (controller->getParameterInfo(i, paramInfo) == kResultOk)
            {
                if (m_programParamInfo.unitId != Vst::UnitID(-1))
                    continue;

                if (paramInfo.flags & Vst::ParameterInfo::kIsProgramChange &&
                        !(paramInfo.flags & Vst::ParameterInfo::kIsHidden))
                {
                    m_programParamInfo = paramInfo;
                }
            }
        }
        if (m_programParamInfo.unitId != Vst::UnitID(-1))
        {
            Vst::IUnitInfo *unitInfos = m_unitInfos;
            if (unitInfos)
            {
                const int32 nunits = unitInfos->getUnitCount();
                for (int32 i = 0; i < nunits; ++i)
                {
                    Vst::UnitInfo unitInfo;
                    if (unitInfos->getUnitInfo(i, unitInfo) != kResultOk)
                        continue;
                    if (unitInfo.id != m_programParamInfo.unitId)
                        continue;

                    const int32 nlists = unitInfos->getProgramListCount();
                    for (int32 j = 0; j < nlists; ++j)
                    {
                        Vst::ProgramListInfo programListInfo;
                        if (unitInfos->getProgramListInfo(j, programListInfo) != kResultOk)
                            continue;
                        if (programListInfo.id != unitInfo.programListId)
                            continue;

                        const int32 nprograms = programListInfo.programCount;
                        for (int32 k = 0; k < nprograms; ++k)
                        {
                            Vst::String128 name;
                            if (unitInfos->getProgramName(
                                            programListInfo.id, k, name) == kResultOk)
                            {
                                std::string s_name = std::to_string(k);
                                s_name += " - ";
                                s_name += utf16_to_utf8(name);
                                _PresetList.push_back(s_name);
                                DMESSAGE("Program name 1 = %s", s_name.c_str() );
                            }
                        }
                        break;
                    }
                }
            }
        }
        if (_PresetList.empty() && m_programParamInfo.stepCount > 0)
        {
            const int32 nprograms = m_programParamInfo.stepCount + 1;
            for (int32 k = 0; k < nprograms; ++k)
            {
                const Vst::ParamValue value
                        = Vst::ParamValue(k)
                        / Vst::ParamValue(m_programParamInfo.stepCount);
                Vst::String128 name;
                if (controller->getParamStringByValue(
                                m_programParamInfo.id, value, name) == kResultOk)
                {
                    std::string s_name = std::to_string(k);
                    s_name += " - ";
                    s_name += utf16_to_utf8(name);
                    _PresetList.push_back(s_name);
                    DMESSAGE("Program name 2 = %s", s_name.c_str() );
                }
            }
        }
    }

    if (controller)
    {
        const int32 nports = _iMidiIns;
        FUnknownPtr<Vst::IMidiMapping> midiMapping(controller);
        if (midiMapping && nports > 0)
        {
            for (int16 i = 0; i < Vst::kCountCtrlNumber; ++i)
            { // controllers...
                for (int32 j = 0; j < nports; ++j)
                { // ports...
                    for (int16 k = 0; k < 16; ++k)
                    { // channels...
                        Vst::ParamID id = Vst::kNoParamId;
                        if (midiMapping->getMidiControllerAssignment(
                                        j, k, Vst::CtrlNumber(i), id) == kResultOk)
                        {
                            std::pair<MidiMapKey, Vst::ParamID> prm ( MidiMapKey(j, k, i), id );
                            _mMidiMap.insert(prm);
                        }
                    }
                }
            }
        }
    }
}

void
VST3_Plugin::clear_plugin()
{
    ::memset(&m_programParamInfo, 0, sizeof(Vst::ParameterInfo));
    m_programParamInfo.id = Vst::kNoParamId;
    m_programParamInfo.unitId = Vst::UnitID(-1);
    _PresetList.clear();

    _mMidiMap.clear();
}

int
VST3_Plugin::numChannels (
	Vst::MediaType type, Vst::BusDirection direction ) const
{
    if (!_pComponent)
        return -1;

    int nchannels = 0;

    const int32 nbuses = _pComponent->getBusCount(type, direction);
    for (int32 i = 0; i < nbuses; ++i)
    {
        Vst::BusInfo busInfo;
        if (_pComponent->getBusInfo(type, direction, i, busInfo) == kResultOk)
        {
            if ((busInfo.busType == Vst::kMain) ||
                    (busInfo.flags & Vst::BusInfo::kDefaultActive))
            {
                nchannels += busInfo.channelCount;
            }
        }
    }

    return nchannels;
}

void
VST3_Plugin::create_audio_ports()
{
    _iAudioInBuses = 0;
    _iAudioOutBuses = 0;

    int32 in_buses = _pComponent->getBusCount(Vst::kAudio, Vst::kInput);
    for (int32 i = 0; i < in_buses; ++i)
    {
        Vst::BusInfo busInfo;
        if (_pComponent->getBusInfo(Vst::kAudio, Vst::kInput, i, busInfo) == kResultOk)
        {
            if ((busInfo.busType == Vst::kMain) ||
                    (busInfo.flags & Vst::BusInfo::kDefaultActive))
            {
                _iAudioInBuses++;
                _vAudioInChannels.push_back(busInfo.channelCount);
            }
        }
    }

    int32 out_buses = _pComponent->getBusCount(Vst::kAudio, Vst::kOutput);
    for (int32 i = 0; i < out_buses; ++i)
    {
        Vst::BusInfo busInfo;
        if (_pComponent->getBusInfo(Vst::kAudio, Vst::kOutput, i, busInfo) == kResultOk)
        {
            if ((busInfo.busType == Vst::kMain) ||
                    (busInfo.flags & Vst::BusInfo::kDefaultActive))
            {
                _iAudioOutBuses++;
                _vAudioOutChannels.push_back(busInfo.channelCount);
            }
        }
    }

    for (int32_t i = 0; i < _plugin_ins; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::AUDIO, "input" ) );
        audio_input[i].hints.plug_port_index = i;
    }

    for (int32_t i = 0; i < _plugin_outs; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::AUDIO, "output" ) );
        audio_output[i].hints.plug_port_index = i;
    }

    _audio_in_buffers = new float * [_plugin_ins]();
    _audio_out_buffers = new float * [_plugin_outs]();

    // Setup processor audio I/O buffers...
    if (_iAudioInBuses)
    {
        _vst_buffers_in = new Vst::AudioBusBuffers[_iAudioInBuses];
        for(int i = 0; i < _iAudioInBuses; i++)
        {
            _vst_buffers_in[i].silenceFlags      = 0;
            _vst_buffers_in[i].numChannels       = _vAudioInChannels[i];
            _vst_buffers_in[i].channelBuffers32  = new float *[_vAudioInChannels[i]]();
        }
    }

    if (_iAudioOutBuses)
    {
        _vst_buffers_out = new Vst::AudioBusBuffers[_iAudioOutBuses];
        for(int i = 0; i < _iAudioOutBuses; i++)
        {
            _vst_buffers_out[i].silenceFlags     = 0;
            _vst_buffers_out[i].numChannels      = _vAudioOutChannels[i];
            _vst_buffers_out[i].channelBuffers32 = new float *[_vAudioOutChannels[i]]();
        }
    }

    MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);
}

void
VST3_Plugin::create_midi_ports()
{
    const int32 inbuses = _pComponent->getBusCount(Vst::kEvent, Vst::kInput);
    const int32 outbuses = _pComponent->getBusCount(Vst::kEvent, Vst::kOutput);

    for (int32_t i = 0; i < inbuses; ++i)
    {
        add_port( Port( this, Port::INPUT, Port::MIDI, "midi_in" ) );
    }

    for (int32_t i = 0; i < outbuses; ++i)
    {
        add_port( Port( this, Port::OUTPUT, Port::MIDI, "midi_out" ) );
    }

    MESSAGE( "Plugin has %i MIDI ins and %i MIDI outs", inbuses, outbuses);
}

void
VST3_Plugin::create_control_ports()
{
    unsigned long control_ins = 0;
    unsigned long control_outs = 0;
    
    Vst::IEditController *controller = _pController;

    if (controller)
    {
        const int32 nparams = controller->getParameterCount();

        for (int32 i = 0; i < nparams; ++i)
        {
            Port::Direction d = Port::INPUT;

            Vst::ParameterInfo paramInfo;
            ::memset(&paramInfo, 0, sizeof(Vst::ParameterInfo));
            if (controller->getParameterInfo(i, paramInfo) == kResultOk)
            {
                if (paramInfo.flags & Vst::ParameterInfo::kIsHidden)
                    continue;

                if ( !(paramInfo.flags & Vst::ParameterInfo::kIsReadOnly) &&
                        !(paramInfo.flags & Vst::ParameterInfo::kCanAutomate) )
                {
                    continue;
                }

                bool have_control_in = false;

                if (paramInfo.flags & Vst::ParameterInfo::kIsReadOnly)
                {
                    d = Port::OUTPUT;
                    ++control_outs;
                }
                else
                if (paramInfo.flags & Vst::ParameterInfo::kCanAutomate)
                {
                    d = Port::INPUT;
                    ++control_ins;
                    have_control_in = true;
                }

                std::string description = utf16_to_utf8(paramInfo.title);
                description += " ";
                description += utf16_to_utf8 (paramInfo.units);

                Port p( this, d, Port::CONTROL, strdup( description.c_str() ) );

                /* Used for OSC path creation unique symbol */
                std::string osc_symbol = strdup( utf16_to_utf8(paramInfo.shortTitle).c_str() );
                osc_symbol.erase(std::remove(osc_symbol.begin(), osc_symbol.end(), ' '), osc_symbol.end());
                osc_symbol += std::to_string( paramInfo.id );

                p.set_symbol(osc_symbol.c_str());

                p.hints.ranged = true;

                if ( paramInfo.stepCount  == 1)
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }
                else if ( paramInfo.stepCount  == 0 )
                {
                    // p.hints.ranged = false;
                    //paramInfo.
                    // continuous ??? WTF
                    p.hints.minimum = (float) 0.0;
                    p.hints.maximum = (float) 1.0;
                }
                else
                {
                    p.hints.minimum = (float) 0.0;
                    p.hints.maximum = (float) paramInfo.stepCount;
                    p.hints.type = Port::Hints::INTEGER;
                }

                p.hints.default_value = (float) paramInfo.defaultNormalizedValue;

                p.hints.parameter_id = paramInfo.id;

                if (paramInfo.flags & Vst::ParameterInfo::kIsBypass)
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }

                if (paramInfo.flags & Vst::ParameterInfo::kIsHidden)
                {
                    p.hints.visible = false;
                }

                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );

                p.hints.plug_port_index = i;

                add_port( p );

                // Cache the port ID and index for easy lookup - only _control_ins
                if (have_control_in)
                {
                   // DMESSAGE( "Control input port \"%s\" ID %u",
                   //         utf16_to_utf8(paramInfo.title).c_str(), p.hints.parameter_id );

                    std::pair<uint32_t, unsigned long> prm ( p.hints.parameter_id, control_ins - 1 );
                    _mParamIds.insert(prm);
                }
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
    
    DMESSAGE ("Control INS = %d: Control OUTS = %d", control_ins, control_outs);
}

void
VST3_Plugin::activate ( void )
{
    if ( !loaded() )
        return;

    if (_bProcessing)
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

	Vst::IComponent *component = _pComponent;
	if (component && _pProcessor)
        {
            vst3_activate(component, Vst::kAudio, Vst::kInput,  true);
            vst3_activate(component, Vst::kAudio, Vst::kOutput, true);
            vst3_activate(component, Vst::kEvent, Vst::kInput,  true);
            vst3_activate(component, Vst::kEvent, Vst::kOutput, true);
            component->setActive(true);
            _pProcessor->setProcessing(true);
            _pHostContext->processAddRef();
            _bProcessing = true;
        }
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST3_Plugin::deactivate ( void )
{
    if ( !loaded() )
        return;
    
    if (!_bProcessing)
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

   if ( _activated )
   {
        _activated = false;
        Vst::IComponent *component = _pComponent;
        if (component && _pProcessor)
        {
            _pHostContext->processReleaseRef();
            _pProcessor->setProcessing(false);
            component->setActive(false);
            _bProcessing = false;
            vst3_activate(component, Vst::kEvent, Vst::kOutput, false);
            vst3_activate(component, Vst::kEvent, Vst::kInput,  false);
            vst3_activate(component, Vst::kAudio, Vst::kOutput, false);
            vst3_activate(component, Vst::kAudio, Vst::kInput,  false);
        }
   }

    if ( chain() )
        chain()->client()->unlock();
}

void
VST3_Plugin::vst3_activate ( Vst::IComponent *component,
	Vst::MediaType type, Vst::BusDirection direction, bool state )
{
    const int32 nbuses = component->getBusCount(type, direction);
    for (int32 i = 0; i < nbuses; ++i)
    {
        Vst::BusInfo busInfo;
        if (component->getBusInfo(type, direction, i, busInfo) == kResultOk)
        {
            component->activateBus(type, direction, i, state);
        }
    }
}

void
VST3_Plugin::add_port ( const Port &p )
{
    Module::add_port(p);

    if ( p.type() == Port::MIDI && p.direction() == Port::INPUT )
        midi_input.push_back( p );
    else if ( p.type() == Port::MIDI && p.direction() == Port::OUTPUT )
        midi_output.push_back( p );
}

void
VST3_Plugin::save_VST3_plugin_state(const std::string &filename)
{
    void* data = nullptr;
    if (const std::size_t dataSize = getState(&data))
    {
        if ( data == nullptr )
        {
            fl_alert( "%s could not complete state save of %s", base_label(), filename.c_str() );
            return;
        }

        FILE *fp;
        fp = fopen(filename.c_str(), "w");

        if(fp == NULL)
        {
            fl_alert( "Cannot open file %s", filename.c_str() );
            return;
        }
        else
        {
            fwrite(data, dataSize, 1, fp);
        }
        fclose(fp);
    }
}

void
VST3_Plugin::restore_VST3_plugin_state(const std::string &filename)
{
    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");

    if (fp == NULL)
    {
        fl_alert( "Cannot open file %s", filename.c_str());
        return;
    }

    fseek(fp, 0, SEEK_END);
    uint64_t size = ftell(fp);
    rewind(fp);

    void *data = malloc(size);

    fread(data, size, 1, fp);
    fclose(fp);

    Stream state(data, size);

    if (_pComponent->setState(&state) != kResultOk)
    {
        fl_alert("IComponent::setState() FAILED! %s", filename.c_str() );
        goto restore_error;
    }

    if (_pController->setComponentState(&state) != kResultOk)
    {
        MESSAGE("IEditController::setComponentState() FAILED! %s", filename.c_str());
        //goto restore_error;
    }

    updateParamValues(false);

restore_error:

    free(data);
}

uint64_t
VST3_Plugin::getState ( void** const dataPtr )
{
    Vst::IComponent *component = _pComponent;
    if (!component)
        return false;

    if ( _last_chunk )
    {
        std::free(_last_chunk);
        _last_chunk = nullptr;
    }

    Stream state;

    if (component->getState(&state) != kResultOk)
    {
        DMESSAGE("getState() Vst::IComponent::getState() FAILED!");
        *dataPtr = _last_chunk = nullptr;
        return 0;
    }
    else
    {
        *dataPtr = _last_chunk = state.data();
        return state.size();
    }

    return 0;
}

void
VST3_Plugin::setProgram(int choice)
{
    float tmp = float(choice);
    float value = tmp / float(m_programParamInfo.stepCount);
    
    updateParam(m_programParamInfo.id, value);
}

void
VST3_Plugin::get ( Log_Entry &e ) const
{
    e.add( ":vst_unique_id", _sUniqueID.c_str() );
    e.add( ":vst3_plugin_path", _plugin_filename.c_str() );
 
    /* these help us display the module on systems which are missing this plugin */
    e.add( ":plugin_ins", _plugin_ins );
    e.add( ":plugin_outs", _plugin_outs );

    if ( _use_custom_data  )
    {
        Module *m = (Module *) this;
        VST3_Plugin *pm = static_cast<VST3_Plugin *> (m);

        /* Export directory location */
        if(!export_import_strip.empty())
        {
            std::size_t found = export_import_strip.find_last_of("/\\");
            std::string path = (export_import_strip.substr(0, found));

            std::string filename = pm->get_custom_data_location(path);

            pm->save_VST3_plugin_state(filename);
            DMESSAGE("Export location = %s", filename.c_str());

            std::string base_file = filename.substr(filename.find_last_of("/\\") + 1);
            e.add( ":custom_data", base_file.c_str() );
        }
        else
        {
            /* If we already have pm->_project_file, it means that we have an existing project
               already loaded. So use that file instead of making a new one */
            std::string file = pm->_project_file;
            if(file.empty())
            {
                /* This is a new project */
                file = pm->get_custom_data_location(project_directory);
            }
            if ( !file.empty() )
            {
                /* This is an existing project */
                pm->_project_file = file;
                pm->save_VST3_plugin_state(file);

                std::string base_file = file.substr(file.find_last_of("/\\") + 1);
                e.add( ":custom_data", base_file.c_str() );
            }
        }
    }

    Module::get( e );
}

void
VST3_Plugin::set ( Log_Entry &e )
{
    int n = 0;
    std::string restore = "";

    /* we need to have number() defined before we create the control inputs in load() */
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

    	if ( ! strcmp(s, ":number" ) )
        {
	    n = atoi(v);
        }
    }

    /* need to call this to set label even for version 0 modules */
    number(n);

    std::string s_unique_id = "";
    std::string s_vst3_path = "";
    
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":vst_unique_id") )
        {
            s_unique_id = v;
        }
        else if ( ! strcmp( s, ":vst3_plugin_path" ) )
        {
            s_vst3_path = v;
        }
        else if ( ! strcmp( s, ":plugin_ins" ) )
        {
            _plugin_ins = atoi( v );
        }
        else if ( ! strcmp( s, ":plugin_outs" ) )
        {
            _plugin_outs = atoi( v );
        }
        else if ( ! strcmp( s, ":custom_data" ) )
        {
            if(!export_import_strip.empty())
            {
                std::string path = export_import_strip;

                std::size_t found = path.find_last_of("/\\");
                restore = (path.substr(0, found));
                restore += "/";
                restore += v;
            }
            else
            {
                restore = project_directory;
                restore += "/";
                restore += v;
                _project_file = restore;
            }
        }
    }

    DMESSAGE("Path = %s", s_vst3_path.c_str());

    Module::Picked picked = { Type_VST3, s_unique_id, 0, s_vst3_path };

    if ( !load_plugin( picked ) )
    {
        fl_alert( "Could not load VST3 plugin %s", s_vst3_path.c_str() );
        return;
    }

    Module::set( e );

    if (!restore.empty())
    {
        restore_VST3_plugin_state(restore);
    }
}

#endif  // VST3_SUPPORT