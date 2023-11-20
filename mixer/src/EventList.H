/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

/* 
 * File:   EventList.H
 * Author: sspresto
 *
 * Created on November 20, 2023, 12:30 PM
 * From Qtractor - qtractorClapPlugin
 */

#pragma once

#ifdef CLAP_SUPPORT

#include <vector>
#include <cstring>  // memset

class EventList
{
public:

        EventList ( uint32_t nsize = 1024, uint32_t ncapacity = 8 )
                : m_nsize(0), m_eheap(nullptr),
                        m_ehead(nullptr), m_etail(nullptr), m_ihead(0)
        {
                resize(nsize);

                m_elist.reserve(ncapacity);

                ::memset(&m_ins, 0, sizeof(m_ins));
                m_ins.ctx  = this;
                m_ins.size = &events_in_size;
                m_ins.get  = &events_in_get;

                ::memset(&m_outs, 0, sizeof(m_outs));
                m_outs.ctx = this;
                m_outs.try_push = &events_out_push;
        }

        ~EventList () { resize(0); }

        const clap_input_events *ins () const
                { return &m_ins; }
        const clap_output_events *outs () const
                { return &m_outs; }

        bool push ( const clap_event_header *eh )
        {
                const uint32_t ntail = m_etail - m_eheap;
                const uint32_t nsize = ntail + eh->size;
                if (m_nsize < nsize)
                        resize(nsize << 1);
                const uint32_t ncapacity = m_elist.capacity();
                if (m_elist.size() >= ncapacity)
                        m_elist.reserve(ncapacity << 1);
                m_elist.push_back(ntail);
                ::memcpy(m_etail, eh, eh->size);
                m_etail += eh->size;

                return true;
        }

        const clap_event_header *get ( uint32_t index ) const
        {
                const clap_event_header *ret = nullptr;
                if (index + m_ihead < m_elist.size()) {
                        ret = reinterpret_cast<const clap_event_header *> (
                                m_eheap + m_elist.at(index + m_ihead));
                }
                return ret;
        }

        const clap_event_header *pop ()
        {
                const clap_event_header *ret = nullptr;
                if (m_ihead < m_elist.size() && m_ehead < m_etail) {
                        ret = reinterpret_cast<const clap_event_header *> (m_ehead);
                        m_ehead += ret->size;
                        ++m_ihead;
                }
                else clear();
                return ret;
        }

        size_t size () const
                { return m_elist.size() - m_ihead; }

        bool empty () const
                { return (m_etail == m_ehead); }

        void clear ()
        {
                m_ehead = m_eheap;
                m_etail = m_ehead;
                m_ihead = 0;
                m_elist.clear();
        }

protected:

        void resize ( uint32_t nsize )
        {
                uint8_t *old_eheap = m_eheap;
                uint8_t *old_ehead = m_ehead;
                uint8_t *old_etail = m_etail;
                m_eheap = nullptr;
                m_ehead = m_eheap;
                m_etail = m_ehead;
                m_nsize = nsize;
                if (m_nsize > 0) {
                        m_eheap = new uint8_t [m_nsize];
                        m_ehead = m_eheap;
                        m_etail = m_ehead;
                        if (old_etail > old_ehead) {
                                const uint32_t ntail
                                        = old_etail - old_ehead;
                                ::memcpy(m_ehead, old_ehead, ntail);
                                m_etail += ntail;
                        }
                }
                if (old_eheap)
                        delete [] old_eheap;
        }

        static uint32_t events_in_size (
                const clap_input_events *ins )
        {
                const EventList *elist
                        = static_cast<const EventList *> (ins->ctx);
                return elist->size();
        }

        static const clap_event_header_t *events_in_get (
                const struct clap_input_events *ins, uint32_t index )
        {
                const EventList *elist
                        = static_cast<const EventList *> (ins->ctx);
                return elist->get(index);
        }

        static bool events_out_push (
                const clap_output_events *outs,
                const clap_event_header *eh )
        {
                EventList *elist = static_cast<EventList *> (outs->ctx);
                return elist->push(eh);
        }

private:

        uint32_t m_nsize;
        uint8_t *m_eheap;
        uint8_t *m_ehead;
        uint8_t *m_etail;
        uint32_t m_ihead;

        std::vector<uint32_t> m_elist;

        clap_input_events  m_ins;
        clap_output_events m_outs;
};

#endif // CLAP_SUPPORT
