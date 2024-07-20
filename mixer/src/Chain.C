
/*******************************************************************************/
/* Copyright (C) 2009 Jonathan Moore Liles                                     */
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

/* Filter chain. This would all be much simpler if we chose not to
 * allow non 1:1 plugins to be mixed in a single chain...
 *
 * Supporting the mixture requires duplicating some inputs (to satisfy
 * stereo input plugins reading mono outputs) and duplicating some
 * plugins (to satisfy mono input plugins reading stereo outputs).
 *
 * Basically, what this means is that the intermediate number of
 * buffers need not have any relation to the starting and ending
 * buffer count. (Picture an ambisonic panner going into an ambisonic
 * decoder (1:6:2).
 *
 * The chain will allocate enough buffers to hold data from the
 * maximum number of channels used by a contained module.
 *
 * The process thread goes as follows:
 *
 * 1. Copy inputs to chain buffers.
 *
 * 2. process() each module in turn (reusing buffers in-place) (inputs
 * will be copied or plugins duplicated as necessary)
 *
 * 3. Copy chain buffers to outputs.
 *
 * For chains where the number of channels never exceeds the maximum
 * of the number of inputs and outputs, the first copy can be
 * optimized out.
 */

#include "const.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>    // usleep()

#include "Chain.H"
#include "Module.H"
#include "Meter_Module.H"
#include "JACK_Module.H"
#include "Gain_Module.H"
#include "Plugin_Module.H"
#include "Controller_Module.H"

#ifdef LV2_SUPPORT
#include "lv2/LV2_Plugin.H"
#endif

#ifdef CLAP_SUPPORT
#include "clap/CLAP_Plugin.H"
#endif

#ifdef VST2_SUPPORT
#include "vst2/VST2_Plugin.H"
#endif

#ifdef VST3_SUPPORT
#include "vst3/VST3_Plugin.H"
#endif

#include <FL/Fl_Box.H>
#include <FL/Fl_Menu.H>
#include <FL/fl_ask.H>

#include "../../FL/Fl_Flip_Button.H"
#include "../../FL/Fl_Flowpack.H"
#include "../../FL/Fl_Packscroller.H"
#include "../../FL/menu_popup.H"
#include "../../FL/test_press.H"
#include "../../nonlib/debug.h"
#include "../../nonlib/dsp.h"

#include <FL/Fl_Tabs.H>
#include "FL/Fl_Scroll.H"
#include <FL/fl_draw.H>

#include "Group.H"
#include "Mixer_Strip.H"
#include "Mixer.H"
extern char *instance_name;
static bool is_startup = true;


/* Chain::Chain ( int X, int Y, int W, int H, const char *L ) : */
/*     Fl_Group( X, Y, W, H, L) */
Chain::Chain ( ) : Fl_Group( 0, 0, 100, 100, "")

{
    /* not really deleting here, but reusing this variable */
    _deleting = true;

    int X = 0;
    int Y = 0;
    int W = 100;
    int H = 100;

/*     _outs = 1; */
/*     _ins = 1; */

    _configure_outputs_callback = NULL;

    _strip = NULL;

    _name = NULL;

    labelsize( 10 );
    align( FL_ALIGN_TOP );

    { Fl_Flip_Button* o  = tab_button = new Fl_Flip_Button( X, Y, W, 16, "chain/controls");
        o->type(FL_TOGGLE_BUTTON);
        o->labelsize( 12 );
        o->callback( cb_handle, this );
    }

    Y += 18;
    H -= 18;

    { Fl_Group *g = chain_tab = new Fl_Group( X, Y, W, H, "" );
        g->labeltype( FL_NO_LABEL );
        g->box( FL_FLAT_BOX );
//        o->color( fl_darker( FL_BACKGROUND_COLOR ) );
//        o->color( FL_BACKGROUND_COLOR );
//        o->box( FL_NO_BOX );
        { Fl_Packscroller *o = new Fl_Packscroller( X, Y, W, H );
            o->color( FL_BACKGROUND_COLOR );
//            o->box( FL_FLAT_BOX );
            o->box( FL_THIN_UP_BOX );
            o->type( Fl_Scroll::VERTICAL );
            { Fl_Pack *mp = modules_pack = new Fl_Pack( X, Y, W, H );
                mp->type( Fl_Pack::VERTICAL );
                mp->spacing( 6 );
                mp->end();
                Fl_Group::current()->resizable( mp );
            }
            o->end();
        }
        g->end();
    }
    { Fl_Group *g = control_tab = new Fl_Group( X, Y, W, H, "" );
        g->box( FL_FLAT_BOX );
        g->color( FL_BACKGROUND_COLOR );
        g->labeltype( FL_NO_LABEL );
        g->hide();
        { Fl_Scroll *o = new Fl_Scroll( X, Y, W, H );
            o->color( FL_BACKGROUND_COLOR );
            o->box( FL_NO_BOX );
            o->type( Fl_Scroll::VERTICAL );
            { Fl_Pack *cp = controls_pack = new Fl_Pack( X, Y, W, H );
                cp->type( Fl_Pack::VERTICAL );
                cp->spacing( 5 );
//            cp->color( FL_RED );
                cp->end();
                Fl_Group::current()->resizable( cp );
            }
            o->end();
            Fl_Group::current()->resizable( o );
        }
        g->end();
        g->hide();
        Fl_Group::current()->resizable( g );
    }
    end();

    log_create();

    _deleting = false;
}

Chain::~Chain ( )
{
    DMESSAGE( "Destroying chain" );

    log_destroy();

    _deleting = true;

    /* FIXME: client may already be dead during teardown if group is destroyed first. */
    if ( client() )
	client()->lock();

    for ( unsigned int i = scratch_port.size(); i--; )
        free( static_cast<sample_t*>(scratch_port[i].buffer()) );

    scratch_port.clear();
    
    /* if we leave this up to FLTK, it will happen after we've
     already destroyed the client */
    modules_pack->clear();
    modules_pack = NULL;
    controls_pack->clear();
    modules_pack = NULL;

    if ( client() )
	client()->unlock();
}

Group *
Chain::client ( void ) 
{
    return strip()->group();
}


void
Chain::get ( Log_Entry &e ) const
{
    e.add( ":strip", strip() );
    e.add( ":tab", tab_button->value() ? "controls" : "chain" );
}

void
Chain::set ( Log_Entry &e )
{
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":tab" ) )
        {
            tab_button->value( strcmp( v, "controls" ) == 0 );
            tab_button->do_callback();
        }
        else if ( ! strcmp( s, ":strip" ) )
        {
            unsigned int j;
            sscanf( v, "%X", &j );
            Mixer_Strip *t = static_cast<Mixer_Strip*>(Loggable::find( j ) );

            assert( t );

            t->chain( this );
        }
    }
}


void
Chain::log_children ( void ) const
{
    log_create();

    for ( int i = 0; i < modules(); ++i )
    {
        module(i)->log_create();
    }

    for ( int i = 0; i < controls_pack->children(); ++i )
    {
        const Controller_Module *cm = static_cast<Controller_Module*>( controls_pack->child( i ) );

        cm->log_create();
    }
}

/* Fill this chain with JACK I/O, Gain, and Meter modules. */
void
Chain::initialize_with_default ( void )
{

    { JACK_Module *m = new JACK_Module();
        m->is_default( true );
        m->chain( this );
        m->configure_outputs( 1 );
        add( m );
    }

    { Module *m = new Gain_Module();
        m->is_default( true );
        m->chain(this);
        add( m );
    }

    { Module *m = new Meter_Module();
        m->is_default( true );
        add( m );
    }

    { JACK_Module *m = new JACK_Module();
        m->is_default( true );
        m->chain( this );
        add( m );
    }
}


void Chain::cb_handle(Fl_Widget* o) {
    if ( o == tab_button )
    {
        Fl_Flip_Button *fb = static_cast<Fl_Flip_Button*>( o );

        if ( fb->value() == 0 )
        {
            control_tab->hide();
            chain_tab->show();
        }
        else
        {
            chain_tab->hide();
            control_tab->show();
        }
    }
}

void Chain::cb_handle(Fl_Widget* o, void* v) {
    ((Chain*)(v))->cb_handle(o);
}

void
Chain::remove ( Controller_Module *m )
{
    DMESSAGE( "Removing controller module from chain" );

    client()->lock();

    m->disconnect();

    controls_pack->remove( m );
    modules_pack->remove( m );

    build_process_queue();

    client()->unlock();

    redraw();
}

void
Chain::send_feedback ( bool force )
{
    for ( int i = 0; i < modules(); i++ )
        module(i)->send_feedback( force );
}

void
Chain::schedule_feedback ( void )
{
    for ( int i = 0; i < modules(); i++ )
        module(i)->schedule_feedback();
}

/* remove a module from the chain. this isn't guaranteed to succeed,
 * because removing the module might result in an invalid routing */
bool
Chain::remove ( Module *m )
{
    int i = modules_pack->find( m );

    int ins = 0;

    if ( i != 0 )
        ins = module( i - 1 )->noutputs();

    if ( ! can_configure_outputs( m, ins ) )
    {
        if(!m->is_zero_input_synth())
        {
            fl_alert( "Can't remove module at this point because the resultant chain is invalid" );
            return false;
        }
        else
        {
            /* When we add a zero input synth, we set the jack port outs to zero. So now to make the
               chain valid we reset the jack outs to 1. The only time that JACK == 0 is when we have
               a zero input synth. For clarity, removing a zero input synth would never invalidate
               the chain. But can_configure_outputs() will return false which is how we got here. So
               set the jack outs to 1 and continue with removal. */
            if(module( i - 1 )->is_jack_module() )
            {
                Module *jm = module( i - 1 );
                JACK_Module *j = static_cast<JACK_Module *> (jm);
                j->configure_outputs( 1 );
            }
        }
    }

    /* Flag to tell any Plugin_Modules that custom_data should be set to remove on save.
       This is ignored by modules that don't have custom data. */
    m->_is_removed = true;

    client()->lock();

    strip()->handle_module_removed( m );

    modules_pack->remove( m );

    configure_ports();

    client()->unlock();

    return true;
}

/* determine number of output ports, signal if changed.  */
void
Chain::configure_ports ( void )
{
    int nouts = 0;

    client()->lock();

    for ( int i = 0; i < modules(); ++i )
    {
        module( i )->configure_inputs( nouts );
        nouts = module( i )->noutputs();
    }

    unsigned int req_buffers = required_buffers();

    DMESSAGE( "required_buffers = %i", req_buffers );

    if ( scratch_port.size() < req_buffers )
    {
	for ( unsigned int i = req_buffers - scratch_port.size(); i--; )
        {
            Module::Port p( NULL, Module::Port::OUTPUT, Module::Port::AUDIO );
            p.set_buffer( buffer_alloc( client()->nframes() ) );
            buffer_fill_with_silence( static_cast<sample_t*>( p.buffer() ), client()->nframes() );
            scratch_port.push_back( p );
        }
    }
    else if ( scratch_port.size() > req_buffers )
    {
	for ( unsigned int i = scratch_port.size() - req_buffers; i--; )
	{
	    free(scratch_port.back().buffer());
	    scratch_port.pop_back();
	}
    }

    build_process_queue();

    client()->unlock();

    parent()->redraw();
}

/** invoked from the JACK latency callback... We need to update the latency values on this chains ports */
void
Chain::set_latency ( JACK::Port::direction_e dir )
{
    nframes_t tmax = 0;
    nframes_t tmin = 0;
    nframes_t added_min = 0;
    nframes_t added_max = 0;

    for ( int i = 0; i < modules(); i++ )
    {
        Module *m;

        if ( dir == JACK::Port::Input )
            m = module( i );
        else
            m = module( (modules() - 1) - i );
        
        nframes_t min,max;
        min = max = 0;
        
        nframes_t a = m->get_module_latency();
        
        added_min += a;
        added_max += a;
        
        if ( dir == JACK::Port::Input ? m->aux_audio_input.size() : m->aux_audio_output.size() )
        {
            m->get_latency( dir, &min, &max );
            
            tmin = 0;
            added_min = 0;
        }
        
        if ( min > tmin )
            tmin = min;
        if ( max > tmax )
            tmax = max;
        
        m->set_latency( dir, tmin + added_min, tmax + added_max );
        
    }
}

int 
Chain::get_module_instance_number ( Module *m )
{
    int n = 0;

    for ( int i = 0; i < modules(); ++i )
        if ( ! strcmp( module(i)->base_label(), m->base_label() ) )
            n++;

    return n;
} 

/* calculate the minimum number of buffers required to satisfy this chain */
int
Chain::required_buffers ( void )
{
    int buffers = 0;
    int outs = 0;

    for ( int i = 0; i < modules(); ++i )
    {
        outs = module( i )->can_support_inputs( outs );

        if ( outs > buffers )
            buffers = outs;
    }

    return buffers;
}

/* called by a module when it wants to alter the number of its
 * outputs. Also used to test for chain validity when inserting /
 * removing modules */
bool
Chain::can_configure_outputs ( Module *m, int n ) const
{
    /* start at the requesting module */

    int outs = n;

    int i = modules_pack->find( m );

    if ( modules() - 1 == i )
        /* last module */
        return true;

    for ( i++ ; i < modules(); ++i )
    {
        outs = module( i )->can_support_inputs( outs );

        if ( outs < 0 )
            return false;
    }

    return true;
}

unsigned int
Chain::maximum_name_length ( void )
{
    return JACK::Client::maximum_name_length() - ( strlen( instance_name ) + 1 );
}

void
Chain::freeze_ports ( void )
{
    for ( int i = 0; i < modules(); i++ )
    {
        Module *m = module(i);
        m->freeze_ports();
    }
}

void
Chain::thaw_ports ( void )
{
    for ( int i = 0; i < modules(); i++ )
    {
        Module *m = module(i);
        m->thaw_ports();
    }
}

/* rename chain... we have to let our modules know our name has
 * changed so they can take the appropriate action (in particular the
 * JACK module). */
void
Chain::name ( const char *name )
{
    _name = name;

    if ( strip()->group() )
    {
        if ( strip()->group()->single() )
	    /* we are the owner of this group and its only member, so
	     * rename it */
            strip()->group()->name(name);
    }
    
    for ( int i = 0; i < modules(); ++i )
    {
        module( i )->handle_chain_name_changed();
    }
}

bool
Chain::add ( Module *m )
{
    /* FIXME: hacky */
    if ( !strcmp( m->name(), "Controller" ) )
        return false;
    else
        return insert( NULL, m );
}

bool
Chain::add ( Controller_Module *m )
{
    DMESSAGE( "Adding control" );
    add_control(m);
    return true;
}

bool
Chain::insert ( Module *m, Module *n )
{
    client()->lock();

    Module::sample_rate( client()->sample_rate() );
    n->resize_buffers( client()->nframes() );

    /* inserting a new instance */
    if ( -1 == n->number() ) 
	n->number( get_module_instance_number( n ) );
    
    if ( !m )
    {
        if ( modules() == 0 && n->can_support_inputs( 0 ) >= 0 )
        {
            n->chain( this );
            n->configure_inputs( 0 );
            modules_pack->add( n );

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
            n->configure_midi_inputs();
            n->configure_midi_outputs();
#endif
        }
        /* This is used when loading from project file */
        else if ( n->can_support_inputs( module( modules() - 1 )->noutputs() ) >= 0 )
        {
            n->chain( this );
            n->configure_inputs( module( modules() - 1 )->noutputs() );
            modules_pack->add( n );
    
#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
            n->configure_midi_inputs();
            n->configure_midi_outputs();

            /* Eliminate the JACK input when we have a zero synth s*/
            if (n->is_zero_input_synth())
            {
                if(module( 0 )->is_jack_module() )
                {
                    Module *jm = module( 0 );
                    JACK_Module *j = static_cast<JACK_Module *> (jm);
                    j->configure_outputs( 0 );
                }
            }
#endif
        }
        else
        {
            DMESSAGE( "Module says it can't support %i inputs", module( modules() - 1 )->noutputs() );

            goto err;
        }
    }
    else
    {
        int i = modules_pack->find( m );

        n->chain( this );

        if ( 0 == i )
        {
            /* Don't allow insertion of zero_input synths before the JACK module */
            if (n->is_zero_input_synth())
                goto err;

            DMESSAGE("Inserting to head of chain");
            if ( n->can_support_inputs( 0 ) >= 0 )
                n->configure_inputs( 0 );
            else
                goto err;
        }
        else    // This is the plugin module
        {
            int jack_module = i - 1;
            if(jack_module < 0)
            {
                DMESSAGE("Attempting to Insert before JACK module!!");
                goto err;
            }
            else
            {
                DMESSAGE("After JACK Module = %s", module( jack_module )->is_jack_module() ? "true" : "false");
            }

            /* If the plugin has zero inputs then it can only be inserted directly after a JACK module */
            if (!module( jack_module )->is_jack_module() && n->is_zero_input_synth())
            {
                goto err;
            }

            /* User is trying to insert something before an existing zero input synth */
            if (module( i )->is_zero_input_synth())
            {
                goto err;
            }

            if ( n->can_support_inputs(  module( i - 1 )->noutputs() ) >= 0 )
            {
                n->configure_inputs(  module( i - 1 )->noutputs() );

                m->configure_inputs( n->noutputs() );

                for ( int j = i + 1; j < modules(); ++j )
                    module( j )->configure_inputs( module( j - 1 )->noutputs() );
            }
            else
            {
                goto err;
            }

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
            n->configure_midi_inputs();
            n->configure_midi_outputs();

            /* For zero input synths, set the JACK out port to zero since JACK input is invalid */
            if (n->is_zero_input_synth())
            {
                if(module( jack_module )->is_jack_module() )
                {
                    Module *jm = module( i - 1 );
                    JACK_Module *j = static_cast<JACK_Module *> (jm);
                    j->configure_outputs( 0 );
                }
            }
#endif
        }

        modules_pack->insert( *n, i );
    }


    strip()->handle_module_added( n );

    configure_ports();

    client()->unlock();

    DMESSAGE( "Module \"%s\" has %i:%i audio and %i:%i control ports",
              n->name(),
              n->ninputs(),
              n->noutputs(),
              n->ncontrol_inputs(),
              n->ncontrol_outputs() );

    n->initialize();
    return true;

err:

    /* If the plugin has MIDI, the JACK ports will not get configured on err,
    so on module delete, we destroy all related JACK ports. This is not a problem for audio
    ins and outs above since they do not have JACK ports. Since the failure
    above meant we don't have JACK ports created for MIDI, we clear any MIDI
    vectors here so the JACK port deletion does not get called on NULL ports and crash */
#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
    n->clear_midi_vectors();
#endif

    client()->unlock();

    DMESSAGE( "Insert failed" );

    return false;
}

/* add a control to the control strip. Assumed to already be connected! */
void
Chain::add_control ( Controller_Module *m )
{
    client()->lock();

    controls_pack->add( m );

    configure_ports();

    client()->unlock();

    controls_pack->redraw();
}

void
Chain::draw_connections ( Module *m )
{
    int spacing;
    int offset;

    int X, Y, W, H;

    ( static_cast<Fl_Packscroller*>( chain_tab->child( 0 ) ) )->bbox( X, Y, W, H );

    fl_push_clip( X, Y, W, H );

    Fl_Color c = FL_FOREGROUND_COLOR;
    fl_color( c );

    if ( m->ninputs() )
    {
        spacing = w() / m->ninputs();
        offset = spacing / 2;

        for ( int i = m->ninputs(); i--; )
            fl_rectf( m->x() + offset + ( spacing * i ), m->y() - 3, 2, 3 );
    }

    fl_color( fl_darker( c ) );

    if ( m->noutputs() )
    {
        spacing = w() / m->noutputs();
        offset = spacing / 2;
        for ( int i = m->noutputs(); i--; )
            fl_rectf( m->x() + offset + ( spacing * i ), m->y() + m->h(), 2, 3 );
    }

    fl_pop_clip();
}

void
Chain::add_to_process_queue ( Module *m )
{
    for ( std::list<Module*>::const_iterator i = process_queue.begin(); i != process_queue.end(); ++i )
        if ( m == *i )
            return;

    process_queue.push_back( m );
}

/* run any time the internal connection graph might have
 * changed... Tells the process thread what order modules need to be
 * run in. */
void
Chain::build_process_queue ( void )
{
    client()->lock();
    
    process_queue.clear();

    for ( int i = 0; i < modules(); ++i )
    {
        Module *m = static_cast<Module*>( module( i ) );

        /* controllers */
        for ( unsigned int j = 0; j < m->control_input.size(); ++j )
        {
            if ( m->control_input[j].connected() )
            {
                add_to_process_queue( m->control_input[j].connected_port()->module() );
            }
        }

        /* audio modules */
        add_to_process_queue( m );

        /* indicators */
        for ( unsigned int j = 0; j < m->control_output.size(); ++j )
        {
            if ( m->control_output[j].connected() )
            {
                add_to_process_queue( m->control_output[j].connected_port()->module() );
            }
        }
    }

    /* connect all the ports to the buffers */
    for ( int i = 0; i < modules(); ++i )
    {
        // This can happen when a zero input synth cannot be loaded.
        // We give users a warning but this causes crash so lets not do that.
        if(scratch_port.size() == 0)
            break;

        Module *m = module( i );
        
        for ( unsigned int j = 0; j < m->audio_input.size(); ++j )
        {
            m->audio_input[j].set_buffer( scratch_port[j].buffer() );
        }
        for ( unsigned int j = 0; j < m->audio_output.size(); ++j )
        {
            m->audio_output[j].set_buffer( scratch_port[j].buffer() );
        }

        m->handle_port_connection_change();
    }

/*     DMESSAGE( "Process queue looks like:" ); */

/*     for ( std::list<Module*>::const_iterator i = process_queue.begin(); i != process_queue.end(); ++i ) */
/*     { */
/*         const Module* m = *i; */

/*         if ( m->audio_input.size() || m->audio_output.size() ) */
/*             DMESSAGE( "\t%s", (*i)->name() ); */
/*         else if ( m->control_output.size() ) */
/*             DMESSAGE( "\t%s -->", (*i)->name() ); */
/*         else if ( m->control_input.size() ) */
/*             DMESSAGE( "\t%s <--", (*i)->name() ); */

/*         { */
/*             char *s = m->get_parameters(); */

/*             DMESSAGE( "(%s)", s ); */

/*             delete[] s; */
/*         } */
/*     } */

    client()->unlock();
}

void
Chain::strip ( Mixer_Strip * ms )
{
    _strip = ms;
}


void
Chain::draw ( void )
{
    Fl_Group::draw();

/*     if ( 0 == strcmp( "Chain", tabs->value()->label() ) ) */
    if ( chain_tab->visible() )
        for ( int i = 0; i < modules(); ++i )
            draw_connections( module( i ) );
}

void
Chain::resize ( int X, int Y, int W, int H )
{
    Fl_Group::resize( X, Y, W, H );

/* this won't naturally resize because it's inside of an Fl_Scroll... */
    controls_pack->size( W, controls_pack->h() );
}

void
Chain::get_output_ports ( std::list<std::string> &sl)
{
    for ( int i = 0; i < modules(); i++ )
    {
        Module *m = module(i);
        
        for ( unsigned int j = 0; j < m->aux_audio_output.size(); j++ )
        {
            char *s;

            asprintf( &s, "%s/%s", 
                      "*", 
                      m->aux_audio_output[j].jack_port()->name() );

            sl.push_back( s );

            free(s);

            if ( ! strip()->group()->single() )
            {
                asprintf( &s, "%s/%s", 
                          strip()->group()->name(), 
                          m->aux_audio_output[j].jack_port()->name() );

                
                sl.push_back( s );
                
                free(s);
            }
        }
    }
}

void
Chain::auto_connect_outputs ( void )
{
    for ( int i = 0; i < modules(); i++ )
    {
        module(i)->auto_connect_outputs();
    }
}

void
Chain::auto_disconnect_outputs ( void )
{
    for ( int i = 0; i < modules(); i++ )
    {
        module(i)->auto_disconnect_outputs();
    }
}



/*****************/
/* Import/Export */
/*****************/

void
Chain::snapshot ( void *v )
{
    ((Chain*)v)->snapshot();
}

void
Chain::snapshot ( void )
{
    log_children();
}

bool
Chain::do_export ( const char *filename )
{
    MESSAGE( "Exporting chain state" );
    Loggable::snapshot_callback( &Chain::snapshot, this );
    Loggable::snapshot( filename );
    return true;
}


/**********/
/* Client */
/**********/

void
Chain::process ( nframes_t nframes )
{
    for ( std::list<Module*>::const_iterator i = process_queue.begin(); i != process_queue.end(); ++i )
    {
        if(_deleting)
            return;
        
	Module *m = *i;
	    
	m->process( nframes );
    }
}

void
Chain::buffer_size ( nframes_t nframes )
{
    for ( unsigned int i = scratch_port.size(); i--; )
        free(scratch_port[i].buffer());
    scratch_port.clear();

    configure_ports();

    Module::set_buffer_size ( nframes );
    for ( int i = 0; i < modules(); ++i )
    {
        Module *m = module(i);

        m->resize_buffers( nframes );
    }
}

int
Chain::sample_rate_change ( nframes_t nframes )
{
    Module::sample_rate ( nframes );
    for ( int i = 0; i < modules(); ++i )
    {
        Module *m = module(i);

        m->handle_sample_rate_change( nframes );
    }

    return 0;
}

/* handle jack port connection change */
void
Chain::port_connect ( jack_port_id_t a, jack_port_id_t b, int /*connect*/ )
{
    if ( _deleting )
        return;

    /* this is called from JACK non-RT thread... */

    if ( jack_port_is_mine( client()->jack_client(), jack_port_by_id( client()->jack_client(), a ) ) ||
         jack_port_is_mine( client()->jack_client(), jack_port_by_id( client()->jack_client(), b ) ))
    {
        /* When the mixer is first starting under NSM, the call to Fl::awake would sometimes
           occur before the initial main() Fl:wait() which would cause an intermittent segfault.
           So the usleep(50000) is to allow the main() time to initialize before. */
        if(is_startup)
        {
            is_startup = false;
            usleep(50000);
        }

        /* Fl::awake() means use the main thread to process. Needed because a race condition would occur
           if connections are changed when the UI is visible and redraw is triggered from multiple events. */
        Fl::awake( Chain::update_connection_status, this );
    }
}

void
Chain::update ( void )
{
    for ( int i = 0; i < controls_pack->children(); ++i )
    {
        Controller_Module *cm = static_cast<Controller_Module*>( controls_pack->child( i ) );
        cm->update();
    }

    for ( int i = 0; i < modules(); i++ )
    {
        Module *m = module(i);
        m->update();
    }
}

void
Chain::update_connection_status ( void *v )
{
    ((Chain*)v)->update_connection_status();
}

void
Chain::update_connection_status ( void )
{
    for ( int i = 0; i < modules(); i++ )
    {
        Module *m = module(i);
        
        if ( !strcmp( m->basename(), "JACK" ) )
        {
            ((JACK_Module*)m)->update_connection_status();
        }
    }
}
