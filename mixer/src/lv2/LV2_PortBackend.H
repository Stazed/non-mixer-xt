/*******************************************************************************/
/* Copyright (C) 2026- Stazed                                                  */
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

#ifdef LV2_SUPPORT

#include "lv2_evbuf.h"
#include "lilvmm.hpp"
#include "../Module.H"

#define LV2_PORT(p) ((p)->backend<LV2_PortBackend>())

class LV2_PortBackend : public Module::PortBackend
{
public:

    struct EnumeratorScalePoints
    {
        std::string Label;
        float Value {0.f};

        static bool before(const EnumeratorScalePoints& a, const EnumeratorScalePoints& b)
        {
            return a.Value < b.Value;
        }
    };

    std::vector<EnumeratorScalePoints> ScalePoints;

    LV2_Evbuf* _evbuf {nullptr};

    const LilvNode* _property {nullptr};
    LilvNode* _lilv_symbol {nullptr};
    LilvNode* _lilv_label {nullptr};

    LV2_URID _property_mapped {0};

    bool _clear_input_buffer {false};
    bool _supports_time_position {false};

    std::string _file;
    
    PortBackend* clone() const override
    {
        auto* c = new LV2_PortBackend(*this);

        /* event buffers should not be shared */
        c->_evbuf = nullptr;

        return c;
    }
    
    void event_buffer ( LV2_Evbuf * evbuf )
    {
        _evbuf = evbuf;
    }
    LV2_Evbuf* event_buffer ( void ) const
    {
        return _evbuf;
    }

    ~LV2_PortBackend()
    {
        if (_evbuf)
            lv2_evbuf_free(_evbuf);
    }
};

#endif