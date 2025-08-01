
/*******************************************************************************/
/* Copyright (C) 2008-2021 Jonathan Moore Liles (as "Non-Mixer")               */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
/*                                                                             */
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

#include <string.h>

#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Pack.H>

#include "../../FL/Fl_Scalepack.H"
#include "../../nonlib/dsp.h"
#include "../../FL/img_io_input_connector_10x10_png.h"
#include "../../FL/img_io_output_connector_10x10_png.h"

#include <FL/Enumerations.H>

#include "Chain.H"

#include "JACK_Module.H"
#include <FL/fl_draw.H>

#include <FL/Fl.H>
#include <FL/Fl_Browser.H>

#include <FL/Fl_PNG_Image.H>

#ifdef FLTK_SUPPORT
#include <FL/platform.H>
#endif

static Fl_PNG_Image *input_connector_image = NULL;
static Fl_PNG_Image *output_connector_image = NULL;

extern char *instance_name;

#include "Mixer.H"
#include "Group.H"

#include "Project.H"

static JACK_Module *receptive_to_drop = NULL;

JACK_Module::JACK_Module( bool log )
    : Module( 25, 25, name( ) )
{
    is_jack_module ( true );
    _prefix = 0;

    _connection_handle_outputs[0][0] = 0;
    _connection_handle_outputs[0][1] = 0;
    _connection_handle_outputs[1][0] = 0;
    _connection_handle_outputs[1][1] = 0;

    align ( FL_ALIGN_TOP | FL_ALIGN_INSIDE );

    if ( log )
    {
        /* FIXME: how do Controls find out that a connected value has changed? How does this work in ladspa? */
        {
            Port p ( this, Port::INPUT, Port::CONTROL, "Inputs" );
            p.hints.type = Port::Hints::INTEGER;
            p.hints.minimum = 1;
            p.hints.maximum = MAX_PORTS;
            p.hints.ranged = true;
            p.hints.visible = false;

            p.connect_to ( new float );
            p.control_value_no_callback ( 0 );

            add_port ( p );
        }

        {
            Port p ( this, Port::INPUT, Port::CONTROL, "Outputs" );
            p.hints.type = Port::Hints::INTEGER;
            p.hints.minimum = 1;
            p.hints.maximum = MAX_PORTS;
            p.hints.ranged = true;
            p.hints.visible = false;

            p.connect_to ( new float );
            p.control_value_no_callback ( 0 );

            add_port ( p );
        }

        color ( FL_DARK1 );

        log_create ( );
    }

    {
        Fl_Scalepack *sp1 = new Fl_Scalepack ( x ( ) + Fl::box_dx ( box ( ) ),
            y ( ) + Fl::box_dy ( box ( ) ),
            w ( ) - Fl::box_dw ( box ( ) ),
            h ( ) - Fl::box_dh ( box ( ) ) );
        sp1->type ( Fl_Pack::VERTICAL );
        sp1->spacing ( 0 );

        {
            Fl_Scalepack *sp2 = new Fl_Scalepack ( x ( ) + Fl::box_dx ( box ( ) ),
                y ( ) + Fl::box_dy ( box ( ) ),
                w ( ),
                24 - Fl::box_dh ( box ( ) ) );
            sp2->type ( Fl_Pack::HORIZONTAL );

            sp2->spacing ( 0 );

            {
                Fl_Box *o = input_connection_handle = new Fl_Box ( x ( ), y ( ), 18, 18 );
                o->tooltip ( "Drag and drop to make and break JACK connections." );
                o->hide ( );
                o->image ( input_connector_image ? input_connector_image : input_connector_image = new Fl_PNG_Image ( "input_connector", img_io_input_connector_10x10_png, img_io_input_connector_10x10_png_len ) );

            }

            {
                Fl_Box *o = new Fl_Box ( x ( ) + 10, y ( ), w ( ) - 20, h ( ) );
                Fl_Group::current ( )->resizable ( o );
            }

            {
                Fl_Button *o = dec_button = new Fl_Button ( 0, 0, 12, h ( ), "-" );
                o->callback ( cb_button, this );
                o->labelsize ( 10 );
                o->labelfont ( FL_HELVETICA_BOLD );
                o->hide ( );
            }
            {
                Fl_Button *o = inc_button = new Fl_Button ( 0, 0, 12, h ( ), "+" );
                o->labelsize ( 10 );
                o->labelfont ( FL_HELVETICA_BOLD );
                o->callback ( cb_button, this );
                o->hide ( );
            }

            {
                Fl_Box *o = output_connection_handle = new Fl_Box ( x ( ), y ( ), 12, 12 );
                o->tooltip ( "Drag and drop to make and break JACK connections." );
                o->image ( output_connector_image ? output_connector_image : output_connector_image = new Fl_PNG_Image ( "output_connector", img_io_output_connector_10x10_png, img_io_output_connector_10x10_png_len ) );
                o->hide ( );
            }

            {
                Fl_Box *o = output_connection2_handle = new Fl_Box ( x ( ), y ( ), 12, 12 );
                o->tooltip ( "Drag and drop to make and break JACK connections." );
                o->image ( output_connector_image ? output_connector_image : output_connector_image = new Fl_PNG_Image ( "output_connector", img_io_output_connector_10x10_png, img_io_output_connector_10x10_png_len ) );
                o->hide ( );
            }

            sp2->end ( );
        }

        {
            Fl_Browser *o = connection_display = new Fl_Browser ( 0, 0, w ( ), h ( ) );
            o->has_scrollbar ( Fl_Browser_::VERTICAL );
            o->textsize ( 10 );
            o->textcolor ( fl_contrast ( FL_LIGHT3, FL_BACKGROUND_COLOR ) );
            o->textfont ( FL_COURIER );
            o->box ( FL_FLAT_BOX );
            o->color ( FL_DARK1 );
            // o->color( fl_color_add_alpha( fl_rgb_color( 10, 10, 10 ), 100 ));

            Fl_Group::current ( )->resizable ( o );
        }
        sp1->end ( );
        resizable ( sp1 );
    }
    end ( );
}

JACK_Module::~JACK_Module( )
{
    log_destroy ( );
    JACK_Module::configure_inputs ( 0 );
    JACK_Module::configure_outputs ( 0 );
    if ( _prefix )
        free ( _prefix );
}

void
JACK_Module::draw( void )
{
    Module::draw ( );
    if ( this == receptive_to_drop )
    {
        Fl_Widget *o = input_connection_handle;
#ifdef FLTK_SUPPORT
        fl_draw_box ( FL_OVAL_BOX, o->x ( ), o->y ( ), o->w ( ), o->h ( ), FL_GREEN );
#else
        fl_draw_box ( FL_OVAL_BOX, o->x ( ), o->y ( ), o->w ( ), o->h ( ), fl_color_add_alpha ( FL_GREEN, 127 ) );
#endif
    }
}

static std::list<std::string>
get_connections_for_ports( const std::vector<Module::Port> &ports )
{
    std::list<std::string> names;

    for ( unsigned int i = 0; i < ports.size ( ); ++i )
    {
        const char **connections = ports[i].jack_port ( )->connections ( );

        if ( !connections )
            return names;

        bool is_output = ports[i].jack_port ( )->direction ( ) == JACK::Port::Output;

        for ( const char **c = connections; *c; c++ )
        {
            char *client_id = 0;
            char *strip_name = 0;
            //      char *client_name = 0;

            if ( 2 == sscanf ( *c, "Non-Mixer-XT.%m[^:/]/%m[^:]:", &client_id, &strip_name ) )
            {
                free ( client_id );
                char *s = NULL;
                asprintf ( &s, "%s%s", is_output ? "@r" : "", strip_name );
                free ( strip_name );
                strip_name = s;
            }
            else if ( 2 == sscanf ( *c, "Non-Mixer-XT.%m[^:(] (%m[^:)]):", &client_id, &strip_name ) )
            {
                free ( client_id );
                char *s = NULL;
                asprintf ( &s, "%s%s", is_output ? "@r" : "", strip_name );
                free ( strip_name );
                strip_name = s;
            }
            else if ( 2 == sscanf ( *c, "Non-Timeline.%m[^:/]:%m[^/]/", &client_id, &strip_name ) )
            {
                free ( client_id );
                char *s = NULL;
                asprintf ( &s, "@C2%s%s", is_output ? "@r" : "", strip_name );
                free ( strip_name );
                strip_name = s;
            }
            else if ( 2 == sscanf ( *c, "Non-DAW.%m[^:/]:%m[^/]/", &client_id, &strip_name ) )
            {
                free ( client_id );
                char *s = NULL;
                asprintf ( &s, "@C2%s%s", is_output ? "@r" : "", strip_name );
                free ( strip_name );
                strip_name = s;
            }
            else if ( 1 == sscanf ( *c, "%m[^:]:", &strip_name ) )
            {
                char *s = NULL;
                asprintf ( &s, "@C3%s%s", is_output ? "@r" : "", strip_name );
                free ( strip_name );
                strip_name = s;
            }
            else
            {
                continue;
            }

            for ( std::list<std::string>::const_iterator j = names.begin ( );
                j != names.end ( );
                ++j )
            {
                if ( !strcmp ( j->c_str ( ), strip_name ) )
                {
                    goto skip;
                }
            }

            names.push_back ( strip_name );

skip:
            free ( strip_name );

            ;

        }
    }

    names.sort ( );
    return names;
}

void
JACK_Module::update_connection_status( void )
{
    /* do nothing when running in noui mode, as ->add will call some
     * font measurement stuff which attempts to open the X display. */
    if ( !fl_display )
    {
        return;
    }

    /* /\* causes a lot of unnecessary redraws to do this when loading *\/ */
    /* if ( Project::is_opening_closing() ) */
    /* 	return; */

    /* FIXME: only do something if port list actually changed! */
    std::list<std::string> output_names = get_connections_for_ports ( aux_audio_output );
    std::list<std::string> input_names = get_connections_for_ports ( aux_audio_input );

    if ( (unsigned int) connection_display->size ( ) == input_names.size ( ) + output_names.size ( ) )
        /* looks like nothing was added or removed, bail.
           FIXME: this would be better if it actually compared the lists item by item. */
        return;

    connection_display->clear ( );

    int n = 0;

    for ( std::list<std::string>::const_iterator j = input_names.begin ( );
        j != input_names.end ( );
        ++j )
    {
        connection_display->add ( j->c_str ( ) );
        n++;
    }
    for ( std::list<std::string>::const_iterator j = output_names.begin ( );
        j != output_names.end ( );
        ++j )
    {
        connection_display->add ( j->c_str ( ) );
        n++;
    }

    /* limit number of lines displayed */
    if ( n > 15 )
        n = 15;

    if ( n > 0 )
#ifdef FLTK_SUPPORT
    {
        // The connection_display->incr_height() for NTK is 12, the text size for FLTK is 10, so we add +2
        size ( w ( ), 26 + ( n * ( connection_display->textsize ( ) + 2 ) ) );
        DMESSAGE ( "textsize = %d", connection_display->textsize ( ) );
    }
#else
    {
        size ( w ( ), 26 + ( n * ( connection_display->incr_height ( ) ) ) );
        DMESSAGE ( "connection_display->incr_height()= %d", connection_display->incr_height ( ) );
    }
#endif
    else
        size ( w ( ), 24 );

    parent ( )->parent ( )->redraw ( );
}

void
JACK_Module::cb_button( Fl_Widget *w, void *v )
{
    ( (JACK_Module*) v )->cb_button ( w );
}

void
JACK_Module::cb_button( Fl_Widget *w )
{
    int n = audio_output.size ( );

    Logger log ( this );

    if ( w == dec_button )
    {
        --n;
    }
    else if ( w == inc_button )
    {
        ++n;
    }

    control_input[1].control_value ( n );
}

int
JACK_Module::can_support_inputs( int )
{
    return audio_output.size ( );
}

void
JACK_Module::remove_aux_audio_outputs( void )
{
    for ( unsigned int i = aux_audio_output.size ( ); i--; )
    {
        aux_audio_output.back ( ).jack_port ( )->shutdown ( );
        aux_audio_output.pop_back ( );
    }
}

bool
JACK_Module::configure_inputs( int n )
{
    if ( n > 0 )
    {
        output_connection_handle->show ( );
    }

    if ( n < 1 || n > MAX_PORTS )
        return false;

    int on = audio_input.size ( );

    if ( n > on )
    {
        for ( int i = on; i < n; ++i )
        {
            if ( add_aux_audio_output ( _prefix, i ) )
            {
                add_port ( Port ( this, Port::INPUT, Port::AUDIO ) );
            }
        }

        mixer->maybe_auto_connect_output ( &aux_audio_output.back ( ) );
    }
    else
    {
        for ( int i = on; i > n; --i )
        {
            audio_input.back ( ).disconnect ( );
            audio_input.pop_back ( );
            aux_audio_output.back ( ).disconnect ( );
            aux_audio_output.back ( ).jack_port ( )->shutdown ( );
            delete aux_audio_output.back ( ).jack_port ( );
            aux_audio_output.pop_back ( );

        }
    }

    _connection_handle_outputs[0][0] = 0;
    _connection_handle_outputs[0][1] = aux_audio_output.size ( );

    if ( is_default ( ) )
        control_input[0].control_value_no_callback ( n );

    return true;
}

bool
JACK_Module::configure_outputs( int n )
{
    int on = audio_output.size ( );

    if ( n > MAX_PORTS )
        return false;

    if ( n > 0 )
    {
        input_connection_handle->show ( );
    }

    if ( n > on )
    {
        for ( int i = on; i < n; ++i )
        {
            if ( add_aux_audio_input ( _prefix, i ) )
            {
                add_port ( Port ( this, Port::OUTPUT, Port::AUDIO ) );
            }
        }

        mixer->auto_connect ( );
    }
    else
    {
        for ( int i = on; i > n; --i )
        {
            audio_output.back ( ).disconnect ( );
            audio_output.pop_back ( );
            aux_audio_input.back ( ).disconnect ( );
            aux_audio_input.back ( ).jack_port ( )->shutdown ( );
            delete aux_audio_input.back ( ).jack_port ( );
            aux_audio_input.pop_back ( );
        }
    }

    if ( is_default ( ) )
        control_input[1].control_value_no_callback ( n );

    if ( n > 0 && is_default ( ) )
    {
        dec_button->show ( );
        inc_button->show ( );
    }

    return true;
}

void
JACK_Module::handle_control_changed( Port *p )
{
    //    THREAD_ASSERT( UI );

    if ( 0 == strcmp ( p->name ( ), "Inputs" ) )
    {
        DMESSAGE ( "Adjusting number of inputs (JACK outputs)" );
        configure_inputs ( p->control_value ( ) );
        if ( chain ( ) )
            chain ( )->configure_ports ( );
    }
    else if ( 0 == strcmp ( p->name ( ), "Outputs" ) )
    {
        DMESSAGE ( "Adjusting number of outputs (JACK inputs)" );

        if ( !chain ( ) )
        {
            configure_outputs ( p->control_value ( ) );
        }
        else if ( chain ( )->can_configure_outputs ( this, p->control_value ( ) ) )
        {
            configure_outputs ( p->control_value ( ) );
            chain ( )->configure_ports ( );
        }
        else
        {
            p->connected_port ( )->control_value ( noutputs ( ) );
        }
    }

    Module::handle_control_changed ( p );
}

int
JACK_Module::handle( int m )
{
    static unsigned long _event_state = 0;

    unsigned long evstate = Fl::event_state ( );

    switch ( m )
    {
        case FL_PUSH:
            if ( Fl::event_inside ( output_connection_handle ) ||
                Fl::event_inside ( output_connection2_handle ) ||
                Fl::event_inside ( input_connection_handle ) )
            {
                _event_state = evstate;
                return 1;
            }
            // This generates a warning from clang - is invalid in this case
            return Module::handle ( m ) || 1;   // always returns 1

        case FL_RELEASE:
            Fl::selection_owner ( 0 );
            receptive_to_drop = NULL;

            if ( Fl::event_inside ( output_connection_handle ) ||
                Fl::event_inside ( output_connection2_handle ) ||
                Fl::event_inside ( input_connection_handle ) )
            {
                if ( _event_state & FL_BUTTON3 )
                {
                    /* was a right click */
                    // TODO: Pop up connection menu.
                }
            }
            // This generates a warning from clang - is invalid in this case
            return Module::handle ( m ) || 1;   // always returns 1
        case FL_DRAG:
        {
            if ( Fl::event_is_click ( ) )
                return 1;

            int connection_handle = -1;
            if ( Fl::event_inside ( output_connection_handle ) )
                connection_handle = 0;
            if ( Fl::event_inside ( output_connection2_handle ) )
                connection_handle = 1;

            if ( Fl::event_button1 ( ) &&
                connection_handle >= 0
                && !Fl::selection_owner ( ) )
            {
                DMESSAGE ( "initiation of drag" );

                char *s = static_cast<char*> ( malloc ( 256 ) );

                if ( s == NULL )
                    return 0;

                s[0] = 0;

                for ( unsigned int i = _connection_handle_outputs[connection_handle][0];
                    i < aux_audio_output.size ( ) && i < _connection_handle_outputs[connection_handle][1]; ++i )
                {
                    char *s2;
                    char *tmp;
                    asprintf ( &s2, "jack.port://%s\r\n",
                        aux_audio_output[i].jack_port ( )->jack_name ( ) );

                    tmp = static_cast<char*> ( realloc ( s, strlen ( s ) + strlen ( s2 ) + 1 ) );

                    if ( tmp )
                    {
                        s = tmp;
                        strcat ( s, s2 );
                    }
                    else
                    {
                        WARNING ( "Unable to complete drag - realloc failed" );
                        free ( s );
                        free ( s2 );
                        return 1;
                    }
                    free ( s2 );
                }

                Fl::copy ( s, strlen ( s ) + 1, 0 );

                Fl::selection_owner ( this );

                free ( s );

                Fl::dnd ( );

                return 1;
            }

            return 1;
        }
        /* we have to prevent Fl_Group::handle() from getting these, otherwise it will mess up Fl::belowmouse() */
        case FL_MOVE:
            if ( Fl::event_inside ( output_connection_handle ) ||
                Fl::event_inside ( output_connection2_handle ) ||
                Fl::event_inside ( input_connection_handle ) )
                fl_cursor ( FL_CURSOR_HAND );
            else
                fl_cursor ( FL_CURSOR_DEFAULT );

            /* This calls Fl_Group::handle() which somehow prevent DND FL_PASTE event from being delivered later */
            /* Module::handle(m); */
            return 1;
        case FL_ENTER:
        case FL_DND_ENTER:
            Module::handle ( m );
            return 1;
        case FL_LEAVE:
        case FL_DND_LEAVE:
            Module::handle ( m );
            if ( this == receptive_to_drop )
            {
                receptive_to_drop = NULL;
                redraw ( );
            }
            fl_cursor ( FL_CURSOR_DEFAULT );
            return 1;
        case FL_DND_RELEASE:
            Fl::selection_owner ( 0 );
            receptive_to_drop = NULL;
            redraw ( );
            return 1;
        case FL_DND_DRAG:
        {
            if ( this == receptive_to_drop )
                return 1;

            if ( aux_audio_input.size ( ) )
            {

                receptive_to_drop = this;
                redraw ( );
                return 1;
            }

            return 0;
        }
        case FL_PASTE:
        {
            receptive_to_drop = NULL;
            redraw ( );

            if ( !Fl::event_inside ( this ) )
                return 0;

            /* NOW we get the text... */
            const char *text = Fl::event_text ( );

            DMESSAGE ( "Got drop text \"%s\"", text );

            if ( strncmp ( text, "jack.port://", strlen ( "jack.port://" ) ) )
            {
                return 0;
            }

            std::vector<std::string> port_names;

            char *port_name;
            int end;
            while ( sscanf ( text, "jack.port://%m[^\r\n]\r\n%n", &port_name, &end ) > 0 )
            {
                DMESSAGE ( "Scanning %s", port_name );
                port_names.push_back ( port_name );
                free ( port_name );

                text += end;
            }

            for ( unsigned int i = 0; i < aux_audio_input.size ( ) && i < port_names.size ( ); i++ )
            {
                const char *pn = port_names[i].c_str ( );

                JACK::Port *ji = aux_audio_input[i].jack_port ( );

                if ( ji->connected_to ( pn ) )
                {

                    DMESSAGE ( "Disconnecting from \"%s\"", pn );
                    ji->disconnect ( pn );
                }
                else
                {
                    DMESSAGE ( "Connecting to %s", pn );
                    ji->connect ( pn );
                }
            }

            Fl::selection_owner ( 0 );
            return 1;
        }
    }

    return Module::handle ( m );
}

/**********/
/* Engine */

/**********/

void
JACK_Module::process( nframes_t nframes )
{
    for ( unsigned int i = 0; i < audio_input.size ( ); ++i )
    {
        if ( audio_input[i].connected ( ) )
        {
            buffer_copy ( static_cast<sample_t*> ( aux_audio_output[i].jack_port ( )->buffer ( nframes ) ),
                static_cast<sample_t*> ( audio_input[i].buffer ( ) ),
                nframes );
        }

    }

    for ( unsigned int i = 0; i < audio_output.size ( ); ++i )
    {
        if ( audio_output[i].connected ( ) )
        {
            buffer_copy ( static_cast<sample_t*> ( audio_output[i].buffer ( ) ),
                static_cast<sample_t*> ( aux_audio_input[i].jack_port ( )->buffer ( nframes ) ),
                nframes );
        }
    }
}
