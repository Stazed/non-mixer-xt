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
 * File:   LADSPA_Plugin.C
 * Author: sspresto
 * 
 * Created on November 24, 2022, 4:32 PM
 */

#include <math.h>

#include "../../nonlib/dsp.h"

#include "Chain.H"
#include "LADSPA_Plugin.H"

#define HAVE_LIBLRDF 1
#include "LADSPAInfo.h"

LADSPA_Plugin::LADSPA_Plugin ( ) : Plugin_Module( )
{
    init();
    log_create();
}

LADSPA_Plugin::~LADSPA_Plugin ( )
{
    log_destroy();
    plugin_instances( 0 );
}

static LADSPAInfo *ladspainfo;

bool
LADSPA_Plugin::load_plugin(unsigned long id)
{
    ladspainfo = _ladspainfo;

    _is_lv2 = false;
    _idata->descriptor = ladspainfo->GetDescriptorByID( id );

    _plugin_ins = _plugin_outs = 0;

    if ( ! _idata->descriptor )
    {
        /* unknown plugin ID */
        WARNING( "Unknown plugin ID: %lu", id );
	char s[25];

	snprintf( s, 24, "! %lu", id );
	
        base_label( s );
        return false;
    }

    base_label( _idata->descriptor->Name );

    if ( _idata->descriptor )
    {
        if ( LADSPA_IS_INPLACE_BROKEN( _idata->descriptor->Properties ) )
        {
            WARNING( "Cannot use this plugin because it is incapable of processing audio in-place" );
            return false;
        }
       
        /* else if ( ! LADSPA_IS_HARD_RT_CAPABLE( _idata->descriptor->Properties ) ) */
        /* { */
        /*     WARNING( "Cannot use this plugin because it is incapable of hard real-time operation" ); */
        /*     return false; */
        /* } */

        MESSAGE( "Name: %s", _idata->descriptor->Name );

        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
        {
            if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
            {
                if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    add_port( Port( this, Port::INPUT, Port::AUDIO, _idata->descriptor->PortNames[ i ] ) );
                    _plugin_ins++;
                }
                else if (LADSPA_IS_PORT_OUTPUT(_idata->descriptor->PortDescriptors[i]))
                {
                    _plugin_outs++;
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO, _idata->descriptor->PortNames[ i ] ) );
                }
            }
        }

        MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);
        if(!_plugin_ins)
            is_zero_input_synth(true);

        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
        {
            if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[i] ) )
            {
                Port::Direction d = Port::INPUT;

                if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    d = Port::INPUT;
                }
                else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    d = Port::OUTPUT;
                }

                Port p( this, d, Port::CONTROL, _idata->descriptor->PortNames[ i ] );

                p.hints.default_value = 0;

                LADSPA_PortRangeHintDescriptor hd = _idata->descriptor->PortRangeHints[i].HintDescriptor;

                if ( LADSPA_IS_HINT_BOUNDED_BELOW(hd) )
                {
                    p.hints.ranged = true;
                    p.hints.minimum = _idata->descriptor->PortRangeHints[i].LowerBound;
                    if ( LADSPA_IS_HINT_SAMPLE_RATE(hd) )
                    {
                        p.hints.minimum *= sample_rate();
                    }
                }
                if ( LADSPA_IS_HINT_BOUNDED_ABOVE(hd) )
                {
                    p.hints.ranged = true;
                    p.hints.maximum = _idata->descriptor->PortRangeHints[i].UpperBound;
                    if ( LADSPA_IS_HINT_SAMPLE_RATE(hd) )
                    {
                        p.hints.maximum *= sample_rate();
                    }
                }

                if ( LADSPA_IS_HINT_HAS_DEFAULT(hd) )
                {

                    float Max=1.0f, Min=-1.0f, Default=0.0f;
                    int Port=i;

                    // Get the bounding hints for the port
                    LADSPA_PortRangeHintDescriptor HintDesc=_idata->descriptor->PortRangeHints[Port].HintDescriptor;
                    if (LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc))
                    {
                        Min=_idata->descriptor->PortRangeHints[Port].LowerBound;
                        if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc))
                        {
                            Min*=sample_rate();
                        }
                    }
                    if (LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc))
                    {
                        Max=_idata->descriptor->PortRangeHints[Port].UpperBound;
                        if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc))
                        {
                            Max*=sample_rate();
                        }
                    }

#ifdef LADSPA_VERSION
// We've got a version of the header that supports port defaults
                    if (LADSPA_IS_HINT_HAS_DEFAULT(HintDesc)) {
                        // LADSPA_HINT_DEFAULT_0 is assumed anyway, so we don't check for it
                        if (LADSPA_IS_HINT_DEFAULT_1(HintDesc)) {
                            Default = 1.0f;
                        } else if (LADSPA_IS_HINT_DEFAULT_100(HintDesc)) {
                            Default = 100.0f;
                        } else if (LADSPA_IS_HINT_DEFAULT_440(HintDesc)) {
                            Default = 440.0f;
                        } else {
                            // These hints may be affected by SAMPLERATE, LOGARITHMIC and INTEGER
                            if (LADSPA_IS_HINT_DEFAULT_MINIMUM(HintDesc) &&
                                LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc)) {
                                Default=_idata->descriptor->PortRangeHints[Port].LowerBound;
                            } else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(HintDesc) &&
                                       LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc)) {
                                Default=_idata->descriptor->PortRangeHints[Port].UpperBound;
                            } else if (LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc) &&
                                       LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc)) {
                                // These hints require both upper and lower bounds
                                float lp = 0.0f, up = 0.0f;
                                float min = _idata->descriptor->PortRangeHints[Port].LowerBound;
                                float max = _idata->descriptor->PortRangeHints[Port].UpperBound;
                                if (LADSPA_IS_HINT_DEFAULT_LOW(HintDesc)) {
                                    lp = 0.75f;
                                    up = 0.25f;
                                } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(HintDesc)) {
                                    lp = 0.5f;
                                    up = 0.5f;
                                } else if (LADSPA_IS_HINT_DEFAULT_HIGH(HintDesc)) {
                                    lp = 0.25f;
                                    up = 0.75f;
                                }

                                if (LADSPA_IS_HINT_LOGARITHMIC(HintDesc)) {

                                    p.hints.type = Port::Hints::LOGARITHMIC;

                                    if (min==0.0f || max==0.0f) {
                                        // Zero at either end means zero no matter
                                        // where hint is at, since:
                                        //  log(n->0) -> Infinity
                                        Default = 0.0f;
                                    } else {
                                        // Catch negatives
                                        bool neg_min = min < 0.0f ? true : false;
                                        bool neg_max = max < 0.0f ? true : false;

                                        if (!neg_min && !neg_max) {
                                            Default = exp(::log(min) * lp + ::log(max) * up);
                                        } else if (neg_min && neg_max) {
                                            Default = -exp(::log(-min) * lp + ::log(-max) * up);
                                        } else {
                                            // Logarithmic range has asymptote
                                            // so just use linear scale
                                            Default = min * lp + max * up;
                                        }
                                    }
                                } else {
                                    Default = min * lp + max * up;
                                }
                            }
                            if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc)) {
                                Default *= sample_rate();
                            }
                        }

                        if (LADSPA_IS_HINT_INTEGER(HintDesc)) {
                            if ( p.hints.ranged &&
                                 0 == (int)p.hints.minimum &&
                                 1 == (int)p.hints.maximum )
                                p.hints.type = Port::Hints::BOOLEAN;
                            else
                                p.hints.type = Port::Hints::INTEGER;
                            Default = floorf(Default);
                        }
                        if (LADSPA_IS_HINT_TOGGLED(HintDesc)){
                            p.hints.type = Port::Hints::BOOLEAN;
                        }
                    }
#else
                    Default = 0.0f;
#endif
                    p.hints.default_value = Default;
                }

                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );

                add_port( p );

                DMESSAGE( "Plugin has control port \"%s\" (default: %f)", _idata->descriptor->PortNames[ i ], p.hints.default_value );
            }
        }

        if (bypassable()) {
            Port pb( this, Port::INPUT, Port::CONTROL, "dsp/bypass" );
            pb.hints.type = Port::Hints::BOOLEAN;
            pb.hints.ranged = true;
            pb.hints.maximum = 1.0f;
            pb.hints.minimum = 0.0f;
            pb.hints.dimensions = 1;
            pb.connect_to( _bypass );
            add_port( pb );
        }

    }
    else
    {
        WARNING( "Failed to load plugin" );
        return false;
    }

    int instances = plugin_instances( 1 );
    
    if ( instances )
    {
        bypass( false );
    }

    return instances;
}


void
LADSPA_Plugin::init ( void )
{
    _is_lv2 = false;
    Plugin_Module::init();
    _idata = new ImplementationData();
}

bool
LADSPA_Plugin::loaded ( void ) const
{
    return _idata->handle.size() > 0 && ( _idata->descriptor != NULL );
}

void
LADSPA_Plugin::bypass ( bool v )
{
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

bool
LADSPA_Plugin::configure_inputs( int n )
{
    unsigned int inst = _idata->handle.size();
    
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
        if ( inst != _idata->handle.size() )
        {
            if ( !b )
                deactivate();
            
            if ( plugin_instances( inst ) )
                instances( inst );
            else
                return false;
            
            if ( !b )
                activate();
        }
    }

    return true;
}

void
LADSPA_Plugin::handle_port_connection_change ( void )
{
//    DMESSAGE( "Connecting audio ports" );

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
LADSPA_Plugin::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );
}

void
LADSPA_Plugin::activate ( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );

    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

    if ( _idata->descriptor->activate )
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            _idata->descriptor->activate( _idata->handle[i] );

    *_bypass = 0.0f;

    if ( chain() )
        chain()->client()->unlock();
}

void
LADSPA_Plugin::deactivate( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

    if ( _idata->descriptor->deactivate )
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            _idata->descriptor->deactivate( _idata->handle[i] );

    if ( chain() )
        chain()->client()->unlock();
}

nframes_t
LADSPA_Plugin::get_module_latency ( void ) const
{
    for ( unsigned int i = ncontrol_outputs(); i--; )
    {
        if ( !strcasecmp( "latency", control_output[i].name() ) )
        {
            return control_output[i].control_value();
        }
    }

    return 0;
}

void
LADSPA_Plugin::process ( nframes_t nframes )
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
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            _idata->descriptor->run( _idata->handle[i], nframes );
        
        _latency = get_module_latency();
    }
}


bool
LADSPA_Plugin::plugin_instances ( unsigned int n )
{
    if ( _idata->handle.size() > n )
    {
        for ( int i = _idata->handle.size() - n; i--; )
        {
            DMESSAGE( "Destroying plugin instance" );

            LADSPA_Handle h = _idata->handle.back();

            if ( _idata->descriptor->deactivate )
                _idata->descriptor->deactivate( h );
            if ( _idata->descriptor->cleanup )
                _idata->descriptor->cleanup( h );

            _idata->handle.pop_back();
        }
    }
    else if ( _idata->handle.size() < n )
    {
        for ( int i = n - _idata->handle.size(); i--; )
        {
            DMESSAGE( "Instantiating plugin... with sample rate %lu", (unsigned long)sample_rate());

            void* h;

            if ( ! (h = _idata->descriptor->instantiate( _idata->descriptor, sample_rate() ) ) )
            {
                WARNING( "Failed to instantiate plugin" );
                return false;
            }

            DMESSAGE( "Instantiated: %p", h );

            _idata->handle.push_back( h );

            DMESSAGE( "Connecting control ports..." );

            int ij = 0;
            int oj = 0;

            for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
            {
                if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[k] ) )
                {
                    if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[k] ) )
                        _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_input[ij++].buffer() );
                    else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[k] ) )
                        _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_output[oj++].buffer() );
                }
            }

            // connect ports to magic bogus value to aid debugging.
            for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
                if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
                    _idata->descriptor->connect_port( h, k, (LADSPA_Data*)0x42 );
        }
    }

    return true;
}

bool 
LADSPA_Plugin::get_impulse_response ( sample_t *buf, nframes_t nframes )
{
    apply( buf, nframes );
    
    if ( buffer_is_digital_black( buf + 1, nframes - 1 ))
        /* no impulse response... */
        return false;

    return true;
}

void
LADSPA_Plugin::set_input_buffer ( int n, void *buf )
{
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

    for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
    {
        if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) &&
             LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
        {
            if ( n-- == 0 )
                _idata->descriptor->connect_port( h, i, (LADSPA_Data*)buf );
        }
    }
}

void
LADSPA_Plugin::set_output_buffer ( int n, void *buf )
{
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

    for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
    {
        if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[i] ) &&
             LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
        {
            if ( n-- == 0 )
                _idata->descriptor->connect_port( h, i, (LADSPA_Data*)buf );
        }
    }
}

/** Instantiate a temporary version of the LADSPA plugin, and run it (in place) against the provided buffer */
bool
LADSPA_Plugin::apply ( sample_t *buf, nframes_t nframes )
{
// actually osc or UI    THREAD_ASSERT( UI );

    void* h;

    if ( ! (h = _idata->descriptor->instantiate( _idata->descriptor, sample_rate() ) ) )
    {
        WARNING( "Failed to instantiate plugin" );
        return false;
    }

    int ij = 0;
    int oj = 0;

    for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
    {
        if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[k] ) )
        {
            if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[k] ) )
                _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_input[ij++].buffer() );
            else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[k] ) )
                _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_output[oj++].buffer() );
        }
    }

    if ( _idata->descriptor->activate )
        _idata->descriptor->activate( h );

    int tframes = 512;
    float tmp[tframes];

    memset( tmp, 0, sizeof( float ) * tframes );

    for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
        if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
            _idata->descriptor->connect_port( h, k, tmp );

    /* flush any parameter interpolation */
    _idata->descriptor->run( h, tframes );

    for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
        if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
            _idata->descriptor->connect_port( h, k, buf );

    /* run for real */
    _idata->descriptor->run( h, nframes );

    if ( _idata->descriptor->deactivate )
        _idata->descriptor->deactivate( h );
    if ( _idata->descriptor->cleanup )
        _idata->descriptor->cleanup( h );

    return true;
}


void
LADSPA_Plugin::get ( Log_Entry &e ) const
{

    e.add( ":plugin_id", _idata->descriptor->UniqueID );

    /* these help us display the module on systems which are missing this plugin */
    e.add( ":plugin_ins", _plugin_ins );
    e.add( ":plugin_outs", _plugin_outs );

    Module::get( e );
}

void
LADSPA_Plugin::set ( Log_Entry &e )
{
    int n = 0;

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
    
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":plugin_id" ) )
        {
            load_plugin( (unsigned long) atoll ( v ) );
        }
        else if ( ! strcmp( s, ":plugin_ins" ) )
        {
            _plugin_ins = atoi( v );
        }
        else if ( ! strcmp( s, ":plugin_outs" ) )
        {
            _plugin_outs = atoi( v );
        }
    }

    Module::set( e );
}