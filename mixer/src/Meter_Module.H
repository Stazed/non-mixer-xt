
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

#include "../../nonlib/dsp.h"

#include <vector>

class Fl_Scalepack;

class Meter_Module : public Module
{
    Fl_Scalepack *dpm_pack;

    // This cannot be correct, only usage 'smoothing.pop_back();' in Meter_Module.C line 164.
//   std::vector <Value_Smoothing_Filter> smoothing;

    volatile float *control_value;
    volatile float *peaks;

    int meter_sample_periods;	/* no need to do computations every
				 * buffer when the gui only updates at
				 * 30Hz. So only do it every n
				 * buffers */
    int meter_sample_period_count;

    void set_smoothing_sample_rate ( nframes_t nframes, nframes_t sample_rate );

public:

    Meter_Module ( );
    virtual ~Meter_Module ( );

    const char *name ( void ) const override
    {
        return "Meter";
    }

    int can_support_inputs ( int n ) override
    {
        return n > 0 ? n : -1;
    }
    bool configure_inputs ( int n ) override;

    LOG_CREATE_FUNC( Meter_Module );

    virtual void update ( void ) override;

protected:

    virtual int handle ( int m ) override;
    virtual void process ( nframes_t nframes ) override;
    virtual void draw ( void ) override;
    virtual void resize ( int X, int Y, int W, int H ) override;
};
