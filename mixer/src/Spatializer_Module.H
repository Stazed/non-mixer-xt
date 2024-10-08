
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

#include "JACK_Module.H"

#include "../../nonlib/dsp.h"

#include <vector>

class filter;
class delay;
class ambisonic_panner;
class Spatializer_Module : public JACK_Module
{
    Value_Smoothing_Filter gain_smoothing;
    Value_Smoothing_Filter delay_smoothing;
    Value_Smoothing_Filter late_gain_smoothing;
    Value_Smoothing_Filter early_gain_smoothing;
    Value_Smoothing_Filter azimuth_smoothing;
    Value_Smoothing_Filter elevation_smoothing;

    std::vector<filter*> _lowpass;
    std::vector<filter*> _highpass;
    std::vector<delay*> _delay;

    ambisonic_panner *_panner;
    ambisonic_panner *_early_panner;

public:

    virtual const char *name ( void ) const override
    {
        return "Spatializer";
    }

    int can_support_inputs ( int n ) override
    {
        return n > 0 && n < 3 ? 4 : -1;
    }

    virtual bool configure_inputs ( int n ) override;

    Spatializer_Module ( );
    virtual ~Spatializer_Module ( );

    LOG_CREATE_FUNC( Spatializer_Module );

    MODULE_CLONE_FUNC(Spatializer_Module);

    virtual void handle_sample_rate_change ( nframes_t n ) override;
    virtual void handle_control_changed ( Port *p ) override;
    virtual void draw ( void ) override;

protected:

    virtual void process ( nframes_t nframes ) override;

};

