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
#include "Time.h"
#include "CarlaClapUtils.H"

#include "../Chain.H"
#include "../../../nonlib/dsp.h"

#include <FL/fl_ask.H>  // fl_alert()
#include <dlfcn.h>      // dlopen, dlerror, dlsym

const unsigned char  EVENT_NOTE_OFF         = 0x80;
const unsigned char  EVENT_NOTE_ON          = 0x90;

static constexpr const HostTimerDetails kTimerFallback   = { CLAP_INVALID_ID, 0, 0 };
static /*           */ HostTimerDetails kTimerFallbackNC = { CLAP_INVALID_ID, 0, 0 };
const float F_DEFAULT_MSECS = 0.03f;


class Chain;    // forward declaration

CLAP_Plugin::CLAP_Plugin() :
    Plugin_Module(),
    _entry(nullptr),
    _factory(nullptr),
    _descriptor(nullptr),
    _clap_path(""),
    _clap_id(""),
    _last_chunk(nullptr),
    _position(0),
    _bpm(120.0f),
    _rolling(false),
    _is_processing(false),
    _activated(false),
    _plug_needs_callback(false),
    _plug_request_restart(false),
    _bEditorCreated(false),
    _X11_UI(nullptr),
    _x_is_visible(false),
    _is_floating(false),
    _x_is_resizable(false),
    _x_width(0),
    _x_height(0),
    _audio_ins(nullptr),
    _audio_outs(nullptr),
    _audio_in_buffers(nullptr),
    _audio_out_buffers(nullptr),
    _audioInBuses(0),
    _audioOutBuses(0),
    _plugin(nullptr),
    _params_flush(false),
    _params(nullptr),
    _timer_support(nullptr),
    _gui(nullptr),
    _state(nullptr),
    _project_file(""),
    _midi_ins(0),
    _midi_outs(0),
    _iMidiDialectIns(0),
    _iMidiDialectOuts(0)
{
    _plug_type = Type_CLAP;

    log_create();
}


CLAP_Plugin::~CLAP_Plugin()
{
    log_destroy();

    if (_x_is_visible)
    {
        hide_custom_ui();
    }

    Fl::remove_timeout(&CLAP_Plugin::parameter_update, this);

    clearParamInfos();

    if(_plugin)
        _plugin->deactivate(_plugin);

    if ( _gui )
    {
        if ( _bEditorCreated )
            _gui->destroy(_plugin);

        _gui = nullptr;
    }

    if (_plugin) 
    {
        _plugin->destroy(_plugin);
        _plugin = nullptr;
    }
    
    _params = nullptr;
    _timer_support = nullptr;
    _state = nullptr;

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

    if ( _audio_ins )
    {
        for(unsigned i = 0; i < _audioInBuses; i++)
        {
            delete[] _audio_ins[i].data32;
        }
        delete[] _audio_ins;
    }

    if ( _audio_outs )
    {
        for(unsigned i = 0; i < _audioOutBuses; i++)
        {
            delete[] _audio_outs[i].data32;
        }
        delete[] _audio_outs;
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
    _clap_path = picked.s_plug_path;
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

    if( _state )
        _use_custom_data = true;

    Fl::add_timeout( F_DEFAULT_MSECS, &CLAP_Plugin::parameter_update, this );

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
CLAP_Plugin::configure_inputs ( int n )
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
CLAP_Plugin::handle_port_connection_change ( void )
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
CLAP_Plugin::handle_sample_rate_change ( nframes_t /* sample_rate */ )
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
    _audio_in_buffers[n] = static_cast<float*>( buf );
}

void
CLAP_Plugin::set_output_buffer ( int n, void *buf )
{
    _audio_out_buffers[n] = static_cast<float*>( buf );
}

bool
CLAP_Plugin::loaded ( void ) const
{
    if ( _plugin )
        return true;

    return false;
}

bool
CLAP_Plugin::process_reset()
{
    deactivate();

    _events_in.clear();
    _events_out.clear();

    _position = 0;
    _bpm = 120.0f;
    _rolling = false;

    ::memset(&_process, 0, sizeof(_process));
    ::memset(&_transport, 0, sizeof(_transport));

    if ( audio_input.size() )
    {
        _process.audio_inputs  = (const clap_audio_buffer_t *) _audio_ins;
        _process.audio_inputs_count = _audioInBuses;
    }

    if ( audio_output.size() )
    {
        _process.audio_outputs = _audio_outs;
        _process.audio_outputs_count = _audioOutBuses;
    }

    _process.in_events  = _events_in.ins();
    _process.out_events = _events_out.outs();
    _process.transport = &_transport;
    _process.frames_count = buffer_size();
    _process.steady_time = 0;

    _latency = get_module_latency();

    activate();

    return true;
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
            _transport.bar_start = std::round(CLAP_BEATTIME_FACTOR * pos.bar_start_tick);
            _transport.bar_number = pos.bar - 1;
            _transport.song_pos_beats = std::round(CLAP_BEATTIME_FACTOR * positionBeats);
            _transport.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;

            // Tempo
            _transport.tempo = pos.beats_per_minute;
            _transport.flags |= CLAP_TRANSPORT_HAS_TEMPO;

            // Time Signature
            _transport.tsig_num = static_cast<uint16_t>(pos.beats_per_bar + 0.5f);
            _transport.tsig_denom = static_cast<uint16_t>(pos.beat_type + 0.5f);
            _transport.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        }
        else
        {
            // Tempo
            _transport.tempo = 120.0;
            _transport.flags |= CLAP_TRANSPORT_HAS_TEMPO;

            // Time Signature
            _transport.tsig_num = 4;
            _transport.tsig_denom = 4;
            _transport.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
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
    const int midi_dialect_ins = _iMidiDialectIns;

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
            _events_in.push(&ev.header);
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
            _events_in.push(&ev.header);
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
            _events_in.push(&ev.header);
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
            _events_in.push(&ev.header);
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

        CLAPIMPL::EventList& events_out = CLAP_Plugin::events_out ();

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
                                static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

                        if ( ret )
                            WARNING("Jack MIDI note on error = %d", ret);
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
                                static_cast<jack_midi_data_t*>( &midi_note[0] ), nBytes);

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
                                static_cast<const jack_midi_data_t*>( &em->data[0] ), sizeof(em->data));

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
CLAP_Plugin::clear_midi_vectors()
{
    note_input.clear();
    note_output.clear();
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
            buffer_copy( static_cast<sample_t*>( audio_output[1].buffer() ),
                    static_cast<sample_t*>( audio_input[0].buffer() ), nframes );
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

            _events_out.clear();
            _process.frames_count  = nframes;

            unsigned j = 0;
            for (unsigned i = 0; i < _audioInBuses; i++)
            {
                for (unsigned k = 0; k < _audio_ins[i].channel_count; k++)
                {
                    //DMESSAGE("III = %d: KKK = %d: JJJ = %d", i, k, j);
                    _audio_ins[i].data32[k] = _audio_in_buffers[j] ;
                    j++;
                }
            }

            j = 0;
            for (unsigned i = 0; i < _audioOutBuses; i++)
            {
                for (unsigned k = 0; k < _audio_outs[i].channel_count; k++)
                {
                    //DMESSAGE("III = %d: KKK = %d: JJJ = %d", i, k, j);
                    _audio_outs[i].data32[k] = _audio_out_buffers[j] ;
                    j++;
                }
            }

            _plugin->process(_plugin, &_process);

            _process.steady_time += nframes;
            _events_in.clear();

            // Transfer parameter changes...
            process_params_out();
        }
    }
}

const clap_plugin_entry_t*
CLAP_Plugin::entry_from_CLAP_file(const char *f)
{
    void *handle;
    int *iptr;

    handle = dlopen(f, RTLD_LOCAL | RTLD_LAZY);
    if (!handle)
    {
        /* We did not find the plugin from the snapshot path so lets try
           a different path. The case is if the project was copied to a
           different computer in which the plugins are installed in a different
           location - i.e. - /usr/lib vs /usr/local/lib.
        */
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
            found = path.find_last_of("/\\");
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
                    iptr = static_cast<int *>( dlsym(handle, "clap_entry") );
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
    iptr = static_cast<int *>( dlsym(handle, "clap_entry") );

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
        else
        if (::strcmp(ext_id, CLAP_EXT_LOG) == 0)
                return &host_data->g_host_log;
#if 0
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
 Adds pair to unordered maps, _param_infos >> (id, *clap_param_info)
 The map is used to look up any parameter by id number which is saved
 by the parameter port when created.
 */
void
CLAP_Plugin::addParamInfos (void)
{
    if (_params && _params->count && _params->get_info)
    {
        const uint32_t nparams = _params->count(_plugin);
        for (uint32_t i = 0; i < nparams; ++i)
        {
            clap_param_info *param_info = new clap_param_info;
            ::memset(param_info, 0, sizeof(clap_param_info));
            if (_params->get_info(_plugin, i, param_info))
            {
                std::pair<clap_id, const clap_param_info *> infos ( param_info->id, param_info );
                _param_infos.insert(infos);
            }
        }
    }
}

void
CLAP_Plugin::clearParamInfos (void)
{
    for (auto i : _param_infos)
    {
        delete i.second;
    }

    _param_infos.clear();
    _paramIds.clear();
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
    _paramIds.clear();
    _paramValues.clear();

    destroy_connected_controller_module();

    for (unsigned i = 0; i < control_input.size(); ++i)
    {
        // if it is NOT the bypass then delete the buffer
        if ( strcmp(control_input[i].name(), "dsp/bypass") )
            delete static_cast<float*>( control_input[i].buffer() );
    }

    for (unsigned i = 0; i < control_output.size(); ++i)
    {
        delete static_cast<float*>( control_output[i].buffer() );
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
 Adds a parameter value to _events_in which is then processed by the plugin when
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
            = _param_infos.find (id);

        if ( got == _param_infos.end() )
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
            _events_in.push(&ev.header);
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

    if (_plugin && _params && _params->get_value)
    {
#if 1
        _params->get_value(_plugin, id, &value);
#else
        std::unordered_map<clap_id, const clap_param_info *>::const_iterator got
            = _param_infos.find (id);

        if ( got == _param_infos.end() )
        {
            DMESSAGE("Parameter Id not found = %d", id);
            return 0.0;
        }

        const clap_param_info *param_info = got->second;

        if (param_info)
            _params->get_value(_plugin, param_info->id, &value);
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
    _params = static_cast<const clap_plugin_params *> (
        _plugin->get_extension(_plugin, CLAP_EXT_PARAMS));

    _timer_support = static_cast<const clap_plugin_timer_support *> (
        _plugin->get_extension(_plugin, CLAP_EXT_TIMER_SUPPORT));
//    m_posix_fd_support = static_cast<const clap_plugin_posix_fd_support *> (
//        _plugin->get_extension(_plugin, CLAP_EXT_POSIX_FD_SUPPORT));

    _gui = static_cast<const clap_plugin_gui *> (
        _plugin->get_extension(_plugin, CLAP_EXT_GUI));
    _state	= static_cast<const clap_plugin_state *> (
        _plugin->get_extension(_plugin, CLAP_EXT_STATE));
//    m_note_names = static_cast<const clap_plugin_note_name *> (
//        _plugin->get_extension(_plugin, CLAP_EXT_NOTE_NAME));

    addParamInfos();
}

void
CLAP_Plugin::create_audio_ports()
{
    _plugin_ins = 0;
    _plugin_outs = 0;
    _audioInBuses = 0;
    _audioOutBuses = 0;

    const clap_plugin_audio_ports_t *audio_ports
        = static_cast<const clap_plugin_audio_ports_t *> (
                _plugin->get_extension(_plugin, CLAP_EXT_AUDIO_PORTS));

    if (audio_ports && audio_ports->count && audio_ports->get)
    {
        clap_audio_port_info info;
        _audioInBuses = audio_ports->count(_plugin, true);    // true == input
        for (uint32_t i = 0; i < _audioInBuses; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (audio_ports->get(_plugin, i, true, &info))
            {
                _audioInChannels.push_back(info.channel_count);
                for (unsigned ii = 0; ii < info.channel_count; ++ii)
                {
                    add_port( Port( this, Port::INPUT, Port::AUDIO, info.name ) );
                    audio_input[_plugin_ins].hints.plug_port_index = ii;
                    _plugin_ins++;
                }
            }
        }

        _audioOutBuses = audio_ports->count(_plugin, false);  // false == output
        
     //   DMESSAGE("_audioOutBuses = %u", _audioOutBuses);
        for (uint32_t i = 0; i < _audioOutBuses; ++i)
        {
            ::memset(&info, 0, sizeof(info));
            if (audio_ports->get(_plugin, i, false, &info))
            {
                _audioOutChannels.push_back(info.channel_count);
               // DMESSAGE("Channel count = %u", info.channel_count);
                for (unsigned ii = 0; ii < info.channel_count; ++ii)
                {
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO, info.name ) );
                    audio_output[_plugin_outs].hints.plug_port_index = ii;
                    _plugin_outs++;
                }
            }
        }
    }

    _audio_in_buffers = new float * [_plugin_ins];
    _audio_out_buffers = new float * [_plugin_outs];

    if (_audioInBuses)
    {
        _audio_ins = new clap_audio_buffer_t[_audioInBuses];
        for(unsigned int i = 0; i < _audioInBuses; i++)
        {
            _audio_ins[i].channel_count = _audioInChannels[i];
            _audio_ins[i].data32 = new float *[_audioInChannels[i]]();
            _audio_ins[i].data64 = nullptr;
            _audio_ins[i].constant_mask = 0;
            _audio_ins[i].latency = 0;
        }
    }

    if (_audioOutBuses)
    {
        _audio_outs = new clap_audio_buffer_t[_audioOutBuses];
        for(unsigned int i = 0; i < _audioOutBuses; i++)
        {
            _audio_outs[i].channel_count = _audioOutChannels[i];
            _audio_outs[i].data32 = new float *[_audioOutChannels[i]]();
            _audio_outs[i].data64 = nullptr;
            _audio_outs[i].constant_mask = 0;
            _audio_outs[i].latency = 0;
        }
    }

    MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);
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

            clap_param_info param_info;
            ::memset(&param_info, 0, sizeof(param_info));
            if (params->get_info(_plugin, i, &param_info))
            {
                bool have_control_in = false;

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
                osc_symbol.erase(std::remove(osc_symbol.begin(), osc_symbol.end(), ' '), osc_symbol.end());
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
                    _paramIds.insert(prm);
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

    MESSAGE( "Plugin has %i control ins and %i control outs", control_ins, control_outs);
}

void
CLAP_Plugin::create_note_ports()
{
    _midi_ins = 0;
    _midi_outs = 0;

    _iMidiDialectIns = 0;
    _iMidiDialectOuts = 0;
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
                        ++_iMidiDialectIns;

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
                        ++_iMidiDialectOuts;

                add_port( Port( this, Port::OUTPUT, Port::MIDI, strdup(info.name) ) );
                note_output[_midi_outs].hints.plug_port_index = i;
                ++_midi_outs;
            }
        }
    }

    MESSAGE( "Plugin has %i MIDI ins and %i MIDI outs", _midi_ins, _midi_outs);
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

// Plugin parameters flush.
void
CLAP_Plugin::plugin_params_flush (void)
{
    if (!_plugin)
            return;

    if (!_params_flush || _is_processing)
            return;

    _params_flush = false;

    _events_in.clear();
    _events_out.clear();

    if (_params && _params->flush)
    {
        _params->flush(_plugin, _events_in.ins(), _events_out.outs());
        process_params_out();
        _events_out.clear();
    }
}

// Transfer parameter changes...
void
CLAP_Plugin::process_params_out (void)
{
    const uint32_t nevents = _events_out.size();
    for (uint32_t i = 0; i < nevents; ++i)
    {
        const clap_event_header *eh = _events_out.get(i);

        if (eh && (
                eh->type == CLAP_EVENT_PARAM_VALUE ||
                eh->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                eh->type == CLAP_EVENT_PARAM_GESTURE_END))
        {
            _params_out.push(eh);
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
    CLAPIMPL::EventList& params_out = CLAP_Plugin::params_out();
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
                _paramValues.insert(prm);
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
                    = _paramValues.find (param_id);

                if ( got == _paramValues.end() )
                {
                    WARNING("GESTURE_END Id not found = %d", param_id);
                    param_id = CLAP_INVALID_ID;
                }
                else
                {
                    value = got->second;
                    _paramValues.erase(param_id);
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
                    = _paramValues.find (param_id);

                // If we found the item, then replace it with new pair
                if ( !(got == _paramValues.end()) )
                {
                    std::pair<int, double> prm ( int(ev->param_id), value );

                    _paramValues.erase(param_id);
                    _paramValues.insert(prm);

                    param_id = CLAP_INVALID_ID;
                }
            }
        }
        // Actually make the change...
        if (param_id != (int) CLAP_INVALID_ID)
        {
            std::unordered_map<int, unsigned long>::const_iterator got
                    = _paramIds.find (param_id);

            if ( got == _paramIds.end() )
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

    Fl::repeat_timeout( F_DEFAULT_MSECS, &CLAP_Plugin::parameter_update, this );
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
    if (!_gui)
        return false;

    /* Toggle show and hide */
    if(_bEditorCreated)
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

    if (!_gui->is_api_supported(_plugin, CLAP_WINDOW_API_X11, false))
    {
        _is_floating = _gui->is_api_supported(_plugin, CLAP_WINDOW_API_X11, true);
    }

    if (!_gui->create(_plugin, CLAP_WINDOW_API_X11, _is_floating))
    {
        DMESSAGE("Could not create the plugin GUI.");
        return false;
    }

    /* We seem to have an accepted ui, so lets try to embed it in an X window */
    _x_is_resizable = _gui->can_resize(_plugin);
    
    _X11_UI = new X11PluginUI(this, _x_is_resizable, false);
    _X11_UI->setTitle(label());
    clap_window_t win = { CLAP_WINDOW_API_X11, {} };
    win.ptr = _X11_UI->getPtr();

    if (_is_floating)
    {
        DMESSAGE("Using Floating Window");
        _gui->set_transient(_plugin, &win);
        _gui->suggest_title(_plugin, base_label());
    } else
    {
        if (!_gui->set_parent(_plugin, &win))
        {
            DMESSAGE("Could not embed the plugin GUI.");
            _gui->destroy(_plugin);
            return false;
        }
    }

    DMESSAGE("GOT A CREATE");

    _bEditorCreated = show_custom_ui();

    return _bEditorCreated;
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

    _X11_UI->setSize(width, height, true, _x_is_resizable);

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
        _bEditorCreated = false;

        if (_gui)
            _gui->destroy(_plugin);
    }
    // The floating window has been closed, or the connection to the gui has been lost.
    // If was_destroyed is true, then the host must call clap_plugin_gui->destroy() to acknowledge
    // the gui destruction.
}

bool
CLAP_Plugin::show_custom_ui()
{
    if (_is_floating)
    {
        _x_is_visible = _gui->show(_plugin);
        Fl::add_timeout( F_DEFAULT_MSECS, &CLAP_Plugin::custom_update_ui, this );
        return _x_is_visible;
    }

    _X11_UI->show();
    _X11_UI->focus();

    _x_is_visible = true;

    _gui->show(_plugin);

    Fl::add_timeout( F_DEFAULT_MSECS, &CLAP_Plugin::custom_update_ui, this );

    return true;
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
    if (!_is_floating)
    {
        if(_x_is_visible)
            _X11_UI->idle();
    }

    for (LinkedList<HostTimerDetails>::Itenerator it = _fTimers.begin2(); it.valid(); it.next())
    {
        const uint32_t currentTimeInMs = water::Time::getMillisecondCounter();
        HostTimerDetails& timer(it.getValue(kTimerFallbackNC));

        if (currentTimeInMs > timer.lastCallTimeInMs + timer.periodInMs)
        {
            timer.lastCallTimeInMs = currentTimeInMs;
            if(Thread::is( "UI" ))
                _timer_support->on_timer(_plugin, timer.clapId);
        }
    }

    if(_x_is_visible)
    {
        Fl::repeat_timeout( F_DEFAULT_MSECS, &CLAP_Plugin::custom_update_ui, this );
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
        return _gui->hide(_plugin);
    }

    Fl::remove_timeout(&CLAP_Plugin::custom_update_ui, this);

    _x_is_visible = false;

    if (_X11_UI != nullptr)
        _X11_UI->hide();

    if (_bEditorCreated)
    {
        _gui->destroy(_plugin);
        _bEditorCreated = false;
    }

    if(_X11_UI != nullptr)
    {
        delete _X11_UI;
        _X11_UI = nullptr;
    }

    return true;
}

void
CLAP_Plugin::handlePluginUIClosed()
{
    _x_is_visible = false;
}

void
CLAP_Plugin::handlePluginUIResized(const uint width, const uint height)
{
    DMESSAGE("Handle Resized W = %d: H = %d", width, height);
    if (_x_width != width || _x_height != height)
    {
        uint width2 = width;
        uint height2 = height;

        if (_gui->adjust_size(_plugin, &width2, &height2))
        {
            if (width2 != width || height2 != height)
            {
                _x_width = width2;
                _x_height = height2;
                _X11_UI->setSize(width2, height2, false, false);
            }
            else
            {
                _gui->set_size(_plugin, width2, height2);
            }
        }
    }
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
    if (_timer_support == nullptr)
    {
        const clap_plugin_timer_support_t* const timerExt = static_cast<const clap_plugin_timer_support_t*>(
            _plugin->get_extension(_plugin, CLAP_EXT_TIMER_SUPPORT));

        if (timerExt != nullptr && timerExt->on_timer != nullptr)
            _timer_support = timerExt;
    }

    NON_SAFE_ASSERT_RETURN(_timer_support != nullptr, false);

    // FIXME events only driven as long as UI is open
    // CARLA_SAFE_ASSERT_RETURN(fUI.isCreated, false);

    const HostTimerDetails timer = 
    {
        _fTimers.isNotEmpty() ? _fTimers.getLast(kTimerFallback).clapId + 1 : 1,
        periodInMs,
        0
    };

    _fTimers.append(timer);

    *timerId = timer.clapId;
    return true;
}

bool
CLAP_Plugin::clapUnregisterTimer(const clap_id timerId)
{
    DMESSAGE("ClapTimerUnregister(%u)", timerId);

    for (LinkedList<HostTimerDetails>::Itenerator it = _fTimers.begin2(); it.valid(); it.next())
    {
        const HostTimerDetails& timer(it.getValue(kTimerFallback));

        if (timer.clapId == timerId)
        {
            _fTimers.remove(it);
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
    _params_flush = true;
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
    _plug_request_restart = true;
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
    if(_plug_needs_callback)
        return false;

    return Thread::is( "UI" );
}

bool
CLAP_Plugin::is_audio_thread()
{
    return Thread::is ( "RT" );
}

// Host LOG callbacks...
void
CLAP_Plugin::host_log (
	const clap_host * /*host*/, clap_log_severity severity, const char *msg )
{
    switch (severity)
    {
    case CLAP_LOG_DEBUG:
            DMESSAGE("CLAP_log: Debug: %s", msg);
            break;
    case CLAP_LOG_INFO:
            MESSAGE("CLAP_log: Info: %s", msg);
            break;
    case CLAP_LOG_WARNING:
            WARNING("CLAP_log: Warning: %s", msg);
            break;
    case CLAP_LOG_ERROR:
            WARNING("CLAP_log: Error: %s", msg);
            break;
    case CLAP_LOG_FATAL:
            WARNING("CLAP_log: Fatal: %s", msg);
            break;
    case CLAP_LOG_HOST_MISBEHAVING:
            WARNING("CALP_log: Host misbehaving: %s", msg);
            break;
    default:
            DMESSAGE("CLAP_log: Unknown: %s", msg);
            break;
    }
}

void
CLAP_Plugin::save_CLAP_plugin_state(const std::string &filename)
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
CLAP_Plugin::restore_CLAP_plugin_state(const std::string &filename)
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
    if (_state->load(_plugin, &stream))
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

    if(_last_chunk)
    {
        std::free(_last_chunk);
        _last_chunk = nullptr;
    }

    clap_ostream_impl stream;
    if (_state->save(_plugin, &stream))
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
            std::size_t found = export_import_strip.find_last_of("/\\");
            std::string path = (export_import_strip.substr(0, found));

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

    Module::Picked picked = { Type_CLAP, strdup(s_clap_id.c_str()), 0, s_clap_path };

    if ( !load_plugin( picked ) )
    {
        fl_alert( "Could not load CLAP plugin %s", s_clap_path.c_str() );
        return;
    }

    Module::set( e );

    if (!restore.empty())
    {
        restore_CLAP_plugin_state(restore);
    }
}

#endif  // CLAP_SUPPORT

