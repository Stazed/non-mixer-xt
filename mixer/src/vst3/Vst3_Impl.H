/*******************************************************************************/
/* Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.    */
/* Copyright (C) 2024- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
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


/* 
 * File:   Vst3_Impl.H
 * Author: sspresto
 *
 * Created on December 23, 2023, 8:17 AM
 */

#pragma once

#ifdef VST3_SUPPORT

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/gui/iplugview.h"

using namespace Steinberg;
using namespace Linux;

namespace VST3IMPL
{
//----------------------------------------------------------------------
// class ParamQueue -- VST3 plugin parameter queue impl.
//

class ParamQueue : public Vst::IParamValueQueue
{
public:

    // Constructor.
    ParamQueue (int32 nsize = 8)
        : m_id(Vst::kNoParamId), m_queue(nullptr), m_nsize(0), m_ncount(0)
        { FUNKNOWN_CTOR	resize(nsize); }

    // Destructor.
    virtual ~ParamQueue ()
        { resize(0); FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IParamValueQueue ---
    //
    Vst::ParamID PLUGIN_API getParameterId () override
        { return m_id; }

    int32 PLUGIN_API getPointCount () override
        { return m_ncount; }

    tresult PLUGIN_API getPoint (
            int32 index, int32& offset, Vst::ParamValue& value ) override
    {
        if (index < 0 || index >= m_ncount)
            return kResultFalse;

        const QueueItem& item = m_queue[index];
        offset = item.offset;
        value = item.value;
        return kResultOk;
    }

    tresult PLUGIN_API addPoint (
            int32 offset, Vst::ParamValue value, int32& index ) override
    {
        int32 i = 0;

        for ( ; i < m_ncount; ++i)
        {
            QueueItem& item = m_queue[i];
            if (item.offset > offset)
                    break;
            if (item.offset == offset)
            {
                item.value = value;
                index = i;
                return kResultOk;
            }
        }

        if (i >= m_nsize)
            resize(m_nsize); // warning: non RT-safe!

        index = i;

        QueueItem& item = m_queue[index];
        item.value = value;
        item.offset = offset;
        i = m_ncount++;
        while (i > index)
        {
            QueueItem& item2 = m_queue[i];
            QueueItem& item1 = m_queue[--i];
            item2.value  = item1.value;
            item2.offset = item1.offset;
        }

        return kResultOk;
    }

    // Helper methods.
    //
    void setParameterId (Vst::ParamID id) { m_id = id; }

    void takeFrom (ParamQueue& queue)
    {
        m_id     = queue.m_id;
        m_queue  = queue.m_queue;
        m_nsize  = queue.m_nsize;
        m_ncount = queue.m_ncount;

        queue.m_id     = Vst::kNoParamId;
        queue.m_queue  = nullptr;
        queue.m_nsize  = 0;
        queue.m_ncount = 0;
    }

    void clear () { m_ncount = 0; }

protected:

    void resize (int32 nsize)
    {
        const int32 nsize2 = (nsize << 1);
        if (m_nsize != nsize2)
        {
            QueueItem *old_queue = m_queue;
            m_queue = nullptr;
            m_nsize = nsize2;
            if (m_nsize > 0)
            {
                m_queue = new QueueItem [m_nsize];

                if (m_ncount > m_nsize)
                    m_ncount = m_nsize;

                for (int32 i = 0; old_queue && i < m_ncount; ++i)
                    m_queue[i] = old_queue[i];
            }

            if (old_queue)
                delete [] old_queue;
        }
    }

private:

    // Instance members.
    struct QueueItem
    {
        QueueItem (Vst::ParamValue val = 0.0, int32 offs = 0)
            : value(val), offset(offs) {}

        Vst::ParamValue value;
        int32 offset;
    };

    Vst::ParamID m_id;
    QueueItem *m_queue;
    int32 m_nsize;
    volatile int32 m_ncount;
};


//----------------------------------------------------------------------
// class ParamChanges -- VST3 plugin parameter changes impl.
//

class ParamChanges : public Vst::IParameterChanges
{
public:

    // Constructor.
    ParamChanges (int32 nsize = 4)
        : m_queues(nullptr), m_nsize(0), m_ncount(0)
        { FUNKNOWN_CTOR resize(nsize); }

    // Destructor.
    virtual ~ParamChanges ()
        { resize(0); FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IParameterChanges ----
    //
    int32 PLUGIN_API getParameterCount () override
        { return m_ncount; }

    Vst::IParamValueQueue *PLUGIN_API getParameterData (int32 index) override
    {
        if (index >= 0 && index < m_ncount)
            return &m_queues[index];
        else
            return nullptr;
    }

    Vst::IParamValueQueue *PLUGIN_API addParameterData (
            const Vst::ParamID& id, int32& index ) override
    {
        int32 i = 0;

        for ( ; i < m_ncount; ++i)
        {
            ParamQueue *queue = &m_queues[i];
            if (queue->getParameterId() == id)
            {
                index = i;
                return queue;
            }
        }

        if (i >= m_nsize)
            resize(m_nsize); // warning: non RT-safe!

        if (i >= m_ncount)
            ++m_ncount;

        index = i;

        ParamQueue *queue = &m_queues[index];
        queue->setParameterId(id);
        return queue;
    }

    // Helper methods.
    //
    void clear ()
    {
        for (int32 i = 0; i < m_ncount; ++i)
            m_queues[i].clear();

        m_ncount = 0;
    }

protected:

    void resize (int32 nsize)
    {
        const int32 nsize2 = (nsize << 1);
        if (m_nsize != nsize2)
        {
            DMESSAGE("ParamChanges[%p]::resize(%d)", this, nsize);

            ParamQueue *old_queues = m_queues;
            m_queues = nullptr;
            m_nsize = nsize2;
            if (m_nsize > 0)
            {
                m_queues = new ParamQueue [m_nsize];

                if (m_ncount > m_nsize)
                    m_ncount = m_nsize;

                for (int32 i = 0; old_queues && i < m_ncount; ++i)
                    m_queues[i].takeFrom(old_queues[i]);
            }

            if (old_queues)
                delete [] old_queues;
        }
    }

private:

    // Instance members.
    ParamQueue *m_queues;
    int32 m_nsize;
    volatile int32 m_ncount;
};

//----------------------------------------------------------------------
// class EventList -- VST3 plugin event list impl.
//

class EventList : public Vst::IEventList
{
public:

    // Constructor.
    EventList (uint32 nsize = 0x100)
        : m_events(nullptr), m_nsize(0), m_ncount(0)
        { resize(nsize); FUNKNOWN_CTOR }

    // Destructor.
    virtual ~EventList ()
        { resize(0); FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    //--- IEventList ---
    //
    int32 PLUGIN_API getEventCount () override
        { return m_ncount; }

    tresult PLUGIN_API getEvent (int32 index, Vst::Event& event) override
    {
        if (index < 0 || index >= m_nsize)
                return kInvalidArgument;

        event = m_events[index];

        return kResultOk;
    }

    tresult PLUGIN_API addEvent (Vst::Event& event) override
    {
        if (m_ncount >= m_nsize)
            resize(m_nsize);  // warning: non RT-safe!

        m_events[m_ncount++] = event;

        return kResultOk;
    }

    // Helper methods.
    //
    void clear () { m_ncount = 0; }

protected:

    void resize (int32 nsize)
    {
        const int32 nsize2 = (nsize << 1);
        if (m_nsize != nsize2)
        {
            DMESSAGE("EventList[%p]::resize(%d)", this, nsize);
            Vst::Event *old_events = m_events;
            m_events = nullptr;
            m_nsize = nsize2;
            if (m_nsize > 0)
            {
                m_events = new Vst::Event [m_nsize];

                if (m_ncount > m_nsize)
                    m_ncount = m_nsize;

                if (old_events)
                    ::memcpy(&m_events[0], old_events,
                            sizeof(Vst::Event) * m_ncount);

                if (m_nsize > m_ncount)
                    ::memset(&m_events[m_ncount], 0,
                            sizeof(Vst::Event) * (m_nsize - m_ncount));
            }

            if (old_events)
                delete [] old_events;
        }
    }

private:

    // Instance members.
    Vst::Event *m_events;
    int32 m_nsize;
    volatile int32 m_ncount;
};

}   // namespace VST3IMP

#endif // VST3_SUPPORTL