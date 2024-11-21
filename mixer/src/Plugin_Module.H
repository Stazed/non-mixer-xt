
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

#include "../../nonlib/Loggable.H"

class Fl_Menu_Button;

class Plugin_Module : public Module
{

private:

    nframes_t _last_latency;

    void bbox ( int &X, int &Y, int &W, int &H ) override
    {
        X = x();
        Y = y() + 5;
        W = w();
        H = h() - 10;
    }

public:

    int _plugin_ins;
    int _plugin_outs;
    bool _crosswire;

private:

    void connect_ports ( void );

public:

    virtual bool load_plugin ( Module::Picked /* picked */ )
    {
        return false;
    };

    virtual void update ( void ) override;

    Plugin_Module ( );
    virtual ~Plugin_Module();

    int plugin_ins ( void ) const
    {
        return _plugin_ins;
    }
    int plugin_outs ( void ) const
    {
        return _plugin_outs;
    }

    const char *name ( void ) const override
    {
        return "Plugin";
    }

    int can_support_inputs ( int ) override;
    bool configure_inputs ( int ) override;

    virtual bool bypass ( void ) const override
    {
        return *_bypass == 1.0f;
    }
    virtual void bypass ( bool /*v*/ ) override {};

    virtual void process ( nframes_t ) override {};

    void resize_buffers ( nframes_t buffer_size ) override;

    virtual void clear_midi_vectors() override {};
    
    nframes_t get_current_latency( void ) override
    {
        return _latency;
    }

    LOG_CREATE_FUNC( Plugin_Module );
    MODULE_CLONE_FUNC( Plugin_Module );

protected:

    std::string get_custom_data_location(const std::string &path);

    volatile nframes_t _latency;
    void init ( void ) override;

    void get ( Log_Entry & /*e*/ ) const override {};
    void set ( Log_Entry &e ) override;

};
