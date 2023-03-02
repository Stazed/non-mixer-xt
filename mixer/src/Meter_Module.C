
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

#include "const.h"

#include <math.h>
#include <FL/Fl.H>
#include <FL/Fl_Single_Window.H>
#include <FL/fl_draw.H>

#include "../../FL/Fl_Scalepack.H"
#include "../../FL/test_press.H"
#include "../../nonlib/JACK/Port.H"

#include "Meter_Module.H"
#include "DPM.H"


Meter_Module::Meter_Module ( )
    : Module ( 50, 100, name() )
{
    box( FL_FLAT_BOX );
    dpm_pack = new Fl_Scalepack( x() + 2, y() + 2, w() - 4, h() - 4 );
    dpm_pack->type( FL_HORIZONTAL );
    dpm_pack->spacing( 1 );
    
    control_value = 0;
    peaks = 0;
    meter_sample_period_count = 0;
    meter_sample_periods = 0;
    
    color( fl_darker( fl_darker( FL_BACKGROUND_COLOR )));

    end();

    // private port for peak meter ui
    Port p( this, Port::OUTPUT, Port::CONTROL );
    p.hints.type = Port::Hints::LINEAR;
    p.hints.ranged = true;
    p.hints.maximum = 10.0f;
    p.hints.minimum = 0.0f;
    p.hints.dimensions = 1;
    p.connect_to( new float[1] );
    p.control_value_no_callback( 0 );

    // public port for meter level
    Port p2( this, Port::OUTPUT, Port::CONTROL, "Level (dB)" );
    p2.hints.type = Port::Hints::LINEAR;
    p2.hints.ranged = true;
    p2.hints.maximum = 6.0f;
    p2.hints.minimum = -70.0f;
    p2.hints.dimensions = 1;
    p2.connect_to( new float[1] );
    p2.control_value_no_callback( 0 );

    add_port( p );
    add_port( p2 );

    log_create();
}

Meter_Module::~Meter_Module ( )
{
    if ( control_value )
        delete[] control_value;

    log_destroy();
}





void Meter_Module::resize ( int X, int Y, int W, int H )
{
    Fl_Group::resize(X,Y,W,H);
    dpm_pack->resize( x() + 2, y() + 2, w() - 4, h() - 4 );
}

void
Meter_Module::draw ( void )
{
    /* draw_box(x(),y(),w(),h()); */

    Fl_Group::draw();
    
    fl_rect( x(), y(), w(), h(), fl_darker(FL_BACKGROUND_COLOR));
    fl_rect( x()+1, y()+1, w()-2, h()-2, fl_darker(fl_darker(FL_BACKGROUND_COLOR)));
}

void
Meter_Module::update ( void )
{
    float dB = -70.0;

    for ( int i = dpm_pack->children(); i--; )
    {
	DPM* o = ((DPM*)dpm_pack->child( i ));

	const float v = CO_DB( control_value[i] );
	
    // use loudest channel for public meter level
    if ( v > dB )
        dB = v;

	if ( v > o->value() )
	    o->value( v );

	o->update();
	
        control_value[i] = 0;
    }

    control_output[1].control_value_no_callback(dB);

}

bool
Meter_Module::configure_inputs ( int n )
{
    THREAD_ASSERT( UI );

    int on = audio_input.size();

    if ( n > on )
    {
        for ( int i = on; i < n; ++i )
        {
            DPM *dpm = new DPM( 0, 0, w(), h() );
            dpm->type( FL_VERTICAL );
            align( (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_INSIDE ) );

            dpm_pack->add( dpm );

            add_port( Port( this, Port::INPUT, Port::AUDIO ) );
            add_port( Port( this, Port::OUTPUT, Port::AUDIO ) );
        }
    }
    else
    {
        for ( int i = on; i > n; --i )
        {
            DPM *dpm = (DPM*)dpm_pack->child( dpm_pack->children() - 1 );
            dpm_pack->remove( dpm );
            delete dpm;

            audio_input.back().disconnect();
            audio_input.pop_back();
            audio_output.back().disconnect();
            audio_output.pop_back();

	    smoothing.pop_back();
        }
    }

    	    /* DMESSAGE( "sample rate: %lu, nframes: %lu", sample_rate(), this->nframes() ); */

    control_output[0].hints.dimensions = n;
    delete[] (float*)control_output[0].buffer();
    {
        float *f = new float[n];

        for ( int i = n; i--; )
            f[i] = 0;

        control_output[0].connect_to( f );
    }

    if ( control_value )
        delete [] control_value;

    control_value = new float[n];
    for ( int i = n; i--; )
        control_value[i] = 0;

    if ( control_output[0].connected() )
        control_output[0].connected_port()->module()->handle_control_changed( control_output[0].connected_port() );

    return true;
}



int
Meter_Module::handle ( int m )
{
    switch ( m )
    {
        case FL_PUSH:
        {
            int r = 0;
            if ( test_press( FL_BUTTON1 ) )
            {
                /* don't let Module::handle eat our click */
                r = Fl_Group::handle( m );
            }
            return Module::handle( m ) || r;
        }
    }

    return Module::handle( m );
}



/**********/
/* Engine */
/**********/


void
Meter_Module::process ( nframes_t nframes )
{
    for ( unsigned int i = 0; i < audio_input.size(); ++i )
    {
	const float peak = buffer_get_peak( (sample_t*) audio_input[i].buffer(), nframes );
	
	/* const float RMS = sqrtf( peak / (float)nframes); */

	/* since the GUI only updates at 20 or 30hz, there's no point in doing this more often than necessary. */

	/* need to store this separately from other peaks as it must be reset each time we do a round of smoothing output */
		
	/* store peak value */
	if ( peak > ((float*)control_output[0].buffer())[i] )
	    ((float*)control_output[0].buffer())[i] = peak;

	if ( peak > control_value[i] )
	    control_value[i] = peak;
    }
}
