#pragma once

#ifdef LV2_SUPPORT

#include "lv2_evbuf.h"
#include "lilvmm.hpp"
#include "../Module.H"

class LV2_PortBackend : public Module::PortBackend
{
public:

    struct EnumeratorScalePoints
    {
        std::string label;
        float value {0.f};

        static bool before(const EnumeratorScalePoints& a, const EnumeratorScalePoints& b)
        {
            return a.value < b.value;
        }
    };

    std::vector<EnumeratorScalePoints> scale_points;

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