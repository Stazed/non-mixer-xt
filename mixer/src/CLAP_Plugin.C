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
 * File:   CLAP_Plugin.C
 * Author: sspresto
 * 
 * Created on November 1, 2023, 7:10 PM
 */

#ifdef CLAP_SUPPORT

#include "CLAP_Plugin.H"
#include "Clap_Discovery.H"

#include "Chain.H"

#include "../../nonlib/dsp.h"

class Chain;    // forward declaration

CLAP_Plugin::CLAP_Plugin() : Plugin_Module( )
{
    init();
}


CLAP_Plugin::~CLAP_Plugin()
{
    _plugin->deactivate(_plugin);
    _plugin->destroy(_plugin);
}

bool
CLAP_Plugin::load_plugin ( Module::Picked picked )
{
    //	close();

    _entry = entry_from_CLAP_file(picked.uri);
    if (!_entry)
    {
        WARNING("Clap_entry returned a nullptr = %s", picked.uri);
        return false;
    }

    if( !_entry->init(picked.uri) )
    {
        WARNING("Clap_entry cannot initialize = %s", picked.uri);
        return false;
    }

    _factory = static_cast<const clap_plugin_factory *> (
            _entry->get_factory(CLAP_PLUGIN_FACTORY_ID));

    if (!_factory)
    {
        WARNING("Plugin factory is null %s", picked.uri);
        return false;
    }

    auto count = _factory->get_plugin_count(_factory);
    if (picked.unique_id >= count)
    {
        WARNING("Bad plug-in index = %d: count = %d", picked.unique_id, count);
        return false;
    }

    _descriptor = _factory->get_plugin_descriptor(_factory, picked.unique_id);
    if (!_descriptor)
    {
        WARNING("No plug-in descriptor.", picked.unique_id);
        return false;
    }

    base_label(_descriptor->name);

    if (!clap_version_is_compatible(_descriptor->clap_version))
    {
        WARNING("Incompatible CLAP version: %d"
                " plug-in is %d.%d.%d, host is %d.%d.%d.", picked.unique_id,
                _descriptor->clap_version.major,
                _descriptor->clap_version.minor,
                _descriptor->clap_version.revision,
                CLAP_VERSION.major,
                CLAP_VERSION.minor,
                CLAP_VERSION.revision);
        return false;
    }

    _host = clap_discovery::createCLAPInfoHost();

    _host->host_data        = this;
    _host->get_extension    = get_extension;
    _host->request_restart  = request_restart;
    _host->request_process  = request_process;
    _host->request_callback = request_callback;

    _plugin = _factory->create_plugin(_factory, _host, _descriptor->id);

    if( !_plugin->init(_plugin) )
    {
        WARNING("Cannot initialize plugin = %s", _descriptor->name);
        return false;
    }
    
    create_audio_ports();
    create_control_ports();
    create_note_ports();
    
    process_reset();
    
    if(!_plugin_ins)
        is_zero_input_synth(true);

    return true;   // FIXME obviously
}

bool
CLAP_Plugin::configure_inputs ( int n )
{
 //   unsigned int inst = _idata->handle.size();
    
    /* The synth case - no inputs and JACK module has one */
    if( ninputs() == 0 && n == 1)
    {
        _crosswire = false;
    }
    else if ( ninputs() != n )
    {
        _crosswire = false;

        if ( n != ninputs() )
        {
            if ( 1 == n && plugin_ins() > 1 )
            {
                DMESSAGE( "Cross-wiring plugin inputs" );
                _crosswire = true;

                audio_input.clear();

                for ( int i = n; i--; )
                    audio_input.push_back( Port( this, Port::INPUT, Port::AUDIO ) );
            }
#if 0
            else if ( n >= plugin_ins() &&
                      ( plugin_ins() == 1 && plugin_outs() == 1 ) )
            {
                DMESSAGE( "Running multiple instances of plugin" );

                audio_input.clear();
                audio_output.clear();

                for ( int i = n; i--; )
                {
                    add_port( Port( this, Port::INPUT, Port::AUDIO ) );
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO ) );
                }

                inst = n;
            }
#endif
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
    }

    if ( loaded() )
    {
        bool b = bypass();

        // FIXME instances
    //    if ( inst != _idata->handle.size() )
    //    {
            if ( !b )
                deactivate();
            
          //  if ( plugin_instances( inst ) )
          //      instances( inst );
          //  else
          //      return false;
            
            if ( !b )
                activate();
      //  }
    }

    return true;
}


void
CLAP_Plugin::handle_port_connection_change ( void )
{
    if ( loaded() )
    {
        _audio_ins.channel_count = plugin_ins();
        _audio_outs.channel_count = plugin_outs();

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
CLAP_Plugin::handle_chain_name_changed ( void )
{
    
}

void
CLAP_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    
}

void
CLAP_Plugin::resize_buffers ( nframes_t buffer_size )
{
    
}

void
CLAP_Plugin::set_input_buffer ( int n, void *buf )
{
    _audio_in_buffers[n] = (float*) buf;
    _audio_ins.data32 = _audio_in_buffers;

#if 0   // FIXME instances
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
    {
        h = _idata->handle[0];
    }

    for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
             LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[i].Types ) )
        {
            if ( n-- == 0 )
                _idata->lv2.descriptor->connect_port( h, i, (float*)buf );
        }
    }
#endif
}

void
CLAP_Plugin::set_output_buffer ( int n, void *buf )
{
    _audio_out_buffers[n] = (float*) buf;
    _audio_outs.data32 = _audio_out_buffers;

#if 0   // FIXME instances
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
        h = _idata->handle[0];

    for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
            LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[i].Types ) )
        {
            if ( n-- == 0 )
                _idata->lv2.descriptor->connect_port( h, i, (float*)buf );
        }
    }
#endif
}

bool
CLAP_Plugin::loaded ( void ) const
{
    if ( _plugin )
        return true;

    return false;

    //return _idata->handle.size() > 0 && ( _idata->lv2.rdf_data && _idata->lv2.descriptor);
}

bool
CLAP_Plugin::process_reset()
{
    deactivate();

    ::memset(&_audio_ins, 0, sizeof(_audio_ins));
    _audio_ins.channel_count = plugin_ins();
    _audio_ins.data32 = _audio_in_buffers;
    _audio_ins.data64 = nullptr;
    _audio_ins.constant_mask = 0;
    _audio_ins.latency = 0;

    ::memset(&_audio_outs, 0, sizeof(_audio_outs));
    _audio_outs.channel_count = plugin_outs();
    _audio_outs.data32 = _audio_out_buffers;
    _audio_outs.data64 = nullptr;
    _audio_outs.constant_mask = 0;
    _audio_outs.latency = 0;
        
    ::memset(&_process, 0, sizeof(_process));
    
    _process.audio_inputs  = &_audio_ins;
    _process.audio_inputs_count = 1;
    _process.audio_outputs = &_audio_outs;
    _process.in_events = nullptr;   // FIXME
    _process.out_events = nullptr;  // FIXME
 //   _process.in_events  = m_events_in.ins();
 //   _process.out_events = m_events_out.outs();
    
    _process.audio_outputs_count = 1;
    _process.transport = nullptr;   // FIXME
    _process.frames_count = 0;      // FIXME
    _process.steady_time = 0;
    
    activate();
    
    return true;
#if 0
    	qtractorClapPluginType *pType
		= static_cast<qtractorClapPluginType *> (m_pPlugin->type());
	if (pType == nullptr)
		return false;

	deactivate();

	m_srate = pAudioEngine->sampleRate();
	m_nframes = pAudioEngine->bufferSize();
	m_nframes_max = pAudioEngine->bufferSizeEx();

	::memset(&m_audio_ins, 0, sizeof(m_audio_ins));
	m_audio_ins.channel_count = pType->audioIns();

	::memset(&m_audio_outs, 0, sizeof(m_audio_outs));
	m_audio_outs.channel_count = pType->audioOuts();

	m_events_in.clear();
	m_events_out.clear();

	::memset(&m_process, 0, sizeof(m_process));
	if (pType->audioIns() > 0) {
		m_process.audio_inputs = &m_audio_ins;
		m_process.audio_inputs_count = 1;
	}
	if (pType->audioOuts() > 0) {
		m_process.audio_outputs = &m_audio_outs;
		m_process.audio_outputs_count = 1;
	}
	m_process.in_events  = m_events_in.ins();
	m_process.out_events = m_events_out.outs();
	m_process.frames_count = pAudioEngine->blockSize();
	m_process.steady_time = 0;
	m_process.transport = g_host.transport();

	activate();

	return true;
#endif
}

void
CLAP_Plugin::bypass ( bool v )
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
CLAP_Plugin::freeze_ports ( void )
{
    
}

void 
CLAP_Plugin::thaw_ports ( void )
{
    
}

void
CLAP_Plugin::configure_midi_inputs ()
{
    if(!note_input.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < note_input.size(); ++i )
    {
        if(!(note_input[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += note_input[i].name();

        DMESSAGE("CONFIGURE MIDI INPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Input, JACK::Port::MIDI );
        note_input[i].jack_port(jack_port);

        if( !note_input[i].jack_port()->activate() )
        {
            delete note_input[i].jack_port();
            note_input[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI IN port" );
            return;
        }
    }
}

void
CLAP_Plugin::configure_midi_outputs ()
{
    if(!note_output.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < note_output.size(); ++i )
    {
        if(!(note_output[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += note_output[i].name();

        DMESSAGE("CONFIGURE MIDI OUTPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Output, JACK::Port::MIDI );
        note_output[i].jack_port(jack_port);

        if( !note_output[i].jack_port()->activate() )
        {
            delete note_output[i].jack_port();
            note_output[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI OUT port" );
            return;
        }
    }
}

nframes_t
CLAP_Plugin::get_module_latency ( void ) const
{
    return 0;   // FIXME
}

void
CLAP_Plugin::process ( nframes_t nframes )
{
    handle_port_connection_change();

    if ( unlikely( bypass() ) )
    {
        /* If this is a mono to stereo plugin, then duplicate the input channel... */
        /* There's not much we can do to automatically support other configurations. */
        if ( ninputs() == 1 && noutputs() == 2 )
        {
            buffer_copy( (sample_t*)audio_output[1].buffer(), (sample_t*)audio_input[0].buffer(), nframes );
        }

        _latency = 0;
    }
    else
    {
        if (!_plugin)
            return;

        if (!_activated)
            return;
        
        if (! _is_processing)
        {
           _is_processing = _plugin->start_processing(_plugin);
        }
        
        if (_is_processing)
        {
            _process.frames_count  = nframes;
           // _plugin->process(_plugin, &_process); // FIXME
            _process.steady_time += nframes;
        }
    }
#if 0
    if (!m_plugin)
        return;

    if (!m_activated)
        return;

    if (!m_processing && !m_sleeping)
    {
        plugin_params_flush();
        g_host.transportAddRef();
        m_processing = m_plugin->start_processing(m_plugin);
    }
    else if (m_processing && (m_sleeping || m_restarting))
    {
        m_plugin->stop_processing(m_plugin);
        m_processing = false;
        g_host.transportReleaseRef();
        if (m_plugin->reset && !m_restarting)
            m_plugin->reset(m_plugin);
    }

    if (m_processing)
    {
        // Run main processing...
        m_audio_ins.data32 = ins;
        m_audio_outs.data32 = outs;
        m_events_out.clear();
        m_process.frames_count = nframes;
        
        m_plugin->process(m_plugin, &m_process);

        m_process.steady_time += nframes;
        m_events_in.clear();
        // Transfer parameter changes...
        process_params_out();
    }
#endif
}

const clap_plugin_entry_t*
CLAP_Plugin::entry_from_CLAP_file(const char *f)
{
    void *handle;
    int *iptr;

    handle = dlopen(f, RTLD_LOCAL | RTLD_LAZY);
    if (!handle)
    {
        DMESSAGE("dlopen failed on Linux: %s", dlerror());
	return nullptr;
    }

    iptr = (int *)dlsym(handle, "clap_entry");

    return (clap_plugin_entry_t *)iptr;
}

const void*
CLAP_Plugin::get_extension(const struct clap_host*, const char* eid)
{
    return nullptr; // FIXME
}

void
CLAP_Plugin::request_restart(const struct clap_host * host)
{
    // TODO
}

void
CLAP_Plugin::request_process(const struct clap_host * host)
{
    // TODO
}

void
CLAP_Plugin::request_callback(const struct clap_host * host)
{
    // TODO
}

void
CLAP_Plugin::create_audio_ports()
{
    _plugin_ins = 0;
    _plugin_outs = 0;

    const clap_plugin_audio_ports_t *audio_ports
        = static_cast<const clap_plugin_audio_ports_t *> (
                _plugin->get_extension(_plugin, CLAP_EXT_AUDIO_PORTS));

    if (audio_ports && audio_ports->count && audio_ports->get)
    {
        clap_audio_port_info info;
        const uint32_t nins = audio_ports->count(_plugin, true);    // true == input
        for (uint32_t i = 0; i < nins; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (audio_ports->get(_plugin, i, true, &info))
            {
                if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                {
                    for (unsigned ii = 0; ii < info.channel_count; ++ii)
                    {
                        add_port( Port( this, Port::INPUT, Port::AUDIO, info.name ) );
                        audio_input[_plugin_ins].hints.plug_port_index = ii;
                        _plugin_ins++;
                    }
                }
            }
        }

        const uint32_t nouts = audio_ports->count(_plugin, false);  // false == output
        for (uint32_t i = 0; i < nouts; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (audio_ports->get(_plugin, i, false, &info))
            {
                if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                {
                    for (unsigned ii = 0; ii < info.channel_count; ++ii)
                    {
                        add_port( Port( this, Port::OUTPUT, Port::AUDIO, info.name ) );
                        audio_output[_plugin_outs].hints.plug_port_index = ii;
                        _plugin_outs++;
                    }
                }
            }
        }
    }

    _audio_in_buffers = new float * [_plugin_ins];
    _audio_out_buffers = new float * [_plugin_outs];
}

void
CLAP_Plugin::create_control_ports()
{
    _control_ins = 0;
    _control_outs = 0;

    const clap_plugin_params *params
            = static_cast<const clap_plugin_params *> (
                    _plugin->get_extension(_plugin, CLAP_EXT_PARAMS));

    if (params && params->count && params->get_info)
    {
        const uint32_t nparams = params->count(_plugin);
        for (uint32_t i = 0; i < nparams; ++i)
        {
            Port::Direction d = Port::INPUT;

            clap_param_info param_info;
            ::memset(&param_info, 0, sizeof(param_info));
            if (params->get_info(_plugin, i, &param_info))
            {
                if (param_info.flags & CLAP_PARAM_IS_READONLY)
                {
                    d = Port::OUTPUT;
                    ++_control_outs;
                }
                else
                {
                    if (param_info.flags & CLAP_PARAM_IS_AUTOMATABLE)
                    {
                        d = Port::INPUT;
                        ++_control_ins;
                    }
                }

                Port p( this, d, Port::CONTROL, strdup(param_info.name) );
                
                /* Used for OSC path creation unique symbol */
                std::string osc_symbol = param_info.name;
                std::remove(osc_symbol.begin(), osc_symbol.end(), ' ');
                osc_symbol += std::to_string( param_info.id );
                
                p.set_symbol(osc_symbol.c_str());
                
                p.hints.ranged = true;
                p.hints.minimum = (float) param_info.min_value;
                p.hints.maximum = (float) param_info.max_value;
                p.hints.default_value = p.hints.minimum;
                
                if (param_info.flags & CLAP_PARAM_IS_STEPPED)
                {
                    if ( p.hints.ranged &&
                         0 == (int)p.hints.minimum &&
                         1 == (int)p.hints.maximum )
                        p.hints.type = Port::Hints::BOOLEAN;
                    else
                        p.hints.type = Port::Hints::INTEGER;
                }
                if (param_info.flags & CLAP_PARAM_IS_HIDDEN )
                {
                    p.hints.visible = false;
                }
                
                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );

                add_port( p );
                
                DMESSAGE( "Plugin has control port \"%s\" (default: %f)", param_info.name, p.hints.default_value );
            }
        }
    }
}

void
CLAP_Plugin::create_note_ports()
{
    _midi_ins = 0;
    _midi_outs = 0;

//    m_iMidiDialectIns = 0;
//    m_iMidiDialectOuts = 0;
    const clap_plugin_note_ports *note_ports
            = static_cast<const clap_plugin_note_ports *> (
                    _plugin->get_extension(_plugin, CLAP_EXT_NOTE_PORTS));
    if (note_ports && note_ports->count && note_ports->get)
    {
        clap_note_port_info info;
        const uint32_t nins = note_ports->count(_plugin, true);
        for (uint32_t i = 0; i < nins; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (note_ports->get(_plugin, i, true, &info))
            {
              //  if (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI)
              //          ++m_iMidiDialectIns;

                add_port( Port( this, Port::INPUT, Port::MIDI, strdup(info.name) ) );
                note_input[_midi_ins].hints.plug_port_index = i;
                ++_midi_ins;
            }
        }
        const uint32_t nouts = note_ports->count(_plugin, false);
        for (uint32_t i = 0; i < nouts; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (note_ports->get(_plugin, i, false, &info))
            {
              //  if (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI)
              //          ++m_iMidiDialectOuts;

                add_port( Port( this, Port::OUTPUT, Port::MIDI, strdup(info.name) ) );
                note_output[_midi_outs].hints.plug_port_index = i;
                ++_midi_outs;
            }
        }
    }
}

void
CLAP_Plugin::activate ( void )
{
    if ( !loaded() )
        return;

    if ( _activated )
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );

    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

#if 0   // FIXME instances
    if ( _idata->lv2.descriptor->activate )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->lv2.descriptor->activate( _idata->handle[i] );
        }
    }
#endif

    _activated = _plugin->activate(_plugin, (double) sample_rate(), buffer_size(), buffer_size() );
    
    *_bypass = 0.0f;

    if ( chain() )
        chain()->client()->unlock();
}

void
CLAP_Plugin::deactivate ( void )
{
    if ( !loaded() )
        return;

    if ( !_activated )
        return;

#if 0
    if (!m_sleeping && !force)
    {
        m_sleeping = true;
        return;
    }
#endif
    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

    _activated = false;

    _plugin->deactivate(_plugin);

#if 0   // FIXME instances
    if ( _idata->lv2.descriptor->deactivate )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->lv2.descriptor->deactivate( _idata->handle[i] );
        }
    }
#endif

    if ( chain() )
        chain()->client()->unlock();
}

void
CLAP_Plugin::add_port ( const Port &p )
{
    Module::add_port(p);

    if ( p.type() == Port::MIDI && p.direction() == Port::INPUT )
        note_input.push_back( p );
    else if ( p.type() == Port::MIDI && p.direction() == Port::OUTPUT )
        note_output.push_back( p );
}

void
CLAP_Plugin::init ( void )
{
    _plug_type = CLAP;
    _is_processing = false;
    _activated = false;

    Plugin_Module::init();

    // _project_directory = "";
}

void
CLAP_Plugin::get ( Log_Entry &e ) const
{
    
}

void
CLAP_Plugin::set ( Log_Entry &e )
{
    
}

#endif  // CLAP_SUPPORT
