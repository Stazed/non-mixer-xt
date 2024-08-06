
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

/* Filter module. Can host LADPSA Plugins, or can be inherited from to make internal
   modules with special features and appearance. */

#include "const.h"

#include <string.h>
#include <string>
#include <stdlib.h>
#include <FL/fl_draw.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_ask.H>

#include "Plugin_Module.H"
#include "Mixer_Strip.H"
#include "Chain.H"

#include "../../nonlib/debug.h"

static bool warn_legacy_once = false;

Plugin_Module::Plugin_Module( ) :
    Module( 50, 35, name( ) ),
    _last_latency( 0 ),
    _plugin_ins( 0 ),
    _plugin_outs( 0 ),
    _crosswire( false ),
    _latency( 0 )
{
    color ( fl_color_average ( fl_rgb_color ( 0x99, 0x7c, 0x3a ), FL_BACKGROUND_COLOR, 1.0f ) );

    end ( );

    Plugin_Module::init ( );
    log_create ( );
}

Plugin_Module::~Plugin_Module( )
{
    log_destroy ( );
}

void
Plugin_Module::init( void )
{
    /* module will be bypassed until plugin is loaded */
    *( (float*) _bypass ) = 1.0f;

    align ( (Fl_Align) FL_ALIGN_CENTER | FL_ALIGN_INSIDE );
    //     color( (Fl_Color)fl_color_average( FL_MAGENTA, FL_WHITE, 0.5f ) );

    int tw, th, tx, ty;

    bbox ( tx, ty, tw, th );
}

void
Plugin_Module::update( void )
{
    if ( _last_latency != _latency )
    {
        DMESSAGE ( "Plugin latency changed to %lu", (unsigned long) _latency );

        chain ( )->client ( )->recompute_latencies ( );
    }

    _last_latency = _latency;

    update_tooltip ( );
}

int
Plugin_Module::can_support_inputs( int n )
{
    /* The synth case, 0 ins any outs. For these we only allow to add
       a zero synth if the JACK ins are 1. ie. n == 1 */
    if ( plugin_ins ( ) == 0 && ( n == 1 ) )
        return plugin_outs ( );

    /* this is the simple case */
    if ( plugin_ins ( ) == n )
        return plugin_outs ( );
        /* e.g. MONO going into STEREO */
        /* we'll duplicate our inputs */
    else if ( n < plugin_ins ( ) &&
              1 == n )
    {
        return plugin_outs ( );
    }
        /* e.g. STEREO going into MONO */
        /* we'll run multiple instances of the plugin */
        /* Only for LADSPA and LV2 */
    else if ( n > plugin_ins ( ) &&
              ( plugin_ins ( ) == 1 && plugin_outs ( ) == 1 ) )
    {
        if ( ( _plug_type == Type_CLAP ) || ( _plug_type == Type_VST2 ) ||
             ( _plug_type == Type_VST3 ) )
        {
            return -1; // Don't support multiple instances
        }
        else // LADSPA & LV2 can run multiple instance
            return n;
    }

    return -1;
}

bool
Plugin_Module::configure_inputs( int /*n*/ )
{
    return false;
}

void
Plugin_Module::resize_buffers( nframes_t buffer_size )
{
    Module::resize_buffers ( buffer_size );
}

/**
 This generates the plugin state save file/directory we use for customData.
 It generates the random directory suffix '.nABCD' to support multiple instances.
 */
std::string
Plugin_Module::get_custom_data_location( const std::string &path )
{
    std::string project_base = path;

    if ( project_base.empty ( ) )
        return "";

    std::string slabel = "/";
    slabel += label ( );

    /* Just replacing spaces in the plugin label with '_' cause it looks better */
    std::replace ( slabel.begin ( ), slabel.end ( ), ' ', '_' );

    project_base += slabel;

    /* This generates the random directory suffix '.nABCD' to support multiple instances */
    char id_str[6];

    id_str[0] = 'n';
    id_str[5] = 0;

    for ( int i = 1; i < 5; i++ )
        id_str[i] = 'A' + ( rand ( ) % 25 );

    project_base += ".";
    project_base += id_str;

    DMESSAGE ( "project_base = %s", project_base.c_str ( ) );

    return project_base;
}

void
Plugin_Module::set( Log_Entry & )
{
    if ( !warn_legacy_once )
    {
        fl_alert ( "Non-mixer-xt ERROR - This snapshot contains legacy unsupported modules.\n"
                "See Help/Projects to convert to the new format!" );

        warn_legacy_once = true;
    }
}
