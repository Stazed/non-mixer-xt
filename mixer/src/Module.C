
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

#include "Module.H"
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Enumerations.H>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "Module_Parameter_Editor.H"
#include "Chain.H"

#include "JACK_Module.H"
#include "Gain_Module.H"
#include "Mono_Pan_Module.H"
#include "Meter_Module.H"
#include "Plugin_Module.H"

#ifdef LV2_SUPPORT
#include "lv2/LV2_Plugin.H"
#endif
#ifdef CLAP_SUPPORT
#include "clap/CLAP_Plugin.H"
#endif
#ifdef LADSPA_SUPPORT
#include "ladspa/LADSPA_Plugin.H"
#endif
#ifdef VST3_SUPPORT
#include "vst3/VST3_Plugin.H"
#endif

#include "AUX_Module.H"
#include "Spatializer_Module.H"

#include "../../FL/focus_frame.H"
#include "../../FL/test_press.H"
#include "../../FL/menu_popup.H"
#include "../../nonlib/string_util.h"

#include <FL/Fl_Menu_Button.H>
#include "Mixer.H"

#include "Plugin_Chooser.H"

#include "time.h"


extern char *clipboard_dir;
nframes_t Module::_buffer_size = 0;
nframes_t Module::_sample_rate = 0;
Module *Module::_copied_module_empty = 0;
char *Module::_copied_module_settings = 0;


Module::Module ( int W, int H, const char *L ) :
    Fl_Group( 0, 0, W, H, L ),
    _instances(1),
    _chain(0),
    _is_default(false),
    _is_jack_module(false),
    _is_zero_synth(false),
    _has_name_change(false),
    _base_label(NULL),
    _nframes(0),
    _number(-2),    /* magic number indicates old instance, before numbering */
    _editor(0),
    _plug_type(NONE),
    _is_from_custom_ui(false),
    _is_removed(false),
    _use_custom_data(false)
{
    Module::init();
}

Module::Module ( bool is_default, int W, int H, const char *L ) :
    Fl_Group( 0, 0, W, H, L ),
    Loggable( !is_default ),
    _instances(1),
    _chain(0),
    _is_default(is_default),
    _is_jack_module(false),
    _is_zero_synth(false),
    _has_name_change(false),
    _base_label(NULL),
    _nframes(0),
    _number(-2),    /* magic number indicates old instance, before numbering */
    _editor(0),
    _plug_type(NONE),
    _is_from_custom_ui(false),
    _is_removed(false),
    _use_custom_data(false)
{
    Module::init();
}

Module::Module ( ) :
    Fl_Group( 0, 0, 50, 50, "Unnamed" ),
    _instances(1),
    _chain(0),
    _is_default(false),
    _is_jack_module(false),
    _is_zero_synth(false),
    _has_name_change(false),
    _base_label(NULL),
    _nframes(0),
    _number(-2),    /* magic number indicates old instance, before numbering */
    _editor(0),
    _plug_type(NONE),
    _is_from_custom_ui(false),
    _is_removed(false),
    _use_custom_data(false)
{
    Module::init();
}

Module::~Module ( )
{
    /* we assume that the client for this chain is already locked */
    if ( _bypass )
    {
        delete _bypass;
        _bypass = NULL;
    }

    if ( _editor )
    {
        delete _editor;
        _editor = NULL;
    }
    
    for ( unsigned int i = 0; i < audio_input.size(); ++i )
        audio_input[i].disconnect();
    for ( unsigned int i = 0; i < audio_output.size(); ++i )
        audio_output[i].disconnect();
    for ( unsigned int i = 0; i < aux_audio_input.size(); ++i )
    {
        aux_audio_input[i].disconnect();
        aux_audio_input[i].jack_port()->shutdown();
        delete aux_audio_input[i].jack_port();
    }    
    for ( unsigned int i = 0; i < aux_audio_output.size(); ++i )
    {
        aux_audio_output[i].disconnect();
        aux_audio_output[i].jack_port()->shutdown();
        delete aux_audio_output[i].jack_port();
    }

    destroy_connected_controller_module();

    aux_audio_output.clear();
    aux_audio_input.clear();
    
    audio_input.clear();
    audio_output.clear();

    control_input.clear();
    control_output.clear();

    if ( parent() )
        parent()->remove( this );
}


void
Module::init ( void )
{
    /* we use pointers to these vector elements for port auto connection stuff and need to prevent reallocation from invalidating them. */
    audio_input.reserve(MAX_PORTS);
    audio_output.reserve(MAX_PORTS);
    control_input.reserve(MAX_PORTS);
    control_output.reserve(MAX_PORTS);
    aux_audio_input.reserve(MAX_PORTS);
    aux_audio_output.reserve(MAX_PORTS);
    
    _bypass = new float(0);

    box( FL_UP_BOX );
    labeltype( FL_NO_LABEL );
    align( FL_ALIGN_CENTER | FL_ALIGN_INSIDE );
    set_visible_focus();
    selection_color( FL_YELLOW );

    labelsize(12);

    color( fl_color_average( fl_rgb_color( 0x3a, 0x99, 0x7c ), FL_BACKGROUND_COLOR, 1.0f ));

    tooltip();
}

void
Module::update_tooltip ( void )
{
    char *s;
    asprintf( &s, "Left click to edit parameters; Ctrl + left click to select; right click or MENU key for menu. (info: latency: %lu)", (unsigned long) get_module_latency() );

    copy_tooltip(s);
    free(s);
}

void
Module::get ( Log_Entry &e ) const
{
//    e.add( ":name",            label()           );
//    e.add( ":color",           (unsigned long)color());

    /* If using state restore then all the parameter are stored in the custom data file */
    if(!_use_custom_data)
    {
        char *s = get_parameters();
        if ( strlen( s ) )
            e.add( ":parameter_values", s );

        delete[] s;
    }

    e.add( ":is_default", is_default() );
    e.add( ":chain", chain() );
    e.add( ":active", ! bypass() );
    if ( number() >= 0 )
	e.add( ":number", number() );
}

bool
Module::copy ( void ) const
{
    Module *m = clone_empty();

    if ( ! m )
    {
        DMESSAGE( "Module \"%s\" doesn't support cloning", name() );
        return false;
    }

    Log_Entry *ne = new Log_Entry();

    _copied_module_empty = m;

    {
        Log_Entry e;

        if (clipboard_dir)
        {
            export_import_strip = clipboard_dir;
            export_import_strip += "/clipboard.strip";
        }

        get( e );

        for ( int i = 0; i < e.size(); ++i )
        {
            const char *s, *v;

            e.get( i, &s, &v );

            /* we don't want this module to get added to the current
               chain... */
            if ( !( !strcmp( s, ":chain" ) ||
                    !strcmp( s, ":is_default" ) ||
		    !strcmp( s, ":number" ) ) )
            {
                DMESSAGE( "%s = %s", s, v );
                ne->add_raw( s, v );
            }
        }

        export_import_strip = "";
    }

    _copied_module_settings = ne->print();

    return true;
}

    
void
Module::paste_before ( void )
{
    Module *m = _copied_module_empty;

    Log_Entry le( _copied_module_settings );
    le.remove( ":chain" );

    char *print = le.print();

    DMESSAGE( "Pasting settings: %s", print );

    free( print );

    if (clipboard_dir)
    {
        export_import_strip = clipboard_dir;
        export_import_strip += "/clipboard.strip";
    }

    m->set( le );

    m->number(-1);

    if ( ! chain()->insert( this, m ) )
    {
        fl_alert( "Copied module cannot be inserted at this point in the chain" );
    }

    export_import_strip = "";

    free( _copied_module_settings );
    _copied_module_settings = NULL;
    _copied_module_empty = NULL;

    /* set up for another paste */
    m->copy();
}

void
Module::number ( int v )
{
    _number = v;

    char s[255];

    if ( v > 0 && !is_default() )
	snprintf( s, sizeof(s), "%s.%i", base_label(), v );
    else
	snprintf( s, sizeof(s), "%s", base_label() );
    
    copy_label( s );
}

void
Module::base_label ( const char *s )
{
    if ( _base_label )
	free( _base_label );

    _base_label = NULL;

    if ( s )
	_base_label = strdup(s);
}


void
Module::Port::disconnect_from_strip ( const Mixer_Strip *o )
{
    for ( std::list<Module::Port*>::iterator i = _connected.begin(); i != _connected.end();  )
    {
        Module::Port *p = *i;

	++i;			/* iterator trick */
	
        if ( p->module()->chain()->strip() == o )
        {
            disconnect(p);
        }
    }
}

const char *
Module::Port::osc_number_path ( void )
{
    if ( ! _scaled_signal )
        return NULL;

    int n = _module->chain()->strip()->number();
    
    if ( _by_number_path && n == _by_number_number )
        return _by_number_path;

    if ( _by_number_path )
        free( _by_number_path );

    char *rem;
    char *client_name;
    char *strip_name;

    if ( 3 != sscanf( _scaled_signal->path(), "%m[^/]/strip/%m[^/]/%m[^\n]", &client_name, &strip_name, &rem ) )
        return NULL;

    free( strip_name );

    char *path;
    asprintf( &path, "%s/strip#/%i/%s", client_name, n, rem );

    free( client_name );
    free( rem );
    
    _by_number_path = path;
    _by_number_number = n;

    return path;
}

void
Module::Port::send_feedback ( bool force )
{
    if ( !force && !_pending_feedback )
	return;
    
    float f = control_value();

    if ( hints.ranged )
    {
        // scale value to range.
        
        float scale = hints.maximum - hints.minimum;
        float offset = hints.minimum;
        
        f =  ( f - offset ) / scale;
    }
    
    if ( f > 1.0f )
        f = 1.0f;
    else if ( f < 0.0f )
        f = 0.0f;

    /* struct timespec t; */
    /* clock_gettime( CLOCK_MONOTONIC, &t ); */

    /* /\* don't send feedback more at more than 30Hz rate. *\/ */
    /* unsigned long long ms = (t.tv_sec * 1000 + ( t.tv_nsec / 1000000 )); */
    /* if ( ms - _feedback_milliseconds < 1000 / 30 ) */
    /* 	return; */
    
    /* _feedback_milliseconds = ms; */
		 
    if ( _scaled_signal )
    {
	/* if ( fabsf( _feedback_value - f ) > (1.0f / 128.0f) ) */
	{
            /* only send feedback if value has changed significantly since the last time we sent it. */
	    /* DMESSAGE( "signal value: %f, controL_value: %f", _scaled_signal->value(), f ); */
            /* send feedback for by_name signal */
	    mixer->osc_endpoint->send_feedback( _scaled_signal->path(), f, force );
        
            /* send feedback for by number signal */
	    mixer->osc_endpoint->send_feedback( osc_number_path(), f, force );
	
	    /* _feedback_value = f; */

	    _pending_feedback = false;
	    /* _scaled_signal->value( f ); */
        }
    }
}

void
Module::schedule_feedback ( void )
{
    for ( int i = 0; i < ncontrol_inputs(); i++ )
        control_input[i].schedule_feedback();
}

void
Module::send_feedback ( bool force )
{
    for ( int i = 0; i < ncontrol_inputs(); i++ )
        control_input[i].send_feedback(force);
}

void
Module::handle_control_changed ( Module::Port *p )
{
    if ( _editor )
        _editor->handle_control_changed ( p );

    // redraw if bypass state changed
    if (bypassable() && p == &control_input[control_input.size() - 1])
    {
        if ( !strcmp( "dsp/bypass", p->name()) )
        {
            redraw();
            p->schedule_feedback();
            return;
        }
    }

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)
    Module *m = p->module();
#endif

#ifdef LV2_SUPPORT
    if (m->_plug_type == LV2)
    {
        if(m->_is_from_custom_ui)
        {
          //  DMESSAGE("Received control from custom UI");
            m->_is_from_custom_ui = false;
        }
        else
        {
            LV2_Plugin *pm = static_cast<LV2_Plugin *> (m);

            int i = m->control_input_port_index( p );
            float value = p->control_value();
            DMESSAGE("Port_index = %d: Value = %f", i, value);
            pm->send_to_custom_ui(i, sizeof(float), 0, &value); // 0 = float type
        }
    }
#endif
#ifdef CLAP_SUPPORT
    if (m->_plug_type == CLAP)
    {
        if(m->_is_from_custom_ui)
        {
          //  DMESSAGE("Received control from custom UI");
            m->_is_from_custom_ui = false;
        }
        else
        {
            CLAP_Plugin *pm = static_cast<CLAP_Plugin *> (m);

            uint32_t param_id = p->hints.parameter_id;
            float value = p->control_value();
            DMESSAGE("CLAP Param ID = %d: Value = %f", param_id, value);
            pm->setParameter(param_id, value);
        }
    }
#endif
#ifdef VST3_SUPPORT
    if (m->_plug_type == VST3)
    {
        if(m->_is_from_custom_ui)
        {
          //  DMESSAGE("Received control from custom UI");
            m->_is_from_custom_ui = false;
        }
        else
        {
            VST3_Plugin *pm = static_cast<VST3_Plugin *> (m);

            uint32_t param_id = p->hints.parameter_id;
            float value = 0.0f;

            // VST3 only receives integer in float normalized ranges 0.0 to 1.0.
            // So do the conversion to float normalized here.
            if(p->hints.type == Port::Hints::INTEGER)
            {
                float tmp = p->control_value();
                value = tmp / float(p->hints.maximum);
            }
            else
            {
                value = p->control_value();
            }

            pm->updateParam(param_id, value);
        }
    }
    
#endif
    p->schedule_feedback();
    
    /* DMESSAGE("Control changed"); */
    /* p->send_feedback(false); */
}

/* bool */
/* Module::Port::connected_osc ( void ) const */
/* { */
/*     if ( _scaled_signal ) */
/*         return _scaled_signal->connected(); */
/*     else */
/*         return false; */
/* } */

char *
Module::Port::generate_osc_path ()
{
    const Module::Port *p = this;

    char *path = NULL;

    // /strip/STRIPNAME/MODULENAME/CONTROLNAME

    if ( ! p->hints.visible && ! p->hints.invisible_with_signals)
    {
        return NULL;
    }

    asprintf( &path, "/strip/%s/%s/%s", module()->chain()->name(), p->module()->label(), p->symbol() );

    char *s = escape_url( path );
    
    free( path );

    path = s;

    return path;
}

void
Module::Port::handle_signal_connection_state_changed ( OSC::Signal *, void *o )
{
    ((Module::Port*)o)->module()->redraw();
}

void
Module::Port::change_osc_path ( char *path )
{
    if ( path )
    {
        char *scaled_path = path;
        char *unscaled_path = NULL;

        asprintf( &unscaled_path, "%s/unscaled", path );

        if ( NULL == _scaled_signal )
        {
            float scaled_default = 0.5f;

            if ( hints.ranged )
            {
                float scale = hints.maximum - hints.minimum;
                float offset = hints.minimum;

                scaled_default = ( hints.default_value - offset ) / scale;
            }

            _scaled_signal = mixer->osc_endpoint->add_signal( scaled_path,
                                                              _direction == INPUT ? OSC::Signal::Input : OSC::Signal::Output,
                                                              0.0, 1.0, scaled_default,
                                                              &Module::Port::osc_control_change_cv,
                                                              &Module::Port::osc_control_update_signals,
                                                              this );
            _scaled_signal->set_infos(name(), hints.type);

            _scaled_signal->connection_state_callback( handle_signal_connection_state_changed, this );

            _unscaled_signal = mixer->osc_endpoint->add_signal( unscaled_path,
                                                                _direction == INPUT ? OSC::Signal::Input : OSC::Signal::Output,
                                                                hints.minimum, hints.maximum, hints.default_value,
                                                                &Module::Port::osc_control_change_exact,
                                                                &Module::Port::osc_control_update_signals,
                                                                this );
            _unscaled_signal->set_infos(name(), hints.type);
        }
        else
        {
            DMESSAGE( "Renaming OSC signals" );

            _scaled_signal->rename( scaled_path );
            _unscaled_signal->rename( unscaled_path );
        }

        free( unscaled_path );
        /* this was path, it's ok to free because it was malloc()'d in generate_osc_path */
        free( scaled_path );
    }
}


int 
Module::Port::osc_control_change_exact ( float v, void *user_data )
{
    Module::Port *p = (Module::Port*)user_data;

    Fl::lock();

    float f = v;

    if ( p->hints.ranged )
    {
        if ( f > p->hints.maximum )
            f = p->hints.maximum;
        else if ( f < p->hints.minimum )
            f = p->hints.minimum;

        if ( Hints::BOOLEAN == p->hints.type )
            f = f > (p->hints.maximum - (p->hints.maximum - p->hints.minimum)) * 0.5f ?
                p->hints.maximum : 
                p->hints.minimum;
    }


    p->control_value( f );

    Fl::unlock();

//    mixer->osc_endpoint->send( lo_message_get_source( msg ), "/reply", path, f );

    return 0;
}

int 
Module::Port::osc_control_change_cv ( float v, void *user_data )
{
    Module::Port *p = (Module::Port*)user_data;

    float f = v;

    Fl::lock();

    // clamp value to control voltage range.
    if ( f > 1.0 )
        f = 1.0;
    else if ( f < 0.0 )
        f = 0.0;

    if ( p->hints.ranged )
    {
        if ( Hints::BOOLEAN == p->hints.type )
            f = f > 0.5f ? p->hints.maximum : p->hints.minimum;

        // scale value to range.

        float scale = p->hints.maximum - p->hints.minimum;
        float offset = p->hints.minimum;
        
        f = ( f * scale ) + offset;
    }

    p->control_value( f );

    Fl::unlock();
//    mixer->osc_endpoint->send( lo_message_get_source( msg ), "/reply", path, f );

    return 0;
}

/**
 * Updates the signal's value according to the port's value.
 * Called before sending reply to a query (value-less message).
 * 
 * @param user_data
 *      The control port that needs signal value updated.
 * @return 
 *      Zero on success.
 */
int
Module::Port::osc_control_update_signals ( void *user_data )
{
    Module::Port *p = (Module::Port*)user_data;

    Fl::lock();

    float f = p->control_value();

    if ( p->unscaled_signal() )
    {

        p->unscaled_signal()->value_no_callback(f);
    }

    if ( p->scaled_signal() && p->hints.ranged)
    {
        // scale value to range.

        float scale = p->hints.maximum - p->hints.minimum;
        float offset = p->hints.minimum;
        f =  ( f - offset ) / scale;

        if ( f > 1.0f )
            f = 1.0f;
        else if ( f < 0.0f )
            f = 0.0f;

        p->scaled_signal()->value_no_callback(f);
    }

    Fl::unlock();

    return 0;
}


void
Module::set ( Log_Entry &e )
{
    /* have to do this before adding to chain... */

    int n = -2;
    
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

    	if ( ! strcmp(s, ":number" ) )
	{
	    n = atoi(v);
	}
    }
    
    number(n);
    
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! ( strcmp( s, ":is_default" ) ) )
        {
            is_default( atoi( v ) );
        }
        else if ( ! strcmp( s, ":chain" ) )
        {
            /* This trickiness is because we may need to know the name of
               our chain before we actually get added to it. */
            unsigned int ii;
            sscanf( v, "%X", &ii );
            Chain *t = static_cast<Chain*>(Loggable::find( ii ));

            assert( t );

            chain( t );
        }
    }

    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

/*         if ( ! strcmp( s, ":name" ) ) */
/*             label( v ); */
        if ( ! strcmp( s, ":parameter_values" ) )
        {
            set_parameters( v );
        }
        else if ( ! ( strcmp( s, ":active" ) ) )
        {
            bypass( ! atoi( v ) );
        }
        else if ( ! strcmp( s, ":chain" ) )
        {
            unsigned int ii;
            sscanf( v, "%X", &ii );
            Chain *t = static_cast<Chain*>(Loggable::find( ii ));

            assert( t );

            t->add( this );
        }
    }
}


void
Module::chain ( Chain *v )
{
    if ( _chain != v )
    {
        DMESSAGE( "Adding module %s in to chain %s", label(), v ? v->name() : "NULL" );

        _chain = v; 

        for ( int i = 0; i < ncontrol_inputs(); ++i )
        {
            control_input[i].update_osc_port();
        }

        // Publish output control signals and update them when the chain changes.
        for ( int i = 0; i < ncontrol_outputs(); ++i )
        {
            if ( control_output[i].name() != NULL )
                control_output[i].update_osc_port();
        }
    }
    else
    {
        DMESSAGE( "Module %s already belongs to chain %s", label(), v ? v->name() : "NULL" );
    }
}

/* return a string serializing this module's parameter settings.  The
   format is 1.0:2.0:... Where 1.0 is the value of the first control
   input, 2.0 is the value of the second control input etc.
*/
char *
Module::get_parameters ( void ) const
{
    int len = control_input.size() * 50;

    /* To have something to return because valgrind indicates invalid read/write
     * if control_input.size() == 0 and new char[ len ] is created with 0 size */
    if( !control_input.size() )
        len = 1;

    char *s = new char[ len ];
    
    s[0] = 0;
    char *sp = s;

    if ( control_input.size() )
    {
        for ( unsigned int i = 0; i < control_input.size(); ++i )
            sp += snprintf( sp, len - (sp - s),"%f:", control_input[i].control_value() );

        *(sp - 1) = '\0';
    }

    /* DMESSAGE("get_parameters: %s",s); */
    return s;
}

void
Module::set_parameters ( const char *parameters )
{
    char *s = strdup( parameters );

    char *start = s;
    unsigned int i = 0;
    for ( char *sp = s; ; ++sp )
    {
        if ( ':' == *sp || '\0' == *sp )
        {
            char was = *sp;

            *sp = '\0';

            DMESSAGE( start );

            if ( i < control_input.size() )
                control_input[i].control_value( atof( start ) );
            else
            {
                WARNING( "Module has no parameter at index %i", i );
                break;
            }

            i++;

            if ( '\0' == was  )
                break;

            start = sp + 1;
        }
    }

    free( s );
}

void
Module::draw_box ( int tx, int ty, int tw, int th )
{
    fl_color( fl_contrast( FL_FOREGROUND_COLOR, color() ) );

    fl_push_clip( tx, ty, tw, th );

    Fl_Color c = color();

    if ( bypass() )
	c = fl_darker(fl_darker(c));
    
    if ( ! active_r() )
        c = fl_inactive( c );

    int spacing = w() / instances();
    for ( int i = instances(); i--; )
    {
        fl_draw_box( box(), tx + (spacing * i), ty, tw / instances(), th, c );
    }


    if ( audio_input.size() && audio_output.size() )
    {
        /* maybe draw control indicators */
        if ( control_input.size() )
        {
            fl_draw_box( FL_ROUNDED_BOX, tx + 4, ty + 4, 5, 5, is_being_controlled() ? FL_YELLOW : fl_inactive( FL_YELLOW ) );

            /* fl_draw_box( FL_ROUNDED_BOX, tx + 4, ty + th - 8, 5, 5, is_being_controlled_osc() ? FL_YELLOW : fl_inactive( FL_YELLOW ) ); */
        }

        if ( control_output.size() )
            fl_draw_box( FL_ROUNDED_BOX, tx + tw - 8, ty + 4, 5, 5, is_controlling() ? FL_YELLOW : fl_inactive( FL_YELLOW ) );
    }

    fl_push_clip( tx + Fl::box_dx(box()), ty + Fl::box_dy(box()), tw - Fl::box_dw(box()), th - Fl::box_dh(box()) );

    Fl_Group::draw_children();

    fl_pop_clip();

    if ( focused_r( this ) )
        draw_focus_frame( tx,ty,tw,th, selection_color() );

    fl_pop_clip();
}

#include "SpectrumView.H"
#include <FL/Fl_Double_Window.H>


bool
Module::show_analysis_window ( void )
{
    /* use a large window for more accuracy at low frequencies */
    nframes_t enframes = sample_rate() / 2;
    float *buf = new float[enframes];

    memset( buf, 0, sizeof(float) * enframes );
    
    buf[0] = 1;
       
    if ( ! get_impulse_response( buf, enframes ) )
    {
        // return false;
    }
    
    Fl_Double_Window *w = new Fl_Double_Window( 1000, 500 );

    {
        SpectrumView * o = new SpectrumView( 25,25, 1000 - 50, 500 - 50, label() );
        o->labelsize(10);
        o->align(FL_ALIGN_RIGHT|FL_ALIGN_TOP);
        o->sample_rate( sample_rate() );
        o->data( buf, enframes );
    }

    w->end();

    w->show();

    while ( w->shown() )
        Fl::wait();

    delete w;

    return true;
}

void
Module::draw_label ( int tx, int ty, int tw, int th )
{
    bbox( tx, ty, tw, th );

    if ( ! label() )
        return;

    char *lab = strdup( label() );

    Fl_Color c = fl_contrast( FL_FOREGROUND_COLOR, color() );

    if ( bypass() )
	c = fl_darker(c);

    /* fl_color( active_r() && ! bypass() ? c : fl_inactive(c) ); */

    if ( !active_r() )
	c = fl_inactive(c);
    
    fl_font( FL_HELVETICA, labelsize() );

    char *di = strstr( lab, " -" );
    
    if ( ! di )
        di = strstr( lab, "  " );

    if ( di )
        *di = '\0';

    int LW = fl_width( lab );
    char *s = NULL;

    if ( LW > tw )
    {
        bool initial = true;
        s = new char[strlen(lab) + 1];
        char *sp = s;
        const char *lp = lab;

        for ( ; *lp; ++lp )
        {
            bool skip = false;

            switch ( *lp )
            {
                case ' ':
                    initial = true;
                    skip = false;
                    break;
                case 'i': case 'e': case 'o': case 'u': case 'a':
                    skip = ! initial;
                    initial = false;
                    break;
                default:
                    skip = false;
                    initial = false;
                    break;
            }
            
            if ( ! skip )
                *(sp++) = *lp;
        }
     
        *sp = '\0';
   
    }


    fl_color( c );

    fl_draw( s ? s : lab, tx, ty, tw, th, align() | FL_ALIGN_CLIP );
    
    /* if ( bypass() ) */
    /* { */
    /*     fl_color( fl_color_add_alpha( fl_color(), 127 )  ); */
    /*     fl_line_style( FL_SOLID, 2 ); */
    /*     fl_line( tx, ty + th * 0.5, tx + tw, ty + th * 0.5 ); */
    /*     fl_line_style( FL_SOLID, 0 ); */
    /* } */


    free(lab);

    if ( s )
        delete[] s;
}

void
Module::insert_menu_cb ( const Fl_Menu_ *menu )
{
    
    const char * s_picked =  menu->mvalue()->label();

    DMESSAGE("picked = %s", s_picked );

    Module *mod = NULL;
    
    if ( !strcmp( s_picked, "Aux" ) )
    {
        AUX_Module *jm = new AUX_Module();
     
        mod = jm;
    }
    if ( !strcmp( s_picked, "Spatializer" ) )
    {
        int n = 0;
        for ( int i = 0; i < chain()->modules(); i++ )
        {
            if ( !strcmp( chain()->module(i)->name(), "Spatializer" ) )
                n++;
        }

        if ( n == 0 )
        {
            Spatializer_Module *jm = new Spatializer_Module();
            
            jm->chain( chain() );
            jm->initialize();
            
            mod = jm;
        }
    }
    else if ( !strcmp( s_picked, "Gain" ) )
            mod = new Gain_Module();
    else if ( !strcmp( s_picked, "Meter" ) )
        mod = new Meter_Module();
    else if ( !strcmp( s_picked, "Mono Pan" ))
        mod = new Mono_Pan_Module();
    else if ( !strcmp( s_picked, "Plugin" ))
    {
        Picked picked = Plugin_Chooser::plugin_chooser( this->ninputs() );

        switch ( picked.plugin_type )
        {
#ifdef LADSPA_SUPPORT
            case LADSPA:
            {
                LADSPA_Plugin *m = new LADSPA_Plugin();
                if(!m->load_plugin( picked ))
                {
                    fl_alert( "%s could not be loaded", m->base_label() );
                    delete m;
                    return;
                }

                mod = m;
                break;
            }
#endif  // LADSPA_SUPPORT
#ifdef LV2_SUPPORT
            case LV2:
            {
                LV2_Plugin *m = new LV2_Plugin();
                if(!m->load_plugin( picked ))
                {
                    fl_alert( "%s could not be loaded", m->base_label() );
                    delete m;
                    return;
                }

                mod = m;
                break;
            }
#endif  // LV2_SUPPORT
#ifdef CLAP_SUPPORT
            case CLAP:
            {
                CLAP_Plugin *m = new CLAP_Plugin();
                if(!m->load_plugin( picked ))
                {
                    fl_alert( "%s could not be loaded", m->base_label() );
                    delete m;
                    return;
                }

                mod = m;
                break;
            }
#endif  // CLAP_SUPPORT
#ifdef VST3_SUPPORT
            case VST3:
            {
                VST3_Plugin *m = new VST3_Plugin();
                if(!m->load_plugin( picked ))
                {
                    fl_alert( "%s could not be loaded", m->base_label() );
                    delete m;
                    return;
                }

                mod = m;
                break;
            }
#endif  // VST3_SUPPORT
            default:
            {
                return;
            }
        }
    }

    if ( mod )
    {
	mod->number(-1);
	
        if ( ! chain()->insert( this, mod ) )
        {
            fl_alert( "Cannot insert this module at this point in the chain" );
            delete mod;
            return;
        }

        redraw();
    }
}

void
Module::insert_menu_cb ( Fl_Widget *w, void *v )
{
    ((Module*)v)->insert_menu_cb( (Fl_Menu_*) w );
}

void
Module::menu_cb ( const Fl_Menu_ *m )
{
    char picked[256];

    if ( ! m->mvalue() || m->mvalue()->flags & FL_SUBMENU_POINTER || m->mvalue()->flags & FL_SUBMENU )
        return;

    strncpy( picked, m->mvalue()->label(), sizeof( picked ) -1 );

//    m->item_pathname( picked, sizeof( picked ) );

    DMESSAGE( "%s", picked );

    /* FIXME check this - I believe this was supposed to log the current state for undo, redo but
       the mixer does not have any undo, redo?? Commented out since it does not work with LV2
       save state and triggers a save any time you open the parameter editor window. */
    // Logger log( this );

    if ( ! strcmp( picked, "Edit Parameters" ) )
    {
        command_open_parameter_editor();
    }
    else if ( ! strcmp( picked, "Bypass" ) )
    {
        if ( ! bypassable() )
        {
            fl_alert( "Due to its channel configuration, this module cannot be bypassed." );
        }
        else
        {
            bypass( !bypass() );
            redraw();
        }
    }
    else if ( ! strcmp( picked, "Cut" ) )
    {
        if ( copy() )
        {
            chain()->remove( this );
            Fl::delete_widget( this );
        }
    }
    else if ( ! strcmp( picked, "Copy" ) )
    {
        copy();
    }
    else if ( ! strcmp( picked, "Paste" ) )
    {
        paste_before();
    }
    else if ( ! strcmp( picked, "Show Analysis" ) )
    {
        show_analysis_window();
    }
    else if ( ! strcmp( picked, "Remove" ) )
        command_remove();
}

void
Module::menu_cb ( Fl_Widget *w, void *v )
{
    ((Module*)v)->menu_cb( (Fl_Menu_*) w );
}

/** build the context menu */
Fl_Menu_Button &
Module::menu ( void ) const
{
    static Fl_Menu_Button m( 0, 0, 0, 0, "Module" );
    static Fl_Menu_Button *insert_menu = NULL;

    if ( ! insert_menu )
    {
        insert_menu = new Fl_Menu_Button( 0, 0, 0, 0 );

        insert_menu->add( "Gain", 0, 0 );
        insert_menu->add( "Meter", 0, 0 );
        insert_menu->add( "Mono Pan", 0, 0 );
        insert_menu->add( "Aux", 0, 0 );
        insert_menu->add( "Spatializer", 0, 0 );
        insert_menu->add( "Plugin", 0, 0 );

        insert_menu->callback( &Module::insert_menu_cb, (void*)this );
    }

    m.clear();

    m.add( "Insert", 0, &Module::menu_cb, (void*)this, 0);
    m.add( "Insert", 0, &Module::menu_cb, const_cast< Fl_Menu_Item *>( insert_menu->menu() ), FL_SUBMENU_POINTER );
    m.add( "Edit Parameters", FL_CTRL + ' ', &Module::menu_cb, (void*)this, 0 );
    m.add( "Show Analysis", 's', &Module::menu_cb, (void*)this, 0);
    m.add( "Bypass",   'b', &Module::menu_cb, (void*)this, FL_MENU_TOGGLE | ( bypass() ? FL_MENU_VALUE : 0 ) );
    m.add( "Cut", FL_CTRL + 'x', &Module::menu_cb, (void*)this, is_default() ? FL_MENU_INACTIVE : 0 );
    m.add( "Copy", FL_CTRL + 'c', &Module::menu_cb, (void*)this, is_default() ? FL_MENU_INACTIVE : 0 );
    m.add( "Paste", FL_CTRL + 'v', &Module::menu_cb, (void*)this, _copied_module_empty ? 0 : FL_MENU_INACTIVE );

    m.add( "Remove",  FL_Delete, &Module::menu_cb, (void*)this );

//    menu_set_callback( menu, &Module::menu_cb, (void*)this );
    m.callback( &Module::insert_menu_cb, (void*)this );

    return m;
}

void
Module::handle_chain_name_changed ( )
{
    // Flag to tell Module_Parameter_Editor that the OSC path tooltip needs update
    _has_name_change = true;

    // pass it along to our connected Controller_Modules, if any.
    for ( int i = 0; i < ncontrol_inputs(); ++i )
    {
        if ( control_input[i].connected() )
            control_input[i].connected_port()->module()->handle_chain_name_changed();
    
        control_input[i].update_osc_port();
    }

    // Publish output control signals and update them when the chain changes.
    for ( int i = 0; i < ncontrol_outputs(); ++i )
    {
        if ( control_output[i].name() != NULL )
        {
            if ( control_output[i].connected() )
                control_output[i].connected_port()->module()->handle_chain_name_changed();

            control_output[i].update_osc_port();
        }
    }

    if ( ! chain()->strip()->group()->single() )
    {
        /* we have to rename our JACK ports... */
        for ( unsigned int i = 0; i < aux_audio_input.size(); i++ )
        {
            aux_audio_input[i].jack_port()->trackname( chain()->name() );
            aux_audio_input[i].jack_port()->rename();
        }
        for ( unsigned int i = 0; i < aux_audio_output.size(); i++ )
        {
            aux_audio_output[i].jack_port()->trackname( chain()->name() );
            aux_audio_output[i].jack_port()->rename();
        }
    }
}

int
Module::handle ( int m )
{
    static unsigned long _event_state = 0;

    unsigned long evstate = Fl::event_state();

    switch ( m )
    {
        case FL_ENTER:
//            Fl::focus(this);
        case FL_LEAVE:
            return 1;
    }

    if ( Fl_Group::handle( m ) )
        return 1;

    switch ( m )
    {
        case FL_KEYBOARD:
        {
            /* For LV2 we show the custom UI if available with the space bar. The generic
               UI is shown with CTRL space. If no custom UI then either space or CTRL space opens
               the generic UI. */
            if ( !( Fl::event_key(FL_Control_L) ) &&  !( Fl::event_key(FL_Control_R) ) && (Fl::event_key(32)) ) // 32 == space bar
            {
#ifdef LV2_SUPPORT
                if(_plug_type == LV2)
                {
                    LV2_Plugin *pm = static_cast<LV2_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                    else    // set the dirty flag if we opened the custom UI
                    {
                        set_dirty();
                    }
                }
                else
#endif
#ifdef CLAP_SUPPORT
                if(_plug_type == CLAP)
                {
                    CLAP_Plugin *pm = static_cast<CLAP_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                }
                else
#endif
#ifdef VST3_SUPPORT
                if(_plug_type == VST3)
                {
                    VST3_Plugin *pm = static_cast<VST3_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                }
                else
#endif
                // LADSPA and internal
                {
                    command_open_parameter_editor();
                }

                return 1;
            }

            if ( Fl::event_key() == FL_Menu )
            {
                menu_popup( &menu(), x(), y() );
                return 1;
            }
            else
                return menu().test_shortcut() != 0;
        }
        case FL_PUSH:     
            take_focus();
            _event_state = evstate;
            return 1;
            // if ( Fl::visible_focus() && handle( FL_FOCUS )) Fl::focus(this);
        case FL_DRAG:
            _event_state = evstate;
            return 1;
        case FL_RELEASE:
        {
            unsigned long e = _event_state;
            _event_state = 0;

            if ( ! Fl::event_inside( this ) )
                return 1;

            if ( ( e & FL_BUTTON1 ) && ( e & FL_CTRL ) )
            {
                Fl::focus(this);
                return 1;
            }
            else if ( e & FL_BUTTON1 )
            {
#ifdef LV2_SUPPORT
                if(_plug_type == LV2)
                {
                    LV2_Plugin *pm = static_cast<LV2_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                    else    // set the dirty flag if we opened the custom UI
                    {
                        set_dirty();
                    }
                }
                else
#endif  
#ifdef CLAP_SUPPORT
                if(_plug_type == CLAP)
                {
                    CLAP_Plugin *pm = static_cast<CLAP_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                }
                else
#endif
#ifdef VST3_SUPPORT
                if(_plug_type == VST3)
                {
                    VST3_Plugin *pm = static_cast<VST3_Plugin *> (this);
                    if(!pm->try_custom_ui())
                    {
                        command_open_parameter_editor();
                    }
                }
                else
#endif
                {
                    command_open_parameter_editor();
                }

                return 1;
            }
            else if ( e & FL_BUTTON3 && e & FL_CTRL )
            {
                command_remove();
                return 1;
            }
            else if ( e & FL_BUTTON3 )
            {
                menu_popup( &menu() );
                return 1;
            }
            else if ( e & FL_BUTTON2 )
            {
                if ( !bypassable() )
                {
                    fl_alert( "Due to its channel configuration, this module cannot be bypassed." );
                }
                else
                {
                    bypass( !bypass() );
                    redraw();
                }
                return 1;
            }
            /* else */
            /* { */
            /*     take_focus(); */
            /* } */

            return 0;
        }
        case FL_FOCUS:
        case FL_UNFOCUS:
            redraw();
            return 1;
    }

    return 0;
}

/*************/
/* AUX Ports */
/*************/


static char *
generate_port_name ( const char *aux, int direction, int n )
{
    char *s;
    asprintf( &s, "%s%s%s-%i",
              aux ? aux : "",
              aux ? "/" : "",
              direction == JACK::Port::Input ? "in" : "out",
              n + 1 );

    return s;
}

static void
jack_port_activation_error ( const JACK::Port *p )
{
    fl_alert( "Could not activate JACK port \"%s\"", p->name() );
}

/* freeze/disconnect all jack ports--used when changing groups */
void
Module::freeze_ports ( void )
{
    // pass it along to our connected Controller_Modules, if any.
    for ( int i = 0; i < ncontrol_inputs(); ++i )
    {
        if ( control_input[i].connected() )
	{
	    if ( ! control_input[i].connected_port()->module()  )
	    {
		DWARNING( "Programming error. Connected port has null module. %s %s",
			 
			 name(),
			 control_input[i].connected_port()->name());
	    }
	    else
	    {
		control_input[i].connected_port()->module()->freeze_ports();
	    }
	}
    }

    for ( unsigned int i = 0; i < aux_audio_input.size(); ++i )
    {   
        aux_audio_input[i].jack_port()->freeze();
        aux_audio_input[i].jack_port()->shutdown();
    }

    for ( unsigned int i = 0; i < aux_audio_output.size(); ++i )
    {
        aux_audio_output[i].jack_port()->freeze();
        aux_audio_output[i].jack_port()->shutdown();
    }
}

/* rename and thaw all jack ports--used when changing groups */
void
Module::thaw_ports ( void )
{
    // pass it along to our connected Controller_Modules, if any.
    for ( int i = 0; i < ncontrol_inputs(); ++i )
    {
        if ( control_input[i].connected() )
            control_input[i].connected_port()->module()->thaw_ports();
    }

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for ( unsigned int i = 0; i < aux_audio_input.size(); ++i )
    {   
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        
        aux_audio_input[i].jack_port()->client( chain()->client() );
        aux_audio_input[i].jack_port()->trackname( trackname );
        aux_audio_input[i].jack_port()->thaw();
    }

    for ( unsigned int i = 0; i < aux_audio_output.size(); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        aux_audio_output[i].jack_port()->client( chain()->client() );
        aux_audio_output[i].jack_port()->trackname( trackname );
        aux_audio_output[i].jack_port()->thaw();

        mixer->maybe_auto_connect_output( &aux_audio_output[i] );
    }
}

void
Module::destroy_connected_controller_module()
{
    for ( unsigned int i = 0; i < control_input.size(); ++i )
    {
        /* destroy connected Controller_Module */
        if ( control_input[i].connected() )
        {
            Module *o = static_cast<Module*>( control_input[i].connected_port()->module() );

	    if ( !o )
	    {
		DWARNING( "Programming error. Connected port has null module. %s %s",
			 
			  label(),
			  control_input[i].connected_port()->name());
	    }
	    
	    control_input[i].disconnect();
	    
	    if ( o )
	    {
		if ( ! o->is_default() )
		{
		    DMESSAGE( "Deleting connected module %s", o->label() );
		    
		    delete o;
		}
	    }
	}
	
        control_input[i].destroy_osc_port();
    }

    for ( unsigned int i = 0; i < control_output.size(); ++i )
        control_output[i].disconnect();
}

void
Module::deleteEditor()
{
    if ( _editor )
    {
        if (_editor->visible())
            _editor->hide();

        delete _editor;
        _editor = NULL;
    }
}

void
Module::auto_connect_outputs ( void )
{
    for ( unsigned int i = 0; i < aux_audio_output.size(); ++i )
    {
        mixer->maybe_auto_connect_output( &aux_audio_output[i] );
    }
}

void
Module::auto_disconnect_outputs ( void )
{
    for ( unsigned int i = 0; i < aux_audio_output.size(); ++i )
    {
        Module::Port *p = &aux_audio_output[i];

        while ( p->connected() )
        {
            p->connected_port()->jack_port()->disconnect( p->jack_port()->jack_name() );
            p->disconnect(p->connected_port());
        }
    }
}

void
Module::get_latency ( JACK::Port::direction_e dir, nframes_t *t_min, nframes_t *t_max ) const
{
    nframes_t tmin = JACK_MAX_FRAMES >> 1;
    nframes_t tmax = 0;

    const std::vector<Module::Port> *ports;

    if ( dir == JACK::Port::Input )
        ports = &aux_audio_input;
    else
        ports = &aux_audio_output;

    if ( ports->size() )
    {
        for ( unsigned int i = 0; i < ports->size(); i++ )
        {
            /* if ( ! ports->[i].jack_port()->connected() ) */
            /*     continue; */

            nframes_t min,max;
            
            (*ports)[i].jack_port()->get_latency( dir, &min, &max );
            
            if ( min < tmin )
                tmin = min;
            if ( max > tmax )
                tmax = max;
        }
    }
    else
    {
        tmin = 0;
    }
    
    *t_min = tmin;
    *t_max = tmax;
}

void
Module::set_latency ( JACK::Port::direction_e dir, nframes_t min, nframes_t max )
{
    if ( dir == JACK::Port::Output )
    {
        for ( unsigned int i = 0; i < aux_audio_input.size(); i++ )
            aux_audio_input[i].jack_port()->set_latency( dir, min, max );
    }
    else
    {
        for ( unsigned int i = 0; i < aux_audio_output.size(); i++ )
            aux_audio_output[i].jack_port()->set_latency( dir, min, max );
    }
}


bool
Module::add_aux_port ( bool input, const char *prefix, int i, JACK::Port::type_e type )
{
    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    JACK::Port::direction_e direction = input ? JACK::Port::Input : JACK::Port::Output;

    char *portname = generate_port_name( prefix, direction, i );

    JACK::Port *po = new JACK::Port( chain()->client(), trackname, portname, direction, type );

    free(portname);

    if ( ! po->activate() )
    {
        jack_port_activation_error( po );
        return false;
    }

    if ( po->valid() )
    {
        if ( input )
        {
            Module::Port mp( static_cast<Module*>(this), Module::Port::INPUT, Module::Port::AUX_AUDIO );

            mp.jack_port( po );

            aux_audio_input.push_back( mp );
        }
        else
        {
            Module::Port mp( static_cast<Module*>(this), Module::Port::OUTPUT, Module::Port::AUX_AUDIO );

            mp.jack_port( po );

            aux_audio_output.push_back( mp );
        }
    }
    else
    {
        delete po;
        return false;
    }

    return true;
}

bool
Module::add_aux_audio_output( const char *prefix, int i )
{
    bool r = add_aux_port ( false, prefix, i , JACK::Port::Audio);

    if ( r )
        mixer->maybe_auto_connect_output( &aux_audio_output.back() );

    return r;
}

bool
Module::add_aux_audio_input( const char *prefix, int i )
{
    return add_aux_port ( true, prefix, i , JACK::Port::Audio);
}

bool
Module::add_aux_cv_input( const char *prefix, int i )
{
    return add_aux_port ( true, prefix, i , JACK::Port::CV);
}


/************/
/* Commands */
/************/

void
Module::command_open_parameter_editor ( void )
{
    if ( _editor )
    {
        if(_editor->visible())
        {
            _editor->hide();
        }
        else
        {
            _editor->show();
            set_dirty();
        }
    }
    else if ( ncontrol_inputs() && nvisible_control_inputs() )
    {
        DMESSAGE( "Opening module parameters for \"%s\"", label() );
        _editor = new Module_Parameter_Editor( this );

        _editor->show();
        set_dirty();
    }
}

void
Module::command_activate ( void )
{
    bypass( false );
}

void
Module::command_deactivate ( void )
{
    bypass( true );
}

void
Module::command_remove ( void )
{
    if ( is_default() )
        fl_alert( "Default modules may not be deleted." );
    else
    {
        chain()->remove( this );
        Fl::delete_widget( this );
    }
}
