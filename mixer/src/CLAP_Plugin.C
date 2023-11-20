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
#include "XTUtils.H"
#include "Time.h"

#include "../../nonlib/dsp.h"
#include "NonMixerPluginUI_X11Icon.h"
#include "CarlaClapUtils.H"

#include <unistd.h>    // getpid()
#include <pthread.h>
#include <FL/fl_ask.H>  // fl_alert()


static const uint X11Key_Escape = 9;
static const uint X11Key_W      = 25;

const unsigned char  EVENT_NOTE_OFF         = 0x80;
const unsigned char  EVENT_NOTE_ON          = 0x90;

static bool gErrorTriggered = false;
# if defined(__GNUC__) && (__GNUC__ >= 5) && ! defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
# endif
static pthread_mutex_t gErrorMutex = PTHREAD_MUTEX_INITIALIZER;
# if defined(__GNUC__) && (__GNUC__ >= 5) && ! defined(__clang__)
#  pragma GCC diagnostic pop
# endif

static int temporaryErrorHandler(Display*, XErrorEvent*)
{
    gErrorTriggered = true;
    return 0;
}

static constexpr const HostTimerDetails kTimerFallback   = { CLAP_INVALID_ID, 0, 0 };
static /*           */ HostTimerDetails kTimerFallbackNC = { CLAP_INVALID_ID, 0, 0 };


class Chain;    // forward declaration

CLAP_Plugin::CLAP_Plugin() : Plugin_Module( )
{
    init();

    log_create();
}


CLAP_Plugin::~CLAP_Plugin()
{
    if (_x_is_visible)
    {
        hide_custom_ui();
    }

    Fl::remove_timeout(&CLAP_Plugin::parameter_update, this);

    clearParamInfos();
    _plugin->deactivate(_plugin);

    if ( m_gui )
    {
        if ( m_bEditorCreated )
            m_gui->destroy(_plugin);

        m_gui = nullptr;
    }

    if (_plugin) 
    {
        _plugin->destroy(_plugin);
        _plugin = nullptr;
    }
    
    m_params = nullptr;
    m_timer_support = nullptr;
    m_posix_fd_support = nullptr;
    m_state = nullptr;
    m_note_names = nullptr;

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

    for ( unsigned int i = 0; i < note_input.size(); ++i )
    {
        if(!(note_input[i].type() == Port::MIDI))
            continue;

        if(note_input[i].jack_port())
        {
            note_input[i].disconnect();
            note_input[i].jack_port()->shutdown();
            delete note_input[i].jack_port();
        }
    } 
    for ( unsigned int i = 0; i < note_output.size(); ++i )
    {
        if(!(note_output[i].type() == Port::MIDI))
            continue;

        if(note_output[i].jack_port())
        {
            note_output[i].disconnect();
            note_output[i].jack_port()->shutdown();
            delete note_output[i].jack_port();
        }
    }

    note_output.clear();
    note_input.clear();

    if ( _last_chunk )
        std::free(_last_chunk);

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
}

bool
CLAP_Plugin::load_plugin ( Module::Picked picked )
{
    _clap_path = picked.clap_path;
    _clap_id = picked.s_unique_id;

    _entry = entry_from_CLAP_file(_clap_path.c_str());
    if (!_entry)
    {
        WARNING("Clap_entry returned a nullptr = %s", _clap_path.c_str());
        return false;
    }

    if( !_entry->init(_clap_path.c_str()) )
    {
        WARNING("Clap_entry cannot initialize = %s", _clap_path.c_str());
        return false;
    }

    _factory = static_cast<const clap_plugin_factory *> (
            _entry->get_factory(CLAP_PLUGIN_FACTORY_ID));

    if (!_factory)
    {
        WARNING("Plugin factory is null %s", _clap_path.c_str());
        return false;
    }

    auto count = _factory->get_plugin_count(_factory);
    
    for (uint32_t pl = 0; pl < count; ++pl)
    {
        auto desc = _factory->get_plugin_descriptor(_factory, pl);
        
        if (strcmp (desc->id, _clap_id.c_str() ) == 0)
        {
            _descriptor = _factory->get_plugin_descriptor(_factory, pl);
            break;
        }
    }

    if (!_descriptor)
    {
        WARNING("No plug-in descriptor. %s", _clap_id.c_str());
        return false;
    }
    
    base_label(_descriptor->name);

    if (!clap_version_is_compatible(_descriptor->clap_version))
    {
        WARNING("Incompatible CLAP version: %s"
                " plug-in is %d.%d.%d, host is %d.%d.%d.", _clap_id.c_str(),
                _descriptor->clap_version.major,
                _descriptor->clap_version.minor,
                _descriptor->clap_version.revision,
                CLAP_VERSION.major,
                CLAP_VERSION.minor,
                CLAP_VERSION.revision);
        return false;
    }

    setup_host(&_host, this);

    _plugin = _factory->create_plugin(_factory, &_host, _descriptor->id);

    if( !_plugin->init(_plugin) )
    {
        WARNING("Cannot initialize plugin = %s", _descriptor->name);
        return false;
    }

    initialize_plugin();

    create_audio_ports();
    create_control_ports();
    create_note_ports();

    process_reset();

    if(!_plugin_ins)
        is_zero_input_synth(true);

    if( m_state )
        _use_custom_data = true;

    Fl::add_timeout( 0.06f, &CLAP_Plugin::parameter_update, this );

    return true;
}

void
CLAP_Plugin::setup_host ( clap_host *host, void *host_data )
{
    ::memset(host, 0, sizeof(clap_host));

    host->host_data = host_data;
    host->clap_version = CLAP_VERSION;
    host->name = PACKAGE;
    host->version = VERSION;
    host->vendor = "Non-Mixer-XT team";
    host->url = WEBSITE;
    host->get_extension = get_extension;
    host->request_restart = request_restart;
    host->request_process = request_process;
    host->request_callback = request_callback;
}

bool
CLAP_Plugin::plugin_instances ( unsigned int n )
{
#if 0
    if ( _idata->handle.size() > n )
    {
        for ( int i = _idata->handle.size() - n; i--; )
        {
            DMESSAGE( "Destroying plugin instance" );

            LV2_Handle h = _idata->handle.back();

            if ( _idata->lv2.descriptor->deactivate )
                _idata->lv2.descriptor->deactivate( h );
            if ( _idata->lv2.descriptor->cleanup )
                _idata->lv2.descriptor->cleanup( h );

            _idata->handle.pop_back();
        }
    }
    else if ( _idata->handle.size() < n )
    {
        for ( int i = n - _idata->handle.size(); i--; )
        {
            DMESSAGE( "Instantiating plugin... with sample rate %lu", (unsigned long)sample_rate());

            void* h;

            _lilv_instance = lilv_plugin_instantiate(_lilv_plugin,  sample_rate(), _idata->lv2.features);

            if ( ! _lilv_instance )
            {
                WARNING( "Failed to instantiate plugin" );
                return false;
            }
            else
            {
                h = _lilv_instance->lv2_handle;
                _idata->lv2.descriptor = _lilv_instance->lv2_descriptor;    // probably not necessary
            }

            DMESSAGE( "Instantiated: %p", h );

            _idata->handle.push_back( h );

            DMESSAGE( "Connecting control ports..." );

            int ij = 0;
            int oj = 0;

            for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
            {
                if ( LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[k].Types ) )
                {
                    if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                        _idata->lv2.descriptor->connect_port( h, k, (float*)control_input[ij++].buffer() );
                    else if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                        _idata->lv2.descriptor->connect_port( h, k, (float*)control_output[oj++].buffer() );
                }
                // we need to connect non audio/control ports to NULL
                else if ( ! LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) &&
                         !LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[k].Types ))
                    _idata->lv2.descriptor->connect_port( h, k, NULL );
            }

            // connect ports to magic bogus value to aid debugging.
            for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
                if ( LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                    _idata->lv2.descriptor->connect_port( h, k, (float*)0x42 );
        }
    }

    return true;
#endif
    return false;
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
    Module::handle_chain_name_changed();

    if ( ! chain()->strip()->group()->single() )
    {
        for ( unsigned int i = 0; i < note_input.size(); i++ )
        {
            if(!(note_input[i].type() == Port::MIDI))
                continue;

            if(note_input[i].jack_port())
            {
                note_input[i].jack_port()->trackname( chain()->name() );
                note_input[i].jack_port()->rename();
            }
        }
        for ( unsigned int i = 0; i < note_output.size(); i++ )
        {
            if(!(note_output[i].type() == Port::MIDI))
                continue;

            if(note_output[i].jack_port())
            {
                note_output[i].jack_port()->trackname( chain()->name() );
                note_output[i].jack_port()->rename();
            }
        }
    }
}

void
CLAP_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    deactivate();
    activate();
}

void
CLAP_Plugin::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );

    deactivate();
    activate();
}

void
CLAP_Plugin::set_input_buffer ( int n, void *buf )
{
    _audio_in_buffers[n] = (float*) buf;

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

    m_events_in.clear();
    m_events_out.clear();

    _position = 0;
    _bpm = 120.0f;
    _rolling = false;

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
    ::memset(&m_transport, 0, sizeof(m_transport));
    
    if ( audio_input.size() )
    {
        _process.audio_inputs  = &_audio_ins;
        _process.audio_inputs_count = 1;
    }

    if ( audio_output.size() )
    {
        _process.audio_outputs = &_audio_outs;
        _process.audio_outputs_count = 1;
    }

    _process.in_events  = m_events_in.ins();
    _process.out_events = m_events_out.outs();
    _process.transport = &m_transport;
    _process.frames_count = buffer_size();      // FIXME Check
    _process.steady_time = 0;
    
    _latency = get_module_latency();

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
CLAP_Plugin::process_jack_transport ( uint32_t nframes )
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

    if ( xport_changed )
    {
        if ( has_bbt )
        {
            const double positionBeats = static_cast<double>(pos.frame)
                            / (sample_rate() * 60 / pos.beats_per_minute);

            // Bar/ Beats
            m_transport.bar_start = std::round(CLAP_BEATTIME_FACTOR * pos.bar_start_tick);
            m_transport.bar_number = pos.bar - 1;
            m_transport.song_pos_beats = std::round(CLAP_BEATTIME_FACTOR * positionBeats);
            m_transport.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;

            // Tempo
            m_transport.tempo = pos.beats_per_minute;
            m_transport.flags |= CLAP_TRANSPORT_HAS_TEMPO;

            // Time Signature
            m_transport.tsig_num = static_cast<uint16_t>(pos.beats_per_bar + 0.5f);
            m_transport.tsig_denom = static_cast<uint16_t>(pos.beat_type + 0.5f);
            m_transport.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        }
        else
        {
            // Tempo
            m_transport.tempo = 120.0;
            m_transport.flags |= CLAP_TRANSPORT_HAS_TEMPO;

            // Time Signature
            m_transport.tsig_num = 4;
            m_transport.tsig_denom = 4;
            m_transport.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        }
    }

    // Update transport state to expected values for next cycle
    _position = rolling ? pos.frame + nframes : pos.frame;
    _bpm      = has_bbt ? pos.beats_per_minute : _bpm;
    _rolling  = rolling;
}

void
CLAP_Plugin::process_jack_midi_in ( uint32_t nframes, unsigned int port )
{
    /* Process any MIDI events from jack */
    if ( note_input[port].jack_port() )
    {
        void *buf = note_input[port].jack_port()->buffer( nframes );

        for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i)
        {
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, i);

            process_midi_in(ev.buffer, ev.size, ev.time, 0);
        }
    }
}

void
CLAP_Plugin::process_midi_in (
	unsigned char *data, unsigned int size,
	unsigned long offset, unsigned short port )
{
    const int midi_dialect_ins = m_iMidiDialectIns;

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

        // program change
        // after-touch
        if ((midi_dialect_ins > 0) &&
                (status == 0xc0 || status == 0xd0))
        {
            clap_event_midi ev;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_MIDI;
            ev.header.time = offset;
            ev.header.flags = 0;
            ev.header.size = sizeof(ev);
            ev.port_index = port;
            ev.data[0] = status | channel;
            ev.data[1] = key;
            ev.data[2] = 0;
            m_events_in.push(&ev.header);
            continue;
        }

        // check data size (#2)
        if (++i >= size)
                break;

        // channel value (normalized)
        const int value = (data[i] & 0x7f);

        // note on
        if (status == 0x90)
        {
            clap_event_note ev;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_NOTE_ON;
            ev.header.time = offset;
            ev.header.flags = 0;
            ev.header.size = sizeof(ev);
            ev.note_id = -1;
            ev.port_index = port;
            ev.key = key;
            ev.channel = channel;
            ev.velocity = value / 127.0;
            m_events_in.push(&ev.header);
        }
        else
        // note off
        if (status == 0x80)
        {
            clap_event_note ev;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_NOTE_OFF;
            ev.header.time = offset;
            ev.header.flags = 0;
            ev.header.size = sizeof(ev);
            ev.note_id = -1;
            ev.port_index = port;
            ev.key = key;
            ev.channel = channel;
            ev.velocity = value / 127.0;
            m_events_in.push(&ev.header);
        }
        else
        // key pressure/poly.aftertouch
        // control-change
        // pitch-bend
        if ((midi_dialect_ins > 0) &&
                (status == 0xa0 || status == 0xb0 || status == 0xe0))
        {
            clap_event_midi ev;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_MIDI;
            ev.header.time = offset;
            ev.header.flags = 0;
            ev.header.size = sizeof(ev);
            ev.port_index = port;
            ev.data[0] = status | channel;
            ev.data[1] = key;
            ev.data[2] = value;
            m_events_in.push(&ev.header);
        }
    }
}

void
CLAP_Plugin::process_jack_midi_out ( uint32_t nframes, unsigned int port )
{
    void* buf = NULL;

    if ( note_output[port].jack_port() )
    {
        buf = note_output[port].jack_port()->buffer( nframes );
        jack_midi_clear_buffer(buf);

        EventList& events_out = CLAP_Plugin::events_out ();

        const uint32_t nevents = events_out.size();
        for (uint32_t i = 0; i < nevents; ++i)
        {
            const clap_event_header *eh
              = events_out.get(i);
            if (eh)
            {
                switch (eh->type)
                {
                case CLAP_EVENT_NOTE_ON:
                {
                    const clap_event_note *en
                            = reinterpret_cast<const clap_event_note *> (eh);
                    if (en)
                    {
                        unsigned char  midi_note[3];
                        midi_note[0] = EVENT_NOTE_ON + en->channel;
                        midi_note[1] = en->key;
                        midi_note[2] = en->velocity;

                        size_t size = 3;
                        int nBytes = static_cast<int> (size);
    
                        int ret =  jack_midi_event_write(buf, en->header.time,
                                (jack_midi_data_t*) &midi_note[0], nBytes);

                        if ( ret )
                            WARNING("Jack MIDI note off error = %d", ret);
                    }
                    break;
                }
                case CLAP_EVENT_NOTE_OFF:
                {
                    const clap_event_note *en
                            = reinterpret_cast<const clap_event_note *> (eh);
                    if (en)
                    {
                        unsigned char  midi_note[3];
                        midi_note[0] = EVENT_NOTE_OFF + en->channel;
                        midi_note[1] = en->key;
                        midi_note[2] = en->velocity;

                        size_t size = 3;
                        int nBytes = static_cast<int> (size);
                        int ret =  jack_midi_event_write(buf, en->header.time,
                                (jack_midi_data_t*) &midi_note[0], nBytes);

                        if ( ret )
                            WARNING("Jack MIDI note off error = %d", ret);
                    }
                    break;
                }
                case CLAP_EVENT_MIDI:
                {
                    const clap_event_midi *em
                            = reinterpret_cast<const clap_event_midi *> (eh);
                    if (em)
                    {
                        int ret =  jack_midi_event_write(buf, em->header.time,
                                (jack_midi_data_t*) &em->data[0], sizeof(em->data));

                        if ( ret )
                            WARNING("Jack MIDI write error = %d", ret);
                    }
                    break;
                }
                }
            }
        }
    }
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
    Module::freeze_ports();

    for ( unsigned int i = 0; i < note_input.size(); ++i )
    {
        if(!(note_input[i].type() == Port::MIDI))
            continue;

        if(note_input[i].jack_port())
        {
            note_input[i].jack_port()->freeze();
            note_input[i].jack_port()->shutdown();
        }
    }

    for ( unsigned int i = 0; i < note_output.size(); ++i )
    {
        if(!(note_output[i].type() == Port::MIDI))
            continue;

        if(note_output[i].jack_port())
        {
            note_output[i].jack_port()->freeze();
            note_output[i].jack_port()->shutdown();
        }
    }
}

void 
CLAP_Plugin::thaw_ports ( void )
{
    Module::thaw_ports();

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for ( unsigned int i = 0; i < note_input.size(); ++i )
    {   
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        if(!(note_input[i].type() == Port::MIDI))
            continue;

        if(note_input[i].jack_port())
        {
            note_input[i].jack_port()->client( chain()->client() );
            note_input[i].jack_port()->trackname( trackname );
            note_input[i].jack_port()->thaw();
        }
    }

    for ( unsigned int i = 0; i < note_output.size(); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        if(!(note_output[i].type() == Port::MIDI))
            continue;

        if(note_output[i].jack_port())
        {
            note_output[i].jack_port()->client( chain()->client() );
            note_output[i].jack_port()->trackname( trackname );
            note_output[i].jack_port()->thaw();
        }
    }
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
    if ( _activated )
        return 0;

    if (_plugin )
    {
        const clap_plugin_latency *latency
            = static_cast<const clap_plugin_latency *> (
                _plugin->get_extension(_plugin, CLAP_EXT_LATENCY));

        if (latency && latency->get)
            return latency->get(_plugin);
    }

    return 0;
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
            plugin_params_flush();
           _is_processing = _plugin->start_processing(_plugin);
        }
        
        if (_is_processing)
        {
            process_jack_transport( nframes );

            for( unsigned int i = 0; i < note_input.size(); ++i )
            {
                /* JACK MIDI in to plugin MIDI in */
                process_jack_midi_in( nframes, i );
            }
            
            for ( unsigned int i = 0; i < note_output.size(); ++i)
            {
                /* Plugin to JACK MIDI out */
                process_jack_midi_out( nframes, i);
            }

            m_events_out.clear();
            _process.frames_count  = nframes;

            _plugin->process(_plugin, &_process);

            _process.steady_time += nframes;
            m_events_in.clear();

            // Transfer parameter changes...
            process_params_out();
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
        // We did not find the plugin from the snapshot path so lets try
        // a different path. The case is if the project was copied to a
        // different computer in which the plugins are installed in a different
        // location - i.e. - /usr/lib vs /usr/local/lib.
        std::string file(f);
        std::string restore;
        // Find the base plugin name
        std::size_t found = file.find_last_of("/\\");
        restore = file.substr(found);
        DMESSAGE("Restore = %s", restore.c_str());

        auto sp = clap_discovery::installedCLAPs();   // This to get paths
        for (const auto &q : sp)
        {
            std::string path = q.u8string().c_str();
            DMESSAGE("CLAP PLUG PATHS %s", path.c_str());

            // Find the clap path base name
            std::size_t found = path.find_last_of("/\\");
            std::string base = path.substr(found);

            // Compare the base names and if they match, then use the path
            if (strcmp( restore.c_str(), base.c_str() ) == 0 )
            {
                handle = dlopen(path.c_str(), RTLD_LOCAL | RTLD_LAZY);
                if(!handle)
                {
                    // If it still does not open then abandon
                    return nullptr;
                }
                else
                {
                    // We got a load, so we good to go
                    iptr = (int *)dlsym(handle, "clap_entry");
                    return (clap_plugin_entry_t *)iptr;
                }
            }
            else
            {
                // keep trying until all available paths are checked
                continue;
            }
        }

        // We never got a match
        DMESSAGE("dlopen failed on Linux: %s", dlerror());
	return nullptr;
    }

    // Found on the first try
    iptr = (int *)dlsym(handle, "clap_entry");

    return (clap_plugin_entry_t *)iptr;
}

const void*
CLAP_Plugin::get_extension(const clap_host* host, const char* ext_id)
{
    const CLAP_Plugin *host_data = static_cast<const CLAP_Plugin *> (host->host_data);
    if (host_data)
    {
        DMESSAGE("Host get_extension(%p, \"%s\")", host_data, ext_id);

        if (::strcmp(ext_id, CLAP_EXT_GUI) == 0)
                return &host_data->g_host_gui;
        else
        if (::strcmp(ext_id, CLAP_EXT_TIMER_SUPPORT) == 0)
                return &host_data->g_host_timer_support;
        else
        if (::strcmp(ext_id, CLAP_EXT_STATE) == 0)
                return &host_data->g_host_state;
        else
        if (::strcmp(ext_id, CLAP_EXT_PARAMS) == 0)
                return &host_data->g_host_params;
        else
        if (::strcmp(ext_id, CLAP_EXT_AUDIO_PORTS) == 0)
                return &host_data->g_host_audio_ports;
        else
        if (::strcmp(ext_id, CLAP_EXT_NOTE_PORTS) == 0)
                return &host_data->g_host_note_ports;
        else
        if (::strcmp(ext_id, CLAP_EXT_LATENCY) == 0)
                return &host_data->g_host_latency;
        else
        if (::strcmp(ext_id, CLAP_EXT_THREAD_CHECK) == 0)
                return &host_data->g_host_thread_check;
#if 0
        else
        if (::strcmp(ext_id, CLAP_EXT_LOG) == 0)
                return &host_data->g_host_log;
        else
        if (::strcmp(ext_id, CLAP_EXT_POSIX_FD_SUPPORT) == 0)
                return &host_data->g_host_posix_fd_support;
        else
        if (::strcmp(ext_id, CLAP_EXT_THREAD_POOL) == 0)
                return &host_data->g_host_thread_pool;
        else
        if (::strcmp(ext_id, CLAP_EXT_NOTE_NAME) == 0)
                return &host_data->g_host_note_name;
#endif
    }
    return nullptr;
}

void
CLAP_Plugin::request_restart(const struct clap_host * host)
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);

    if (pImpl)
        pImpl->plugin_request_restart();

    DMESSAGE("Request restart");
}

void
CLAP_Plugin::plugin_request_restart ()
{
    _plug_request_restart = true;
}

void
CLAP_Plugin::request_process(const struct clap_host * host)
{
    DMESSAGE("Request process");
    // TODO
}

void
CLAP_Plugin::request_callback(const struct clap_host * host)
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);

    if (pImpl)
        pImpl->plugin_request_callback();

    DMESSAGE("Request callback");
}

void
CLAP_Plugin::plugin_request_callback()
{    
    _plug_needs_callback = true;
}

/**
 Adds pair to unordered maps, m_param_infos >> (id, *clap_param_info)
 The map is used to look up any parameter by id number which is saved
 by the parameter port when created.
 
 Currently not use is also m_param_ids >> (count, id)
 */
void
CLAP_Plugin::addParamInfos (void)
{
    if (m_params && m_params->count && m_params->get_info)
    {
        const uint32_t nparams = m_params->count(_plugin);
        for (uint32_t i = 0; i < nparams; ++i)
        {
            clap_param_info *param_info = new clap_param_info;
            ::memset(param_info, 0, sizeof(clap_param_info));
            if (m_params->get_info(_plugin, i, param_info))
            {
                std::pair<clap_id, const clap_param_info *> infos ( param_info->id, param_info );
                m_param_infos.insert(infos);
            }
        }
    }
}

void
CLAP_Plugin::clearParamInfos (void)
{
    for (auto i : m_param_infos)
    {
        delete i.second;
    }

    m_param_infos.clear();
    m_paramIds.clear();
}

// Instance parameters initializer.
void
CLAP_Plugin::addParams (void)
{
    create_control_ports();
}

void
CLAP_Plugin::clearParams (void)
{
    m_paramIds.clear();
    m_paramValues.clear();

    destroy_connected_controller_module();

    for (unsigned i = 0; i < control_input.size(); ++i)
    {
        // if it is NOT the bypass then delete the buffer
        if ( strcmp(control_input[i].name(), "dsp/bypass") )
            delete (float*)control_input[i].buffer();
    }

    for (unsigned i = 0; i < control_output.size(); ++i)
    {
        delete (float*)control_output[i].buffer();
    }

    control_input.clear();
    control_output.clear();
}

void
CLAP_Plugin::rescan_parameters()
{
    deactivate();
    deleteEditor();  // parameter editor
    clearParams();
    clearParamInfos();
    addParamInfos();
    addParams();
    activate();
}

/**
 Adds a parameter value to m_events_in which is then processed by the plugin when
 process is running on each cycle. Essentially sends a parameter value change
 to plugin from Module_Parameter_Editor, OSC, or other automation.
 */
void
CLAP_Plugin::setParameter (
	clap_id id, double value )
{
    if (_plugin)
    {
        std::unordered_map<clap_id, const clap_param_info *>::const_iterator got
            = m_param_infos.find (id);

        if ( got == m_param_infos.end() )
        {
            DMESSAGE("Parameter Id not found = %d", id);
            return;
        }

        const clap_param_info *param_info = got->second;

        if (param_info)
        {
            clap_event_param_value ev;
            ::memset(&ev, 0, sizeof(ev));
            ev.header.time = 0;
            ev.header.type = CLAP_EVENT_PARAM_VALUE;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.flags = 0;
            ev.header.size = sizeof(ev);
            ev.param_id = param_info->id;
            ev.cookie = param_info->cookie;
            ev.port_index = 0;
            ev.key = -1;
            ev.channel = -1;
            ev.value = value;
            m_events_in.push(&ev.header);
        }
    }
}

/**
 Gets the current parameter value from the plugin by parameter ID.
 */
double
CLAP_Plugin::getParameter ( clap_id id ) const
{
    double value = 0.0;

    if (_plugin && m_params && m_params->get_value)
    {
#if 1
        m_params->get_value(_plugin, id, &value);
#else
        std::unordered_map<clap_id, const clap_param_info *>::const_iterator got
            = m_param_infos.find (id);

        if ( got == m_param_infos.end() )
        {
            DMESSAGE("Parameter Id not found = %d", id);
            return 0.0;
        }

        const clap_param_info *param_info = got->second;

        if (param_info)
            m_params->get_value(_plugin, param_info->id, &value);
#endif
    }
    return value;
}

// Parameters update methods.
void
CLAP_Plugin::updateParamValues(bool update_custom_ui)
{
    for ( unsigned int i = 0; i < control_input.size(); ++i)
    {
        float value = getParameter(control_input[i].hints.parameter_id);

        if( control_input[i].control_value() != value)
        {
            set_control_value(i, value, update_custom_ui);
        }
    }
}

void
CLAP_Plugin::initialize_plugin()
{
    m_params = static_cast<const clap_plugin_params *> (
        _plugin->get_extension(_plugin, CLAP_EXT_PARAMS));

    m_timer_support = static_cast<const clap_plugin_timer_support *> (
        _plugin->get_extension(_plugin, CLAP_EXT_TIMER_SUPPORT));
    m_posix_fd_support = static_cast<const clap_plugin_posix_fd_support *> (
        _plugin->get_extension(_plugin, CLAP_EXT_POSIX_FD_SUPPORT));

    m_gui = static_cast<const clap_plugin_gui *> (
        _plugin->get_extension(_plugin, CLAP_EXT_GUI));
    m_state	= static_cast<const clap_plugin_state *> (
        _plugin->get_extension(_plugin, CLAP_EXT_STATE));
    m_note_names = static_cast<const clap_plugin_note_name *> (
        _plugin->get_extension(_plugin, CLAP_EXT_NOTE_NAME));

    addParamInfos();
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
    unsigned long control_ins = 0;
    unsigned long control_outs = 0;

    const clap_plugin_params *params
            = static_cast<const clap_plugin_params *> (
                    _plugin->get_extension(_plugin, CLAP_EXT_PARAMS));

    if (params && params->count && params->get_info)
    {
        const uint32_t nparams = params->count(_plugin);
        for (uint32_t i = 0; i < nparams; ++i)
        {
            Port::Direction d = Port::INPUT;
            bool have_control_in = false;

            clap_param_info param_info;
            ::memset(&param_info, 0, sizeof(param_info));
            if (params->get_info(_plugin, i, &param_info))
            {
                if (param_info.flags & CLAP_PARAM_IS_READONLY)
                {
                    d = Port::OUTPUT;
                    ++control_outs;
                }
                else
                {
                   // if (param_info.flags & CLAP_PARAM_IS_AUTOMATABLE)
                   // {
                        d = Port::INPUT;
                        ++control_ins;
                        have_control_in = true;
                    //    DMESSAGE("Control ins = %u", _control_ins);
                   // }
                }

                Port p( this, d, Port::CONTROL, strdup(param_info.name) );
                
                /* Used for OSC path creation unique symbol */
                std::string osc_symbol = param_info.name;
                std::remove(osc_symbol.begin(), osc_symbol.end(), ' ');
                osc_symbol += std::to_string( i );
                
                p.set_symbol(osc_symbol.c_str());
                
                p.hints.ranged = true;
                p.hints.minimum = (float) param_info.min_value;
                p.hints.maximum = (float) param_info.max_value;
                p.hints.default_value = (float) param_info.default_value;
                p.hints.parameter_id = param_info.id;
                
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

                p.hints.plug_port_index = i;

                add_port( p );

                // Cache the port ID and index for easy lookup - only _control_ins
                if (have_control_in)
                {
                   // DMESSAGE( "Control input port \"%s\" ID %u", param_info.name, p.hints.parameter_id );
                    std::pair<int, unsigned long> prm ( int(p.hints.parameter_id), control_ins - 1 );
                    m_paramIds.insert(prm);
                }

               // DMESSAGE( "Plugin has control port \"%s\" (default: %f)", param_info.name, p.hints.default_value );
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
}

void
CLAP_Plugin::create_note_ports()
{
    _midi_ins = 0;
    _midi_outs = 0;

    m_iMidiDialectIns = 0;
    m_iMidiDialectOuts = 0;
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
                if (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI)
                        ++m_iMidiDialectIns;

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
                if (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI)
                        ++m_iMidiDialectOuts;

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

    *_bypass = 0.0f;

    if ( ! _activated )
    {
        _activated = _plugin->activate(_plugin, (double) sample_rate(), buffer_size(), buffer_size() );
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
CLAP_Plugin::deactivate ( void )
{
    if ( !loaded() )
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

   if ( _activated )
   {
       _activated = false;
        _plugin->deactivate(_plugin);
   }

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
    _plug_needs_callback = false;
    _plug_request_restart = false;

    m_bEditorCreated = false;
    m_bEditorVisible = false;
    m_params_flush = false;

    m_params = nullptr;
    m_timer_support = nullptr;
    m_posix_fd_support = nullptr;
    m_gui = nullptr;
    m_state = nullptr;
    m_note_names = nullptr;

    // X window stuff
    _x_display = nullptr;
    _x_host_window = 0;
    _x_child_window = 0;
    _x_child_window_configured = false;
    _x_child_window_monitoring = false; // _x_child_window_monitoring(isResizable || canMonitorChildren) // FIXME
    _x_is_visible = false;
    _x_first_show = true;
    _x_set_size_called_at_least_Once = false;
    _x_is_idling = false;
    _x_is_resizable = false;
    _is_floating = false;
    _x_event_proc = nullptr;
    
    _last_chunk = nullptr;
    _project_file = "";

    Plugin_Module::init();
}

// Plugin parameters flush.
void
CLAP_Plugin::plugin_params_flush (void)
{
    if (!_plugin)
            return;

    if (!m_params_flush || _is_processing)
            return;

    m_params_flush = false;

    m_events_in.clear();
    m_events_out.clear();

    if (m_params && m_params->flush)
    {
        m_params->flush(_plugin, m_events_in.ins(), m_events_out.outs());
        process_params_out();
        m_events_out.clear();
    }
}

// Transfer parameter changes...
void
CLAP_Plugin::process_params_out (void)
{
    const uint32_t nevents = m_events_out.size();
    for (uint32_t i = 0; i < nevents; ++i)
    {
        const clap_event_header *eh = m_events_out.get(i);

        if (eh && (
                eh->type == CLAP_EVENT_PARAM_VALUE ||
                eh->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                eh->type == CLAP_EVENT_PARAM_GESTURE_END))
        {
            m_params_out.push(eh);
        }
    }
}

/**
 Callback for idle interface
 Update the Module_Parameter_Editor values from the Custom UI
 */
void 
CLAP_Plugin::parameter_update ( void *v )
{
    ((CLAP_Plugin*)v)->update_parameters();
}

// Update the Module_Parameter_Editor from custom UI changes.
void
CLAP_Plugin::update_parameters()
{
    EventList& params_out = CLAP_Plugin::params_out();
    const clap_event_header *eh = params_out.pop();
    for ( ; eh; eh = params_out.pop())
    {
        int param_id = CLAP_INVALID_ID;
        double value = 0.0;
        // Check if we're not middle of a gesture...
        if (eh->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
        {
            const clap_event_param_gesture *ev
                    = reinterpret_cast<const clap_event_param_gesture *> (eh);
            if (ev && ev->param_id != CLAP_INVALID_ID)
            {
                std::pair<int, double> prm ( int(ev->param_id), 0.0 );
                m_paramValues.insert(prm);
            }
        }
        else
        if (eh->type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            const clap_event_param_gesture *ev
                    = reinterpret_cast<const clap_event_param_gesture *> (eh);
            if (ev && ev->param_id != CLAP_INVALID_ID)
            {
                param_id = int(ev->param_id);

                std::unordered_map<int, double>::const_iterator got
                    = m_paramValues.find (param_id);

                if ( got == m_paramValues.end() )
                {
                    WARNING("GESTURE_END Id not found = %d", param_id);
                    param_id = CLAP_INVALID_ID;
                }
                else
                {
                    value = got->second;
                    m_paramValues.erase(param_id);
                }

              //  DMESSAGE("Gesture End Value = %f", (float) value);
            }
        }
        else
        if (eh->type == CLAP_EVENT_PARAM_VALUE)
        {
            const clap_event_param_value *ev
                    = reinterpret_cast<const clap_event_param_value *> (eh);
            if (ev && ev->param_id != CLAP_INVALID_ID)
            {
                param_id = ev->param_id;
                value = ev->value;

                std::unordered_map<int, double>::const_iterator got
                    = m_paramValues.find (param_id);

                // If we found the item, then replace it with new pair
                if ( !(got == m_paramValues.end()) )
                {
                    std::pair<int, double> prm ( int(ev->param_id), value );

                    m_paramValues.erase(param_id);
                    m_paramValues.insert(prm);

                    param_id = CLAP_INVALID_ID;
                }
            }
        }
        // Actually make the change...
        if (param_id != (int) CLAP_INVALID_ID)
        {
            std::unordered_map<int, unsigned long>::const_iterator got
                    = m_paramIds.find (param_id);

            if ( got == m_paramIds.end() )
            {
                // probably a control out - we don't do anything with these
                // DMESSAGE("Param Id not found = %d", param_id);
                continue;
            }

            unsigned long index = got->second;

            set_control_value( index, value, false );   // false means don't update custom UI
           // DMESSAGE("Send to Parameter Editor Index = %d: value = %f", index, (float) value);
        }
    }

    params_out.clear();

    if ( _plug_request_restart )
    {
        _plug_request_restart = false;
        deactivate();
        activate();
    }
    
    if ( _plug_needs_callback )
    {
        if (Thread::is( "UI" ))
        {
            _plug_needs_callback = false;
            _plugin->on_main_thread(_plugin);
        }
    }

    Fl::repeat_timeout( 0.06f, &CLAP_Plugin::parameter_update, this );
}

void
CLAP_Plugin::set_control_value(unsigned long port_index, float value, bool update_custom_ui)
{
    if( port_index >= control_input.size())
    {
        WARNING("Invalid Port Index = %d: Value = %f", port_index, value);
        return;
    }

    _is_from_custom_ui = !update_custom_ui;

    control_input[port_index].control_value(value);

    if (!dirty())
        set_dirty();
}

bool
CLAP_Plugin::try_custom_ui()
{
    if (!m_gui)
        return false;

    /* Toggle show and hide */
    if(m_bEditorCreated)
    {
        if (_x_is_visible)
        {
            hide_custom_ui();
            return true;
        }
        else
        {
            show_custom_ui();
            return true;
        }
    }

    clap_window w;
    w.api = CLAP_WINDOW_API_X11;

    if (!m_gui->is_api_supported(_plugin, w.api, false))
    {
        _is_floating = m_gui->is_api_supported(_plugin, w.api, true);
    }

    if (!m_gui->create(_plugin, w.api, _is_floating))
    {
        DMESSAGE("Could not create the plugin GUI.");
        return false;
    }

    /* We seem to have an accepted ui, so lets try to embed it in an X window */
     init_x();

     _x_child_window = getChildWindow();
     w.x11 = _x_host_window;

    if (_is_floating)
    {
        DMESSAGE("Using Floating Window");
        m_gui->set_transient(_plugin, &w);
        m_gui->suggest_title(_plugin, base_label());
    } else
    {
        if (!m_gui->set_parent(_plugin, &w))
        {
            DMESSAGE("Could not embed the plugin GUI.");
            m_gui->destroy(_plugin);
            return false;
        }
#if 0
        bool can_resize = false;
        uint32_t width  = 0;
        uint32_t height = 0;
        clap_gui_resize_hints hints;
        ::memset(&hints, 0, sizeof(hints));
        if (m_gui->can_resize)
            can_resize = m_gui->can_resize(_plugin);
        if (m_gui->get_resize_hints && !m_gui->get_resize_hints(_plugin, &hints))
            WARNING("Could not get the resize hints of the plugin GUI.");
        if (m_gui->get_size && !m_gui->get_size(_plugin, &width, &height))
            WARNING("Could not get the size of the plugin GUI.");

        if (width > 0 && (!hints.can_resize_horizontally || !can_resize))
        {
            // m_pEditorWidget->setFixedWidth(width);
        }
        if (height > 0 && (!hints.can_resize_vertically || !can_resize))
        {
            // m_pEditorWidget->setFixedHeight(height);
        }
        if (width > 1 && height > 1)
        {
            setSize(width, height, true, can_resize);
            // m_pEditorWidget->resize(width, height);
        }
#endif
    }

    DMESSAGE("GOT A CREATE");

    m_bEditorCreated = show_custom_ui();

    return m_bEditorCreated;
}

// Plugin GUI callbacks...
void
CLAP_Plugin::host_gui_resize_hints_changed (const clap_host *host )
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl) pImpl->plugin_gui_resize_hints_changed();
}

bool
CLAP_Plugin::host_gui_request_resize (
	const clap_host *host, uint32_t width, uint32_t height )
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    return (pImpl ? pImpl->plugin_gui_request_resize(width, height) : false);
}

bool
CLAP_Plugin::host_gui_request_show (const clap_host *host)
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    return (pImpl ? pImpl->plugin_gui_request_show() : false);
}

bool
CLAP_Plugin::host_gui_request_hide (const clap_host *host)
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    return (pImpl ? pImpl->plugin_gui_request_hide() : false);
}

void
CLAP_Plugin::host_gui_closed ( const clap_host *host, bool was_destroyed )
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl) pImpl->plugin_gui_closed(was_destroyed);
}

// Plugin GUI callbacks...
void
CLAP_Plugin::plugin_gui_resize_hints_changed (void)
{
    DMESSAGE("host_gui_resize_hints_changed");
    // TODO: ?...
    // The host should call get_resize_hints() again.
}

bool
CLAP_Plugin::plugin_gui_request_resize (
	uint32_t width, uint32_t height )
{
    DMESSAGE("Request Resize W = %u: H = %u", width, height);
    // Request the host to resize the client area to width, height.
    // Return true if the new size is accepted, false otherwise.
    // The host doesn't have to call set_size().
    setSize(width, height, true, _x_is_resizable);
    return true;
}

bool
CLAP_Plugin::plugin_gui_request_show (void)
{
    DMESSAGE("Request Show");
    
    return show_custom_ui();
    // Request the host to show the plugin gui.
    // Return true on success, false otherwise.
}

bool
CLAP_Plugin::plugin_gui_request_hide (void)
{
    DMESSAGE("Request Hide");
    return hide_custom_ui();
    // Request the host to hide the plugin gui.
    // Return true on success, false otherwise.
}

void
CLAP_Plugin::plugin_gui_closed ( bool was_destroyed )
{
    DMESSAGE("Gui closed");
    _x_is_visible = false;
    
    if (was_destroyed)
    {
        m_bEditorCreated = false;

        if (m_gui)
            m_gui->destroy(_plugin);
    }
    // The floating window has been closed, or the connection to the gui has been lost.
    // If was_destroyed is true, then the host must call clap_plugin_gui->destroy() to acknowledge
    // the gui destruction.
}

void
CLAP_Plugin::init_x()
{
    _x_child_window_monitoring = _x_is_resizable = isUiResizable();

    _x_display = XOpenDisplay(nullptr);
    NON_SAFE_ASSERT_RETURN(_x_display != nullptr,);

    const int screen = DefaultScreen(_x_display);

    XSetWindowAttributes attr;
    non_zeroStruct(attr);

    attr.event_mask = KeyPressMask|KeyReleaseMask|FocusChangeMask;

    if (_x_child_window_monitoring)
        attr.event_mask |= StructureNotifyMask|SubstructureNotifyMask;

    _x_host_window = XCreateWindow(_x_display, RootWindow(_x_display, screen),
                                0, 0, 300, 300, 0,
                                DefaultDepth(_x_display, screen),
                                InputOutput,
                                DefaultVisual(_x_display, screen),
                                CWBorderPixel|CWEventMask, &attr);

    NON_SAFE_ASSERT_RETURN(_x_host_window != 0,);

    XSetStandardProperties(_x_display, _x_host_window, label(), label(), None, NULL, 0, NULL);

    XGrabKey(_x_display, X11Key_Escape, AnyModifier, _x_host_window, 1, GrabModeAsync, GrabModeAsync);
    XGrabKey(_x_display, X11Key_W, AnyModifier, _x_host_window, 1, GrabModeAsync, GrabModeAsync);

    Atom wmDelete = XInternAtom(_x_display, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(_x_display, _x_host_window, &wmDelete, 1);

    const pid_t pid = getpid();
    const Atom _nwp = XInternAtom(_x_display, "_NET_WM_PID", False);
    XChangeProperty(_x_display, _x_host_window, _nwp, XA_CARDINAL, 32, PropModeReplace, (const uchar*)&pid, 1);

    const Atom _nwi = XInternAtom(_x_display, "_NET_WM_ICON", False);
    XChangeProperty(_x_display, _x_host_window, _nwi, XA_CARDINAL, 32, PropModeReplace, (const uchar*)sNonMixerX11Icon, sNonMixerX11IconSize);

    const Atom _wt = XInternAtom(_x_display, "_NET_WM_WINDOW_TYPE", False);

    // Setting the window to both dialog and normal will produce a decorated floating dialog
    // Order is important: DIALOG needs to come before NORMAL
    const Atom _wts[2] = {
        XInternAtom(_x_display, "_NET_WM_WINDOW_TYPE_DIALOG", False),
        XInternAtom(_x_display, "_NET_WM_WINDOW_TYPE_NORMAL", False)
    };
    XChangeProperty(_x_display, _x_host_window, _wt, XA_ATOM, 32, PropModeReplace, (const uchar*)&_wts, 2);
}

bool
CLAP_Plugin::isUiResizable() const
{
    return m_gui->can_resize(_plugin);
}

bool
CLAP_Plugin::show_custom_ui()
{
    if (_is_floating)
    {
        _x_is_visible = m_gui->show(_plugin);
        Fl::add_timeout( 0.03f, &CLAP_Plugin::custom_update_ui, this );
        return _x_is_visible;
    }

    if (_x_display == nullptr)
        return false;
    if (_x_host_window == 0)
        return false;

    if (_x_first_show)
    {
        if (const Window childWindow = getChildWindow())
        {
            if (! _x_set_size_called_at_least_Once)
            {
                int width = 0;
                int height = 0;

                XWindowAttributes attrs;
                non_zeroStruct(attrs);

                pthread_mutex_lock(&gErrorMutex);
                const XErrorHandler oldErrorHandler = XSetErrorHandler(temporaryErrorHandler);
                gErrorTriggered = false;

                if (XGetWindowAttributes(_x_display, childWindow, &attrs))
                {
                    width = attrs.width;
                    height = attrs.height;
                }

                XSetErrorHandler(oldErrorHandler);
                pthread_mutex_unlock(&gErrorMutex);

                if (width == 0 && height == 0)
                {
                    XSizeHints sizeHints;
                    non_zeroStruct(sizeHints);

                    if (XGetNormalHints(_x_display, childWindow, &sizeHints))
                    {
                        if (sizeHints.flags & PSize)
                        {
                            width = sizeHints.width;
                            height = sizeHints.height;
                        }
                        else if (sizeHints.flags & PBaseSize)
                        {
                            width = sizeHints.base_width;
                            height = sizeHints.base_height;
                        }
                    }
                }

                if (width > 1 && height > 1)
                    setSize(static_cast<uint>(width), static_cast<uint>(height), false, _x_is_resizable);
            }

            const Atom _xevp = XInternAtom(_x_display, "_XEventProc", False);

            pthread_mutex_lock(&gErrorMutex);
            const XErrorHandler oldErrorHandler(XSetErrorHandler(temporaryErrorHandler));
            gErrorTriggered = false;

            Atom actualType;
            int actualFormat;
            ulong nitems, bytesAfter;
            uchar* data = nullptr;

            XGetWindowProperty(_x_display, childWindow, _xevp, 0, 1, False, AnyPropertyType,
                               &actualType, &actualFormat, &nitems, &bytesAfter, &data);

            XSetErrorHandler(oldErrorHandler);
            pthread_mutex_unlock(&gErrorMutex);

            if (nitems == 1 && ! gErrorTriggered)
            {
                _x_event_proc = *reinterpret_cast<EventProcPtr*>(data);
                XMapRaised(_x_display, childWindow);
            }
        }
    }

    _x_is_visible = true;
    _x_first_show = false;

    XMapRaised(_x_display, _x_host_window);
    XSync(_x_display, False);

    m_gui->show(_plugin);

    Fl::add_timeout( 0.03f, &CLAP_Plugin::custom_update_ui, this );

    return true;
}

Window
CLAP_Plugin::getChildWindow() const
{
    NON_SAFE_ASSERT_RETURN(_x_display != nullptr, 0);
    NON_SAFE_ASSERT_RETURN(_x_host_window != 0, 0);

    Window rootWindow, parentWindow, ret = 0;
    Window* childWindows = nullptr;
    uint numChildren = 0;

    XQueryTree(_x_display, _x_host_window, &rootWindow, &parentWindow, &childWindows, &numChildren);

    if (numChildren > 0 && childWindows != nullptr)
    {
        ret = childWindows[0];
        XFree(childWindows);
    }

    return ret;
}

void
CLAP_Plugin::setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild)
{
    NON_SAFE_ASSERT_RETURN(_x_display != nullptr,);
    NON_SAFE_ASSERT_RETURN(_x_host_window != 0,);

    _x_set_size_called_at_least_Once = true;
    XResizeWindow(_x_display, _x_host_window, width, height);

    if (_x_child_window != 0 && resizeChild)
        XResizeWindow(_x_display, _x_child_window, width, height);

    if (! _x_is_resizable)
    {
        XSizeHints sizeHints;
        non_zeroStruct(sizeHints);

        sizeHints.flags      = PSize|PMinSize|PMaxSize;
        sizeHints.width      = static_cast<int>(width);
        sizeHints.height     = static_cast<int>(height);
        sizeHints.min_width  = static_cast<int>(width);
        sizeHints.min_height = static_cast<int>(height);
        sizeHints.max_width  = static_cast<int>(width);
        sizeHints.max_height = static_cast<int>(height);

        XSetNormalHints(_x_display, _x_host_window, &sizeHints);
    }

    if (forceUpdate)
        XSync(_x_display, False);
}

/**
 Callback for custom ui idle interface
 */
void 
CLAP_Plugin::custom_update_ui ( void *v )
{
    ((CLAP_Plugin*)v)->custom_update_ui_x();
}

/**
 The idle callback to update_custom_ui()
 */
void
CLAP_Plugin::custom_update_ui_x()
{
    // prevent recursion
    if (_x_is_idling) return;

    int nextWidth = 0;
    int nextHeight = 0;

    _x_is_idling = true;
    
    if (_is_floating)
        goto FLOATING;

    for (XEvent event; XPending(_x_display) > 0;)
    {
        XNextEvent(_x_display, &event);

        if (! _x_is_visible)
            continue;

        char* type = nullptr;

        switch (event.type)
        {
        case ConfigureNotify:
            NON_SAFE_ASSERT_CONTINUE(event.xconfigure.width > 0);
            NON_SAFE_ASSERT_CONTINUE(event.xconfigure.height > 0);

            if (event.xconfigure.window == _x_host_window)
            {
                const uint width  = static_cast<uint>(event.xconfigure.width);
                const uint height = static_cast<uint>(event.xconfigure.height);

                if (_x_child_window != 0)
                {
                    if (! _x_child_window_configured)
                    {
                        pthread_mutex_lock(&gErrorMutex);
                        const XErrorHandler oldErrorHandler = XSetErrorHandler(temporaryErrorHandler);
                        gErrorTriggered = false;

                        XSizeHints sizeHints;
                        non_zeroStruct(sizeHints);

                        if (XGetNormalHints(_x_display, _x_child_window, &sizeHints) && !gErrorTriggered)
                        {
                            XSetNormalHints(_x_display, _x_host_window, &sizeHints);
                        }
                        else
                        {
                            WARNING("Caught errors while accessing child window");
                            _x_child_window = 0;
                        }

                        _x_child_window_configured = true;
                        XSetErrorHandler(oldErrorHandler);
                        pthread_mutex_unlock(&gErrorMutex);
                    }

                    if (_x_child_window != 0)
                        XResizeWindow(_x_display, _x_child_window, width, height);
                }
            }
            else if (_x_child_window_monitoring && event.xconfigure.window == _x_child_window && _x_child_window != 0)
            {
                nextWidth = event.xconfigure.width;
                nextHeight = event.xconfigure.height;
            }
            break;

        case ClientMessage:
            type = XGetAtomName(_x_display, event.xclient.message_type);
            NON_SAFE_ASSERT_CONTINUE(type != nullptr);

            if (std::strcmp(type, "WM_PROTOCOLS") == 0)
            {
                _x_is_visible = false;
            }
            break;

        case KeyRelease:
            /* Escape key to close */
            if (event.xkey.keycode == X11Key_Escape )
            {
                _x_is_visible = false;
            }
            /* CTRL W to close */
            else if(event.xkey.keycode == X11Key_W)
            {
                if ((event.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) == (ControlMask))
                {
                    _x_is_visible = false;
                }
            }

            break;

        case FocusIn:
            if (_x_child_window == 0)
                _x_child_window = getChildWindow();
            if (_x_child_window != 0)
            {
                XWindowAttributes wa;
                non_zeroStruct(wa);

                if (XGetWindowAttributes(_x_display, _x_child_window, &wa) && wa.map_state == IsViewable)
                    XSetInputFocus(_x_display, _x_child_window, RevertToPointerRoot, CurrentTime);
            }
            break;
        }

        if (type != nullptr)
            XFree(type);
        else if (_x_event_proc != nullptr && event.type != FocusIn && event.type != FocusOut)
            _x_event_proc(&event);
    }

    if (nextWidth != 0 && nextHeight != 0 && _x_child_window != 0)
    {
        XSizeHints sizeHints;
        non_zeroStruct(sizeHints);

        if (XGetNormalHints(_x_display, _x_child_window, &sizeHints))
            XSetNormalHints(_x_display, _x_host_window, &sizeHints);

        XResizeWindow(_x_display, _x_host_window, static_cast<uint>(nextWidth), static_cast<uint>(nextHeight));
        XFlush(_x_display);
    }

FLOATING:

    _x_is_idling = false;

    for (LinkedList<HostTimerDetails>::Itenerator it = fTimers.begin2(); it.valid(); it.next())
    {
        const uint32_t currentTimeInMs = water::Time::getMillisecondCounter();
        HostTimerDetails& timer(it.getValue(kTimerFallbackNC));

        if (currentTimeInMs > timer.lastCallTimeInMs + timer.periodInMs)
        {
            timer.lastCallTimeInMs = currentTimeInMs;
            m_timer_support->on_timer(_plugin, timer.clapId);
        }
    }

    if(_x_is_visible)
    {
        Fl::repeat_timeout( 0.03f, &CLAP_Plugin::custom_update_ui, this );
    }
    else
    {
        hide_custom_ui();
    }
}

bool
CLAP_Plugin::hide_custom_ui()
{
    DMESSAGE("Closing Custom Interface");
    
    if (_is_floating)
    {
        _x_is_visible = false;
        Fl::remove_timeout(&CLAP_Plugin::custom_update_ui, this);
        return m_gui->hide(_plugin);
    }

    Fl::remove_timeout(&CLAP_Plugin::custom_update_ui, this);

    if(_x_display == nullptr)
        return false;
    if(_x_host_window == 0)
        return false;

    _x_is_visible = false;
    XUnmapWindow(_x_display, _x_host_window);
    XFlush(_x_display);

    return m_gui->hide(_plugin);
}

// Host Timer support callbacks...
bool
CLAP_Plugin::host_register_timer (
	const clap_host *host, uint32_t period_ms, clap_id *timer_id )
{
    return static_cast<CLAP_Plugin*>(host->host_data)->clapRegisterTimer(period_ms, timer_id);
}


bool
CLAP_Plugin::host_unregister_timer (
	const clap_host *host, clap_id timer_id )
{
    return static_cast<CLAP_Plugin*>(host->host_data)->clapUnregisterTimer(timer_id);
}

bool
CLAP_Plugin::clapRegisterTimer(const uint32_t periodInMs, clap_id* const timerId)
{
    DMESSAGE("ClapTimerRegister(%u, %p)", periodInMs, timerId);

    // NOTE some plugins wont have their timer extension ready when first loaded, so try again here
    if (m_timer_support == nullptr)
    {
        const clap_plugin_timer_support_t* const timerExt = static_cast<const clap_plugin_timer_support_t*>(
            _plugin->get_extension(_plugin, CLAP_EXT_TIMER_SUPPORT));

        if (timerExt != nullptr && timerExt->on_timer != nullptr)
            m_timer_support = timerExt;
    }

    NON_SAFE_ASSERT_RETURN(m_timer_support != nullptr, false);

    // FIXME events only driven as long as UI is open
    // CARLA_SAFE_ASSERT_RETURN(fUI.isCreated, false);

    const HostTimerDetails timer = 
    {
        fTimers.isNotEmpty() ? fTimers.getLast(kTimerFallback).clapId + 1 : 1,
        periodInMs,
        0
    };

    fTimers.append(timer);

    *timerId = timer.clapId;
    return true;
}

bool
CLAP_Plugin::clapUnregisterTimer(const clap_id timerId)
{
    DMESSAGE("ClapTimerUnregister(%u)", timerId);

    for (LinkedList<HostTimerDetails>::Itenerator it = fTimers.begin2(); it.valid(); it.next())
    {
        const HostTimerDetails& timer(it.getValue(kTimerFallback));

        if (timer.clapId == timerId)
        {
            fTimers.remove(it);
            return true;
        }
    }

    return false;
}

// Host Parameters callbacks...
void
CLAP_Plugin::host_params_rescan (
	const clap_host *host, clap_param_rescan_flags flags )
{
    DMESSAGE("host_params_rescan(%p, 0x%04x)", host, flags);

    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl) pImpl->plugin_params_rescan(flags);
}


void
CLAP_Plugin::host_params_clear (
	const clap_host *host, clap_id param_id, clap_param_clear_flags flags )
{
    DMESSAGE("host_params_clear(%p, %u, 0x%04x)", host, param_id, flags);

    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl) pImpl->plugin_params_clear(param_id, flags);
}


void
CLAP_Plugin::host_params_request_flush (
	const clap_host *host )
{
    DMESSAGE("host_params_request_flush(%p)", host);

    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl)
        pImpl->plugin_params_request_flush();
}

void
CLAP_Plugin::host_state_mark_dirty (
            const clap_host *host)
{
    DMESSAGE("GOT SET DIRTY");
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl)
        pImpl->set_dirty();
}

// Plugin Parameters callbacks...
void
CLAP_Plugin::plugin_params_rescan (
	clap_param_rescan_flags flags )
{
    DMESSAGE("host_params_rescan(0x%04x)", flags);
    if (_plugin == nullptr)
        return;

    if (flags & CLAP_PARAM_RESCAN_VALUES)
    {
        DMESSAGE("RESCAN VALUES");
        updateParamValues(false);   // false means do not update the custom UI
    }
    else
    if (flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_ALL))
    {
        DMESSAGE("RESCAN INFO & ALL");
        rescan_parameters();
        updateParamValues(false);   // false means do not update the custom UI
    }
}

void
CLAP_Plugin::plugin_params_clear (
	clap_id param_id, clap_param_clear_flags flags )
{
    if (_plugin == nullptr)
        return;

    if (!flags || param_id == CLAP_INVALID_ID)
        return;

    // We cannot delete individual parameters so rescan all and restart
    rescan_parameters();
    updateParamValues(false);   // false means do not update the custom UI
}

void
CLAP_Plugin::plugin_params_request_flush (void)
{
    m_params_flush = true;

//	plugin_params_flush();
}

// Host Audio Ports support callbacks...
bool
CLAP_Plugin::host_audio_ports_is_rescan_flag_supported (
	const clap_host * /* host */, uint32_t /* flag */)
{
    // Not supported
    DMESSAGE("Audio ports rescan support called");
    return false;
}

void
CLAP_Plugin::host_audio_ports_rescan (
	const clap_host * /* host */, uint32_t /* flags */)
{
    DMESSAGE("Audio ports rescan requested");
    // Not supported.
}

// Host Note Ports support callbacks...
uint32_t
CLAP_Plugin::host_note_ports_supported_dialects (
	const clap_host * /* host */)
{
    // Only MIDI 1.0 is scrictly supported.
    return CLAP_NOTE_DIALECT_MIDI;
}

void
CLAP_Plugin::host_note_ports_rescan (
	const clap_host * /* host */, uint32_t /* flags */)
{
    // Not supported.
    DMESSAGE("Host note ports rescan requested");
}

// Host Latency callbacks...
void
CLAP_Plugin::host_latency_changed (
	const clap_host *host )
{
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl) pImpl->plugin_latency_changed();
}

// Plugin Latency callbacks...
void
CLAP_Plugin::plugin_latency_changed (void)
{
    //FIXME todo
   // if (m_pPlugin)
   //     m_pPlugin->request_restart();
}

// Host thread-check callbacks...
bool
CLAP_Plugin::host_is_main_thread (
	const clap_host *host )
{
  //  DMESSAGE("Plugin called is main thread");
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl)
    {
        return pImpl->is_main_thread();
    }

    return false;
}

bool
CLAP_Plugin::host_is_audio_thread (
	const clap_host *host )
{
  //  DMESSAGE("Plugin called is audio thread");
    CLAP_Plugin *pImpl = static_cast<CLAP_Plugin *> (host->host_data);
    if (pImpl)
    {
        return pImpl->is_audio_thread();
    }

    return false;
}

bool
CLAP_Plugin::is_main_thread()
{
    return Thread::is( "UI" );
}

bool
CLAP_Plugin::is_audio_thread()
{
    return Thread::is ( "RT" );
}

void
CLAP_Plugin::save_CLAP_plugin_state(const std::string filename)
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
CLAP_Plugin::restore_CLAP_plugin_state(const std::string filename)
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

    const clap_istream_impl stream(data, size);
    if (m_state->load(_plugin, &stream))
    {
        updateParamValues(false);   // false means do not update the custom UI
    }
    else
    {
        fl_alert( "%s could not complete state restore of %s", base_label(), filename.c_str() );
    }

    free(data);
}

uint64_t
CLAP_Plugin::getState ( void** const dataPtr )
{
    if (!_plugin)
        return false;
    
    std::free(_last_chunk);

    clap_ostream_impl stream;
    if (m_state->save(_plugin, &stream))
    {
        *dataPtr = _last_chunk = stream.buffer;
        return stream.size;
    }
    else
    {
        *dataPtr = _last_chunk = nullptr;
        return 0;
    }

    return 0;
}

void
CLAP_Plugin::get ( Log_Entry &e ) const
{
    e.add( ":clap_plugin_path", _clap_path.c_str() );
    e.add( ":clap_plugin_id", _clap_id.c_str());
 
    /* these help us display the module on systems which are missing this plugin */
    e.add( ":plugin_ins", _plugin_ins );
    e.add( ":plugin_outs", _plugin_outs );

    if ( _use_custom_data  )
    {
        Module *m = (Module *) this;
        CLAP_Plugin *pm = static_cast<CLAP_Plugin *> (m);

        /* Export directory location */
        if(!export_import_strip.empty())
        {
            std::string path = export_import_strip;

            std::size_t found = path.find_last_of("/\\");
            path = (path.substr(0, found));

            std::string filename = pm->get_custom_data_location(path);

            pm->save_CLAP_plugin_state(filename);
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
                pm->save_CLAP_plugin_state(file);

                std::string base_file = file.substr(file.find_last_of("/\\") + 1);
                e.add( ":custom_data", base_file.c_str() );
            }
        }
    }

    Module::get( e );
}

void
CLAP_Plugin::set ( Log_Entry &e )
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

    std::string s_clap_path = "";
    std::string s_clap_id = "";
    
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":clap_plugin_path" ) )
        {
            s_clap_path = v;
        }
        else if ( ! strcmp( s, ":clap_plugin_id" ) )
        {
            s_clap_id =  v;
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

    DMESSAGE("Path = %s: ID = %s", s_clap_path.c_str(), s_clap_id.c_str());

    Module::Picked picked = { CLAP, strdup(s_clap_id.c_str()), 0, s_clap_path };

    if ( !load_plugin( picked ) )
    {
        // What to do - inform the user and ask if they want to delete?
        return;
    }

    Module::set( e );

    if (!restore.empty())
    {
        restore_CLAP_plugin_state(restore);
    }
}

#endif  // CLAP_SUPPORT

