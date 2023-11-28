/*******************************************************************************/
/* Copyright (C) 2013 Jonathan Moore Liles                                     */
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

#include "../../nonlib/JACK/Client.H"
#include "../../nonlib/JACK/Port.H"
#include "../../nonlib/OSC/Endpoint.H"
#include "../../nonlib/MIDI/midievent.H"
#include "../../nonlib/debug.h"
#include "../../nonlib/nsm.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

using namespace MIDI;

#include <jack/ringbuffer.h>
#include <jack/thread.h>

#include <stdlib.h>
#include <stdio.h>

#include <map>
#include <string>

#include <signal.h>
#include <unistd.h>                                             /* usleep */
/* simple program to translate from MIDI<->OSC Signals using a fixed mapping  */

#undef APP_NAME
const char *APP_NAME = "midi-mapper-xt";
#undef APP_TITLE
const char *APP_TITLE = "MIDI-Mapper-xt";
#undef VERSION
const char *VERSION = "1.1";

const int FILE_VERSION = 1;

nsm_client_t *nsm;
char *instance_name;

static nframes_t buffers = 0;


OSC::Endpoint *osc = 0;
/* const double NSM_CHECK_INTERVAL = 0.25f; */


void
handle_hello ( lo_message msg )
{
    int argc = lo_message_get_argc( msg );
    lo_arg **argv = lo_message_get_argv( msg );

    if ( argc >= 4 )
    {
        const char *url = &argv[0]->s;
        const char *name = &argv[1]->s;
        const char *version = &argv[2]->s;
        const char *id = &argv[3]->s;
                
        MESSAGE( "Discovered NON peer %s (%s) @ %s with ID \"%s\"", name, version, url, id );
                
        /* register peer */
        osc->handle_hello( id, url );
    }
}

void
check_nsm ( void )
{
    nsm_check_nowait( nsm );
    //   Fl::repeat_timeout( NSM_CHECK_INTERVAL, &check_nsm, v );
}

static int
osc_non_hello ( const char *, const char *, lo_arg **, int , lo_message msg, void * )
{
    handle_hello( msg );
    return 0;
}


static     void
say_hello ( void )
{
    if ( nsm_is_active( nsm ) )
    {
        lo_message m = lo_message_new();
        
        lo_message_add( m, "sssss",
                        "/non/hello",
                        osc->url(),
                        APP_TITLE,
                        VERSION,
                        instance_name );
        
        nsm_send_broadcast( nsm, m );
    }
}

class Engine : public JACK::Client
{
public:
    jack_ringbuffer_t *input_ring_buf;
    jack_ringbuffer_t *output_ring_buf;
    JACK::Port *midi_input_port;
    JACK::Port *midi_output_port;
    
    Engine ( )
        {
            input_ring_buf = jack_ringbuffer_create( 32 * 32 * sizeof( jack_midi_event_t ));
            jack_ringbuffer_reset( input_ring_buf );
            output_ring_buf = jack_ringbuffer_create( 32 * 32 * sizeof( jack_midi_event_t ));
            jack_ringbuffer_reset( output_ring_buf );

            midi_input_port = 0;
            midi_output_port = 0;
        }

    virtual ~Engine ( )
        {
            deactivate();
        }

    
    int process ( nframes_t nframes )
        {
	    ++buffers;
	    
            /* process input */
            {
                if ( !midi_input_port )
                    return 0;

		
		/* jack_position_t pos; */
		
		/* jack_transport_query( this->jack_client(), &pos ); */

                void *buf = midi_input_port->buffer( nframes );
            
                jack_midi_event_t ev;

                jack_nframes_t count = jack_midi_get_event_count( buf );

		/* if ( count  > 0 ) */
		/* { */
		/* DMESSAGE( "Event count: %lu", count); */
		/* } */
		
                /* place MIDI events into ringbuffer for non-RT thread */
            
                for ( uint i = 0; i < count; ++i )
                {
//            MESSAGE( "Got midi input!" );
                
                    jack_midi_event_get( &ev, buf, i );

		    midievent e;
		    
                    /* e.timestamp( pos.frame + ev.time ); */
		    e.timestamp( ev.time );
		    e.status( ev.buffer[0] );
		    e.lsb( ev.buffer[1] );
		    if ( ev.size == 3 )
			e.msb( ev.buffer[2] );
		    else
			e.msb( 0 );
		                    
                    /* /\* time is frame within cycle, convert to absolute tick *\/ */
                    /* e.timestamp( ph + (ev.time / transport.frames_per_tick) ); */
                    /* e.status( ev.buffer[0] ); */
                    /* e.lsb( ev.buffer[1] ); */
                    /* if ( ev.size == 3 ) */
                    /* e.msb( ev.buffer[2] ); */

                    if ( jack_ringbuffer_write( input_ring_buf, (char*)&e, sizeof( midievent ) ) != sizeof( midievent ) )
                        WARNING( "input buffer overrun" );
                }
            }

            /* process output */
            {
                void *buf = midi_output_port->buffer(nframes);
                
                jack_midi_clear_buffer( buf );

                jack_midi_event_t ev;

                nframes_t frame = 0;

                while ( true )
                {
                    /* jack_ringbuffer_data_t vec[2]; */
                    /* jack_ringbuffer_get_read_vector( output_ring_buf, vec ); */

                    if ( jack_ringbuffer_peek( output_ring_buf, (char*)&ev, sizeof( jack_midi_event_t )) <= 0 )
                        break;

                    unsigned char *buffer = jack_midi_event_reserve( buf, frame, ev.size );
                    if ( !buffer )
                    {
                        WARNING("Output buffer overrun, will send later" );
                        break;
                    }

                    memcpy( buffer, &ev, ev.size );
                    
                    jack_ringbuffer_read_advance( output_ring_buf, sizeof( jack_midi_event_t ) );
                }
            }
            
            return 0;
        }


    void freewheel ( bool /*starting*/ )
        {
        }

    int xrun ( void )
        {
            return 0;
        }

    int buffer_size ( nframes_t /*nframes*/ )
        {
            return 0;
        }

    void shutdown ( void )
        {
        }

    void thread_init ( void )
        {
        }

};

Engine *engine;

const unsigned int MAX_14BIT = 16383;
const unsigned int MAX_7BIT = 127;

static char
get_lsb( int i )
{
    return i & 0x7F;
}

static char
get_msb( int i )
{
    return ( i >> 7 ) & 0x7F;
}

static unsigned int 
get_14bit ( char msb, char lsb )
{
    return msb * (MAX_7BIT + 1) + lsb;
}

class signal_mapping
{
public:

    bool is_nrpn;
    bool is_nrpn14;
    bool is_toggle;
    
    midievent event;

    std::string signal_name;

    OSC::Signal *signal;

    nframes_t last_midi_tick;
    nframes_t last_feedback_tick;

    int learning_value_msb;
    int learning_value_lsb;
    

    bool is_learning ( )
	{
	    return NULL == signal;
	}
    
    signal_mapping ( )
        {
            is_nrpn = false;
	    is_nrpn14 = false;
	    is_toggle =- false;
            signal = NULL;
	    last_midi_tick = 0;
	    last_feedback_tick = 0;
	    learning_value_lsb = 0;
	    learning_value_msb = 0;
        }

    ~signal_mapping ( )
        {
            if ( signal )
                delete signal;
            signal = NULL;
        }

    /* char *serialize ( void ) const */
    /*     { */
    /*         char *s; */
    /*         const char *opcode = 0; */
    /*         int v1 = 0; */
            
    /*         if ( is_nrpn ) */
    /*         { */
    /*             opcode = is_nrpn14 ? "NRPN14" : "NRPN"; */
    /*             v1 = get_14bit( event.msb(), event.lsb() ); */
    /*         } */
    /*         else */
    /*             switch ( event.opcode() ) */
    /*             { */
    /*                 case MIDI::midievent::CONTROL_CHANGE: */
    /*                     opcode = "CC"; */
    /*                     v1 = event.lsb(); */
    /*                     break; */
    /*                 case MIDI::midievent::NOTE_ON: */
    /*                     opcode = "NOTE_ON";                         */
    /*                     v1 = event.note(); */
    /*                     break; */
    /*                 default: */
    /*                     // unsupported */
    /*                     break; */
    /*             } */
       
    /*         asprintf( &s, "%s %d %d%s", opcode, event.channel(), v1, is_toggle ? " T" : "" ); */

    /*         return s; */
    /*     } */

    void deserialize ( const char *s )
        {
            int channel;
            char *opcode;
            int control;

	    if ( 3 == sscanf( s, "%ms %d %d", &opcode, &channel, &control ) )
            {
                event.channel( channel );
                event.opcode( MIDI::midievent::CONTROL_CHANGE );

                is_nrpn = false;

                if ( !strcmp( opcode, "NRPN" ) )
                {
                    is_nrpn = true;
                    
                    event.lsb( get_lsb( control ));
                    event.msb( get_msb( control ));
                }
                else if ( !strcmp( opcode, "CC" ) )
                {
                    event.lsb( control );
                }

                free(opcode);
            }
	    else
	    {
		DMESSAGE( "Failed to parse midi event descriptor: %s", s );
	    }
        }

};

int signal_handler ( float value, void *user_data )
{
    signal_mapping *m = (signal_mapping*)user_data;

    /* DMESSAGE( "Received value: %f", value ); */

    m->last_feedback_tick = buffers;

    /* magic number to give a release time to prevent thrashing. */	
    /* if ( ! ( m->last_feedback_tick > m->last_midi_tick + 4  )) */
    /* 	return 0; */

    if ( m->is_nrpn )
    {
        jack_midi_event_t jev[4];
        {
            midievent e;
            e.opcode( MIDI::midievent::CONTROL_CHANGE );
            e.channel( m->event.channel() );
            e.lsb( 99 );
            e.msb( m->event.msb() );
            jev[0].size = e.size();
            e.raw( (byte_t*)&jev[0], e.size() );
//            e.pretty_print();
        }

        {
            midievent e;
            e.opcode( MIDI::midievent::CONTROL_CHANGE );
            e.channel( m->event.channel() );
            e.lsb( 98 );
            e.msb( m->event.lsb() );
            jev[1].size = e.size();
            e.raw( (byte_t*)&jev[1], e.size() );
//            e.pretty_print();
        }


        {
            midievent e;
            e.opcode( MIDI::midievent::CONTROL_CHANGE );
            e.channel( m->event.channel() );
            e.lsb( 6 );
            e.msb( (int)(value * (float)MAX_14BIT) >> 7 );
            jev[2].size = e.size();
            e.raw( (byte_t*)&jev[2], e.size() );
//            e.pretty_print();
        }

        {
            midievent e;
            e.opcode( MIDI::midievent::CONTROL_CHANGE );
            e.channel( m->event.channel() );
            e.lsb( 38 );
            e.msb( (int)(value * (float)MAX_14BIT) & 0x7F );
            jev[3].size = e.size();
            e.raw( (byte_t*)&jev[3], e.size() );
//            e.pretty_print();
        }

        for ( int i = 0; i < 4; i++ )
        {
            if ( jack_ringbuffer_write( engine->output_ring_buf, (char*)&jev[i], 
                                        sizeof( jack_midi_event_t ) ) != sizeof( jack_midi_event_t ) )
                WARNING( "output buffer overrun" );
        }
    }
    else
    {
        jack_midi_event_t ev;
        
        m->event.msb( value * (float)MAX_7BIT );
        ev.size = m->event.size();
        m->event.raw( (byte_t*)&ev, m->event.size() );
        
//        m->event.pretty_print();
        
        if ( jack_ringbuffer_write( engine->output_ring_buf, (char*)&ev, sizeof( jack_midi_event_t ) ) != sizeof( jack_midi_event_t ) )
            WARNING( "output buffer overrun" );
    }

    return 0;
}


std::map<std::string,signal_mapping> sig_map;
std::map<int,std::string> sig_map_ordered;

bool
save_settings ( void )
{
    FILE *fp = fopen( "signals", "w" );
    
    if ( !fp )
        return false;

    fprintf( fp, "# MIDI-Mapper-XT version %i\n", FILE_VERSION );
    
    for ( std::map<int,std::string>::const_iterator i = sig_map_ordered.begin();
          i != sig_map_ordered.end();
          ++i )
    {
	signal_mapping &m = sig_map[i->second.c_str()];
	
	/* char *midi_event = m.serialize(); */

	/* FIXME: instead of T and NRPN14, use 1 7 and 14 (value significant bits).
	   Also, use syntax like so: [NRPN 0 0] 1bit :: /foo/bar */
        fprintf( fp, "%s\t%s\t%s\n",
		 i->second.c_str(),
//		 midi_event,
		 m.is_toggle ? "1-BIT" :
		 m.is_nrpn && m.is_nrpn14 ? "14-BIT" :
		 "7-BIT",
		 m.signal_name.c_str() );

	/* free(midi_event); */
    }
    
    fclose(fp);
    
    return true;
}


static int max_signal = 0;


bool
load_settings ( void )
{
    FILE *fp = fopen( "signals", "r" );
    
    if ( !fp )
        return false;
    
    sig_map.clear();
    sig_map_ordered.clear();
    
    char *signal_name;
    char *midi_event;
    char *flags = NULL;
    
    max_signal = 0;

    int version = 0;

    if ( 1 == fscanf( fp, "# MIDI-Mapper-XT version %i\n", &version ) )
    {
    }
    else
    {
	version = 0;
	rewind(fp);
    }

    DMESSAGE( "Detected file version %i", version);

    while (
	( 1 == version &&
	  3 == fscanf( fp, "%m[^\t]\t%m[^\t]\t%m[^\n]\n", &midi_event, &flags, &signal_name ) )
	||
	( 0 == version &&
	  2 == fscanf( fp, "[%m[^]]] %m[^\n]\n", &midi_event, &signal_name ) ) )
    {
        DMESSAGE( "Read mapping: %s, %s (%s)", midi_event, signal_name, flags );

        if ( sig_map.find( midi_event ) == sig_map.end() )
        {
	    ++max_signal;
	    
            signal_mapping m;

            m.deserialize( midi_event );

	    if ( flags )
	    {
		m.is_toggle = !strcmp( "1-BIT", flags );
		m.is_nrpn14 = !strcmp( "14-BIT", flags );
	    }

            sig_map[midi_event] = m;
            sig_map[midi_event].signal_name = signal_name;
            sig_map[midi_event].signal = osc->add_signal( signal_name, OSC::Signal::Output, 0, 1, 0, signal_handler, NULL, &sig_map[midi_event] );

	    sig_map_ordered[max_signal] = midi_event;
        }
       
        free(signal_name);
        free(midi_event);

        /* if ( sig_map.find( s ) == sig_map.end() ) */
        /* {             */
        /*     int channel, control; */
                    
        /*     if ( 2 == sscanf( s, "/midi/%d/CC/%d", &channel, &control ) ) */
        /*     { */
        /*         signal_mapping m; */
                    
        /*         m.event.channel( channel ); */
        /*         m.event.opcode( MIDI::midievent::CONTROL_CHANGE ); */
        /*         m.event.lsb( control ); */
                    
        /*         MESSAGE( "creating signal %s", s ); */
        /*         sig_map[s] = m; */

        /*         sig_map[s].signal = osc->add_signal( s, OSC::Signal::Output, 0, 1, 0, signal_handler, &sig_map[s] ); */

        /*     } */
        /*     if ( 2 == sscanf( s, "/midi/%d/NRPN/%d", &channel, &control ) ) */
        /*     { */
        /*         signal_mapping m; */
                    
        /*         m.event.channel( channel ); */
        /*         m.event.opcode( MIDI::midievent::CONTROL_CHANGE ); */
        /*         m.event.lsb( get_lsb( control ) ); */
        /*         m.event.msb( get_msb( control ) ); */

        /*         m.is_nrpn = true; */
                    
        /*         MESSAGE( "creating signal %s", s ); */
        /*         sig_map[s] = m; */

        /*         sig_map[s].signal = osc->add_signal( s, OSC::Signal::Output, 0, 1, 0, signal_handler, &sig_map[s] ); */

        /*     } */
        /*     else */
        /*         WARNING( "Could not decode signal spec \"%s\"", s ); */
        /* } */
            
        /* free(s); */
    }

    return true;
}

static bool
create_engine ( void )
{
    if ( engine ) 
    {
        delete engine->midi_input_port;
        delete engine->midi_output_port;
        delete engine;
    }

    DMESSAGE( "Creating JACK engine" );

    engine = new Engine();
    
    if ( ! engine->init( instance_name ) )
    {
        WARNING( "Failed to create JACK client" );
        return false;
    }

    engine->midi_input_port = new JACK::Port( engine, NULL, "midi-in", JACK::Port::Input, JACK::Port::MIDI );
    engine->midi_output_port = new JACK::Port( engine, NULL, "midi-out", JACK::Port::Output, JACK::Port::MIDI );

    if ( !engine->midi_input_port->activate() )
    {
        WARNING( "Failed to activate JACK port" );
        return false;
    }

    if ( !engine->midi_output_port->activate() )
    {
        WARNING( "Failed to activate JACK port" );
        return false;
    }

    return true;
}


static int 
command_open ( const char *name, const char * /*display_name*/, const char *client_id, char ** /*out_msg*/, void * /*userdata*/ )
{
    if ( instance_name )
        free( instance_name );
    
    instance_name = strdup( client_id );
            
    osc->name( client_id );

    mkdir( name, 0777 );
    chdir( name );

    if ( ! create_engine() )
    {
        return ERR_GENERAL;
    }

    load_settings();

    say_hello();
    
    return ERR_OK;
}

static int
command_save ( char ** /*out_msg*/, void * /*userdata*/ )
{
    if ( save_settings() )
    {
        nsm_send_is_clean(nsm);
        return ERR_OK;
    }
    else
        return ERR_GENERAL;
}

static int
command_broadcast ( const char *path, lo_message msg, void * /*userdata*/ )
{
    lo_message_get_argc( msg );
//    lo_arg **argv = lo_message_get_argv( msg );

    if ( !strcmp( path, "/non/hello" ) )
    {
        handle_hello( msg );
        return 0;
    }
    else 
        return -1;

}

enum nrpn_awaiting
{
    CONTROL_MSB,
    CONTROL_LSB,
    VALUE_MSB,
    VALUE_LSB,
    COMPLETE
};

struct nrpn_state
{
    byte_t control_msb;
    byte_t control_lsb;
    byte_t value_msb;
    byte_t value_lsb;
    bool value_lsb_exists;
    bool complete;

    nrpn_awaiting awaiting;    
};


static
struct nrpn_state *
decode_nrpn ( nrpn_state *state, const midievent &e, bool *emit_one )
{
    /* use a bit of state machine to allow people to misuse value LSB and value MSB CCs as regular CCs */

    nrpn_state *n = &state[e.channel()];

    *emit_one = false;

    switch ( e.lsb() )
    {
        case 6:
	    if ( VALUE_MSB == n->awaiting )
	    {
		n->value_msb = e.msb();
		n->value_lsb = 0 == e.msb() ? 0 : MAX_7BIT;
		
		n->complete = true;
		n->awaiting = VALUE_LSB;
		*emit_one = true;
		return n;
	    }
	    break;
        case 38:
	    if ( VALUE_LSB == n->awaiting )
	    {
		n->value_lsb_exists = true;
		n->value_lsb = e.msb();
		*emit_one = true;
		n->complete = true;
		n->awaiting = COMPLETE;
		return n;
	    }
	    break;
        case 99:
	    n->complete = false;
	    n->value_lsb_exists = false;
	    n->control_msb = e.msb();
	    n->control_lsb = 0;
	    n->awaiting = CONTROL_LSB;
	    n->value_msb = 0;
	    n->value_lsb = 0;
	    return n;
	case 98:
	    n->awaiting = VALUE_MSB;
	    n->complete = false;
	    n->control_lsb = e.msb();
	    return n;
    }
    
    return NULL;
}


static volatile int got_sigterm = 0;

void
sigterm_handler ( int )
{
    got_sigterm = 1;
}

void emit_signal_for_event ( const char *midi_event, midievent &e, struct nrpn_state *st )
{
    bool is_nrpn = st != NULL;
    
    if ( sig_map.find( midi_event ) == sig_map.end() )
    {

	/* first time seeing this control. */
	    
	signal_mapping m;
        	    
	m.event.lsb( e.lsb() );
	m.event.msb( e.msb() );
	
	m.event.opcode( e.opcode() );
	m.event.channel( e.channel() );
	
	m.is_nrpn = is_nrpn;
	
	if ( is_nrpn )
	{
	    m.event.lsb( st->control_lsb );
	    m.event.msb( st->control_msb );

	    if ( st->value_lsb_exists )
		m.learning_value_lsb = st->value_lsb;
	    
	    m.learning_value_msb = st->value_msb;		
	}
	else
	    m.learning_value_msb = e.msb();
	
	/* wait until we see it again to remember it */
	DMESSAGE("First time seeing control %s, will map on next event instance.", midi_event );

	sig_map[midi_event] = m;

	return;
    }
    
	/* if we got this far, it means we are on the second event for a the event type being learned */
    signal_mapping *m = &sig_map[midi_event];

    if ( m->is_learning() )
    {
	/* FIXME: need to gather NRPN LSB value that (maybe) arrives between first and second event for this NRPN controller.
	   14-bit flag is set if there was an LSB, otherwise, 7 or 1 bit mode is applied.
	   
	   In normal operation, 14-bit NRPN controls should await the LSB before sending signal, 7 and 1 bit can send on MSB.
	*/
	
	DMESSAGE( "Going to learn event %s now", midi_event );
	
	char *s;

	asprintf( &s, "/control/%i", ++max_signal );
                      
	DMESSAGE( "Creating signal %s for event %s.", s, midi_event );

	bool is_toggle = false;
	bool is_14bit_nrpn = false;
	
	if ( !is_nrpn )
	{
	    DMESSAGE( "Learning value msb: %u, msb: %u", m->learning_value_msb, e.msb() );
	    
	    is_toggle = ( m->learning_value_msb == 0 && e.msb() == MAX_7BIT ) ||
		( m->learning_value_msb == MAX_7BIT && e.msb() == 0 );
	}
	else
	{
	    is_14bit_nrpn = m->learning_value_lsb != -1;

	    unsigned int val1 = get_14bit( m->learning_value_msb, m->learning_value_lsb );
	    unsigned int val2 = get_14bit( st->value_msb, st->value_lsb );
	    
	    is_toggle = !is_14bit_nrpn
		? ( m->learning_value_msb == 0 && st->value_msb == MAX_7BIT ) ||
		( m->learning_value_msb == MAX_7BIT && st->value_msb == 0 )
		: ( val1 == 0 && val2 == MAX_14BIT ) ||
		( val1 == MAX_14BIT && val2 == 0);
	}

	DMESSAGE( "is toggle %i", is_toggle );

	m->is_toggle = is_toggle;
	m->is_nrpn14 = is_14bit_nrpn;

	m->learning_value_msb = m->learning_value_lsb = 0;

	m->signal_name = s;
	m->signal =
	    osc->add_signal( s, OSC::Signal::Output, 0, 1, 0,
			     signal_handler, NULL,
			     m );

	sig_map_ordered[max_signal] = midi_event;

	nsm_send_is_dirty( nsm );

	free(s);
    }

    float val = 0;

    if ( is_nrpn )
    {
	unsigned int fbv = get_14bit( st->value_msb, st->value_lsb );

	if ( m->is_nrpn14 )
	    val = fbv / (float)MAX_14BIT;
	else 			/* also covers toggles */
	    val = st->value_msb / (float)MAX_7BIT;
    }
    else if ( e.opcode() == MIDI::midievent::CONTROL_CHANGE )
	val = e.msb() / (float)MAX_7BIT;
    else if ( e.opcode() == MIDI::midievent::PITCH_WHEEL )
	val = e.pitch() / (float)MAX_14BIT;

    /* DMESSAGE( "Val: %f, sigval %f", val, m->signal->value() ); */

    /* wait for values to sync for continuous controls (faders and knobs) before emitting signal. For toggles, just send it immediately. */
    if (
	 /* magic number to give a release time to prevent thrashing. */	
	m->last_feedback_tick > m->last_midi_tick + 100
	    &&
	 !m->is_toggle )
    {
    	float percent_off = fabs( val - m->signal->value() ) * 100.0f;
		
    	if ( percent_off > 5 )
    	{
    	    DMESSAGE( "Wating for controls to sync. %s: %f percent off target (must be < 5%) [ M:%f S:%f ] ",
    		      m->signal_name.c_str(),
    		      percent_off,
    		      val,
    		      m->signal->value()
    		);
	    
    	    return;
    	}
    }
    
    m->last_midi_tick = buffers;

    /* DMESSAGE( "Sent value: %f", val ); */
    
    m->signal->value( val );
}

void handle_control_change ( nrpn_state *nrpn_state, midievent &e )
{    
    bool emit_one = false;

    char midi_event[51];                    

    struct nrpn_state *st = decode_nrpn( nrpn_state, e, &emit_one );
                    
    if ( st != NULL &&
	 ( VALUE_LSB == st->awaiting ||
	   COMPLETE == st->awaiting ) )
    {

	snprintf( midi_event, 50, "NRPN %d %d", e.channel(), get_14bit( st->control_msb, st->control_lsb ));

	if ( VALUE_LSB == st->awaiting )
	{
		if ( sig_map.find(midi_event) != sig_map.end() )
		{
		    signal_mapping &m = sig_map[midi_event];
		    
		    if ( m.is_nrpn14 )
		    {
			/* we know there's an LSB coming, so hold off on emitting until we get it */
			return;
		    }
		}
	}
	    
	emit_signal_for_event(midi_event, e, st);
    }
	
    if ( st == NULL )
    {
	if ( e.opcode() == MIDI::midievent::CONTROL_CHANGE )
	{
	    snprintf( midi_event, 50, "CC %d %d", e.channel(), e.lsb() );
		
	    emit_signal_for_event( midi_event, e, NULL);
	}
    }
}
    /* else if ( e.opcode() == MIDI::midievent::PITCH_WHEEL ) */
    /*     asprintf( &s, "/midi/%i/PB", e.channel() ); */


int
main ( int /*argc*/, char **argv )
{
    nrpn_state nrpn_state[16];

    memset( &nrpn_state, 0, sizeof(struct nrpn_state) * 16 );
    
    signal( SIGTERM, sigterm_handler );
    signal( SIGHUP, sigterm_handler );
    signal( SIGINT, sigterm_handler );

    nsm = nsm_new();
//    set_nsm_callbacks( nsm );
    
    nsm_set_open_callback( nsm, command_open, 0 );
    nsm_set_broadcast_callback( nsm, command_broadcast, 0 );
    nsm_set_save_callback( nsm, command_save, 0 );
   
    char *nsm_url = getenv( "NSM_URL" );

    if ( nsm_url )
    {
        if ( ! nsm_init( nsm, nsm_url ) )
        {
            nsm_send_announce( nsm, APP_TITLE, ":dirty:", basename( argv[0] ) );

            /* poll so we can keep OSC handlers running in the GUI thread and avoid extra sync */
//            Fl::add_timeout( NSM_CHECK_INTERVAL, check_nsm, NULL );
        }
    }
    else
    {
        delete engine;
        delete nsm;
        fprintf(stderr, "Attention!!! -- midi-mapper-xt can only be used as an NSM client!!!\n");
        exit(0);
    }
    
    osc = new OSC::Endpoint();

    osc->init( LO_UDP, NULL );
    
    osc->add_method( "/non/hello", "ssss", osc_non_hello, osc, "" );
    
    MESSAGE( "OSC URL = %s", osc->url() );

    /* now we just read from the MIDI ringbuffer and output OSC */
    
    DMESSAGE( "waiting for events" );

    /* jack_midi_event_t ev; */
    
    while ( ! got_sigterm )
    {
        osc->wait(20);
        check_nsm();

        if ( ! engine )
            continue;

	midievent e;

        while ( jack_ringbuffer_read( engine->input_ring_buf, (char *)&e, sizeof( midievent ) ) )
        {
	    /* midievent e; */

            /* e.timestamp( ev.time ); */
            /* e.status( ev.buffer[0] ); */
            /* e.lsb( ev.buffer[1] ); */
            /* if ( ev.size == 3 ) */
            /*     e.msb( ev.buffer[2] ); */
	    /* else */
	    /* 	e.msb( 0 ); */

	    /* DMESSAGE( "[%lu] %u %u %u", e.timestamp(), e.status(), e.lsb(), e.msb() ); */
	    
            switch ( e.opcode() )
            {
                case MIDI::midievent::CONTROL_CHANGE:
                case MIDI::midievent::PITCH_WHEEL:
                {
		    handle_control_change(nrpn_state,e);		    
                    break;
                }
                default:
                    break;
            }
//            e.pretty_print();
        }

//    usleep( 500 );
    }

    delete engine;

    return 0;
}
