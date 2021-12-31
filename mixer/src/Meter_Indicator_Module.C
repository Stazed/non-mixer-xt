
/*******************************************************************************/
/* Copyright (C) 2010 Jonathan Moore Liles                                     */
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

#include "Meter_Indicator_Module.H"

#include <stdio.h>

#include <FL/Fl.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Light_Button.H>

#include "FL/Fl_Dial.H"
#include "FL/Fl_Labelpad_Group.H"
#include "FL/Fl_Scalepack.H"
#include <FL/fl_draw.H>

#include "Chain.H"
#include "DPM.H"
#include "dsp.h"

#include "FL/test_press.H"


const int DX = 1;

Meter_Indicator_Module::Meter_Indicator_Module ( bool is_default )
    : Module ( is_default, 50, 100, name() )
{
    box( FL_FLAT_BOX );
    /* color( fl_darker( fl_darker( FL_BACKGROUND_COLOR ))); */
    color( FL_BLACK );

    _disable_context_menu = false;
    _pad = true;
    control_value = 0;

    add_port( Port( this, Port::INPUT, Port::CONTROL ) );

    control_input[0].hints.visible = false;

    /* dpm_pack = new Fl_Scalepack( x() + 2, y() + 2, w() - 4, h() - 4 ); */
    /* /\* dpm_pack->color( FL_BACKGROUND_COLOR ); *\/ */
    /* dpm_pack->box( FL_NO_BOX ); */
    /* dpm_pack->type( FL_HORIZONTAL ); */
    /* dpm_pack->spacing(1); */

        dpm_pack = new Fl_Scalepack( x() + 20 + 2, y() + 2, w() - 20 - 4, h() - 4 );
    /* dpm_pack->color( FL_BACKGROUND_COLOR ); */
    dpm_pack->box( FL_NO_BOX );
    dpm_pack->type( FL_HORIZONTAL );
    dpm_pack->spacing(1);

     end();

     control_value = new float[1*2];
     control_value[0] = 
	 control_value[1] = 0;

    align( (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_INSIDE ) );

    clear_visible_focus();
}

Meter_Indicator_Module::~Meter_Indicator_Module ( )
{
    if ( control_value )
    {
        delete[] control_value;
        control_value = NULL;
    }

    log_destroy();
}



void Meter_Indicator_Module::resize ( int X, int Y, int W, int H )
{
    Fl_Group::resize(X,Y,W,H);
    dpm_pack->resize( x() + 20 + DX , y() + DX, w() - 20 - DX*2, h() - DX*2);
}

void
Meter_Indicator_Module::draw ( void )
{
    /* if ( damage() & FL_DAMAGE_ALL ) */

    /* draw_box(x(),y(),w(),h()); */
    Fl_Group::draw();

    if ( damage() & FL_DAMAGE_ALL )
    {
	/* need to trigger redraw of exterior label */
	if ( dpm_pack->children() )
	{
	    ((DPM*)dpm_pack->child(0))->public_draw_label( x(), y(), 19, h() );
	}
    }
    
    fl_rect( x(), y(), w(), h(), fl_darker(fl_darker(FL_BACKGROUND_COLOR)));
}

void
Meter_Indicator_Module::get ( Log_Entry &e ) const
{

    Port *p = control_input[0].connected_port();
    Module *m = p->module();

    e.add( ":module", m );
    e.add( ":port", m->control_output_port_index( p ) );

    Module::get( e );
}

void
Meter_Indicator_Module::set ( Log_Entry &e )
{
    Module::set( e );

    int port = -1;
    Module *module = NULL;

    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":port" ) )
        {
            port = atoi( v );
        }
        else if ( ! strcmp( s, ":module" ) )
        {
            int i;
            sscanf( v, "%X", &i );
            Module *t = (Module*)Loggable::find( i );

            assert( t );

            module = t;
        }
    }

    if ( port >= 0 && module )
        control_input[0].connect_to( &module->control_output[port] );
}



void
Meter_Indicator_Module::update ( void )
{
    if ( control_input[0].connected() )
    {
        // A little hack to detect that the connected module's number
        // of control outs has changed.
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
        Port *p = control_input[0].connected_port();
_Pragma("GCC diagnostic pop")
        
	for ( int i = 0; i < dpm_pack->children(); ++i )
	{
	    DPM *o = (DPM*)dpm_pack->child(i);

	    float dB = CO_DB( control_value[i] );
	    
	    if ( dB > o->value() )
		o->value( dB );

	    o->update();
	    /* else */
	    /* { */
	    /* 	/\* do falloff *\/ */
	    /* 	float f = o->value() - 0.75f; */
	    /* 	if ( f < -70.0f ) */
	    /* 	    f = -70.0f; */
		
	    /* 	o->value( f ); */
	    /* } */

	    control_value[i] = 0;
	}
    }
}

void
Meter_Indicator_Module::connect_to ( Port *p )
{
    control_input[0].connect_to( p );

    /* DPM *o = new DPM( 10, 10, 10, 10 ); */
    /* o->type( FL_VERTICAL ); */

    /* dpm_pack->add( o ); */

    redraw();
}



int
Meter_Indicator_Module::handle ( int m )
{
    switch ( m )
    {
        case FL_PUSH:
        {
            if ( Fl::event_button3() && _disable_context_menu )
                return 0;

            if ( test_press( FL_BUTTON1 ) )
            {
                /* don't let Module::handle eat our click */
                return Fl_Group::handle( m );
            }
        }
    }

    return Module::handle( m );
}



void
Meter_Indicator_Module::handle_control_changed ( Port *p )
{
    THREAD_ASSERT( UI );

    /* The engine is already locked by the UI thread at this point in
     the call-graph, so we can be sure that process() won't be
     executed concurrently. */
    if ( p->connected() )
    {
        p = p->connected_port();

        if ( dpm_pack->children() != p->hints.dimensions )
        {
            dpm_pack->clear();

            control_value = new float[p->hints.dimensions];

            for ( int i = 0; i < p->hints.dimensions; i++ )
            {
                DPM *dpm = new DPM( x(), y(), w(), h() );
                dpm->type( FL_VERTICAL );
                align( (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_INSIDE ) );

                dpm_pack->add( dpm );
                dpm_pack->redraw();

                control_value[i] = 0;
				
                dpm->value( CO_DB( control_value[i] ));
                
            }

            redraw();
        }
    }
}

/**********/
/* Engine */
/**********/

void
Meter_Indicator_Module::process ( nframes_t )
{
    if ( control_input[0].connected() )
    {
        Port *p = control_input[0].connected_port();
	
	volatile float *cv = control_value;
	float *pv = (float*)control_input[0].buffer();

        for ( int i = 0; i < p->hints.dimensions; ++i )
	  {
	      /* peak value since we last checked */
	      if ( *pv > *cv )
	      {
		  *cv = *pv;
		  /* reset now that we've got it */
		  *pv = 0;
	      }

	      cv++;
	      pv++;
	  }
    }
}
