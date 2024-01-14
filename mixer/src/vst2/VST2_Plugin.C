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

VST2_Plugin::VST2_Plugin() :
    Plugin_Module()
{
    _plug_type = Type_VST2;

    log_create();
}

VST2_Plugin::~VST2_Plugin()
{
    log_destroy();
}

bool
VST2_Plugin::load_plugin ( Module::Picked picked )
{
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

void
VST2_Plugin::activate ( void )
{
    // FIXME
}

void
VST2_Plugin::deactivate ( void )
{
    // FIXME
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