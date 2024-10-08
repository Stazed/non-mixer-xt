
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

#pragma once

#include "Module.H"
#include <vector>

#include "../../nonlib/JACK/Port.H"
#include "../../nonlib/OSC/Endpoint.H"


class Fl_Menu_Button;
class Fl_Menu_;

class Fl_Valuator;

class Controller_Module : public Module
{

    bool _horizontal;
    bool _pad;

    volatile float control_value;

    Fl_Menu_Button & menu ( void );
    static void menu_cb ( Fl_Widget *w, void *v );
    void menu_cb ( const Fl_Menu_ *m );


    static void peer_callback(  OSC::Signal *sig,  OSC::Signal::State state, void *v );
    void peer_callback( OSC::Signal *sig, OSC::Signal::State state );
    void add_osc_peers_to_menu ( Fl_Menu_Button *m, const char *prefix );
    void add_osc_connections_to_menu ( Fl_Menu_Button *m, const char *prefix );

    static void learning_callback ( void *userdata );
    void learning_callback ( void );

public:

    static bool _learn_mode;
    static Controller_Module *_learning_control;

    static bool learn_by_number;
    static bool learn_mode ( void )
    {
        return _learn_mode;
    }
    static void learn_mode ( bool b )
    {
        _learn_mode = b;
    }

    enum Mode { GUI, CV, OSC, MIDI };

    enum Type { KNOB,
                SLIDER,
                SPINNER,
                TOGGLE,
                CHOICE,
                SPATIALIZATION,
              };

    Mode mode ( void ) const
    {
        return _mode;
    }
    void mode ( Mode v );

    void horizontal ( bool v )
    {
        _horizontal = v;
    }
    Type type ( void ) const
    {
        return _type;
    }

    Controller_Module ( bool is_default = false );
    virtual ~Controller_Module ( );

    const char *name ( void ) const override
    {
        return "Controller";
    }

    int can_support_inputs ( int ) override
    {
        return 0;
    }
    bool configure_inputs ( int ) override
    {
        return false;
    }

    void pad ( bool v )
    {
        _pad = v;
    }

    static void cb_handle ( Fl_Widget *w, void *v );
    void cb_handle ( Fl_Widget *w );
    static void cb_spatializer_handle ( Fl_Widget *w, void *v );
    void cb_spatializer_handle ( Fl_Widget *w );

    void connect_to ( Port *p );
    bool connect_spatializer_to ( Module *m );
    bool connect_spatializer_radius_to ( Module *m );
    void disconnect ( void );

    void handle_control_changed ( Port *p ) override;
    void handle_chain_name_changed ( void ) override;
    void handle_control_disconnect ( Port *p ) override;

    virtual void command_remove ( void ) override;

    LOG_CREATE_FUNC( Controller_Module );

    virtual void update ( void ) override;

    virtual void process ( nframes_t nframes ) override;

    void draw ( void ) override;

    int handle ( int m ) override;

    void take_focus ( void );
//    void set_control_value ( float f ) { control_value = f; }

protected:

    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

private:

    void apply_label ( Port *p, Fl_Widget *o );
    void maybe_create_panner ( void );
    char *generate_osc_path ( void );
    void change_osc_path ( char *path );

    Mode _mode;
    Type _type;

    Fl_Widget *control;

};
