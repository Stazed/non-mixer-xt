
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


class Fl_Valuator;
class Fl_Scalepack;

class Meter_Indicator_Module : public Module
{
    Fl_Scalepack *dpm_pack;


    bool _pad;

    volatile float *control_value;

    bool _disable_context_menu;

public:

    virtual void update ( void ) override;

    void disable_context_menu ( bool b )
    {
        _disable_context_menu = b;
    }

    void handle_control_changed ( Port *p ) override;

    Meter_Indicator_Module ( bool is_default = false );
    virtual ~Meter_Indicator_Module ( );

    bool allows_external_control ( void ) const
    {
        return false;
    }
    const char *name ( void ) const override
    {
        return "Meter Indicator";
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

    void connect_to ( Port *p );

    LOG_CREATE_FUNC( Meter_Indicator_Module );

    virtual void process ( nframes_t ) override;

protected:

    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

    virtual void draw ( void ) override;
    virtual void resize ( int X, int Y, int W, int H ) override;
    virtual int handle ( int m ) override;

private:

};
