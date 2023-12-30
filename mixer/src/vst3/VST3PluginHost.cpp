/*******************************************************************************/
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
 * File:   VST3PluginHost.cpp
 * Author: sspresto
 * 
 * Created on December 29, 2023, 1:36 PM
 */

#ifdef VST3_SUPPORT

#include "VST3PluginHost.h"
#include "../../../nonlib/debug.h"
#include "VST3_Plugin.H"
#undef WARNING      // Fix redefinition with /nonlib/debug.h"

#define WARNING( fmt, args... ) warnf( W_WARNING, __MODULE__, __FILE__, __FUNCTION__, __LINE__, fmt, ## args )


std::string utf16_to_utf8(const std::u16string& utf16)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert; 
    std::string utf8 = convert.to_bytes(utf16);
    return utf8;
}

// Constructor.
VST3PluginHost::VST3PluginHost (VST3_Plugin * plug)
{
    FUNKNOWN_CTOR

    m_plugin = plug;
    m_plugInterfaceSupport = owned(NEW PlugInterfaceSupport());

    m_pTimer = new Timer(this);

    m_timerRefCount = 0;

#ifdef CONFIG_VST3_XCB
    m_pXcbConnection = nullptr;
    m_iXcbFileDescriptor = 0;
#endif

    m_processRefCount = 0;
}


// Destructor.
VST3PluginHost::~VST3PluginHost (void)
{
    clear();

    delete m_pTimer;

    m_plugInterfaceSupport = nullptr;

    FUNKNOWN_DTOR
}

//--- IHostApplication ---
//
tresult PLUGIN_API VST3PluginHost::getName ( Vst::String128 name )
{
    const std::string str("VST3PluginHost");
    const int nsize = str.length() < 127 ? str.length() : 127;
//    const QString str("qtractorVst3PluginHost");
//    const int nsize = qMin(str.length(), 127);
    ::memcpy(name, str.c_str(), nsize * sizeof(Vst::TChar));
    name[nsize] = 0;
    return kResultOk;
}


tresult PLUGIN_API VST3PluginHost::createInstance (
	TUID cid, TUID _iid, void **obj )
{
    const FUID classID (FUID::fromTUID(cid));
    const FUID interfaceID (FUID::fromTUID(_iid));

    if (classID == Vst::IMessage::iid &&
            interfaceID == Vst::IMessage::iid)
    {
        *obj = new Message();
        return kResultOk;
    }
    else
    if (classID == Vst::IAttributeList::iid &&
            interfaceID == Vst::IAttributeList::iid)
    {
        *obj = new AttributeList();
        return kResultOk;
    }

    *obj = nullptr;
    return kResultFalse;
}


tresult PLUGIN_API VST3PluginHost::queryInterface (
	const char *_iid, void **obj )
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IHostApplication)
    QUERY_INTERFACE(_iid, obj, IHostApplication::iid, IHostApplication)

    if (m_plugInterfaceSupport &&
            m_plugInterfaceSupport->queryInterface(_iid, obj) == kResultOk)
    {
        return kResultOk;
    }

    *obj = nullptr;
    return kResultFalse;
}


uint32 PLUGIN_API VST3PluginHost::addRef (void)
    { return 1; }

uint32 PLUGIN_API VST3PluginHost::release (void)
    { return 1; }


// Timer stuff...
//
void VST3PluginHost::startTimer ( int msecs )
    { if (++m_timerRefCount == 1) m_pTimer->start(msecs); }

void VST3PluginHost::stopTimer (void)
    { if (m_timerRefCount > 0 && --m_timerRefCount == 0) m_pTimer->stop(); }

int VST3PluginHost::timerInterval (void) const
    { return m_pTimer->interval(); }

// IRunLoop stuff...
//
tresult VST3PluginHost::registerEventHandler (
	IEventHandler *handler, FileDescriptor fd )
{
    DMESSAGE("registerEventHandler(%p, %d)", handler, int(fd));

    std::pair<IEventHandler *, int> Attr ( handler, int(fd) );
    m_eventHandlers.insert(Attr);

    m_plugin->event_handlers_registered(true);
    //m_eventHandlers.insert(handler, int(fd));
    return kResultOk;
}


tresult VST3PluginHost::unregisterEventHandler ( IEventHandler *handler )
{
    DMESSAGE("unregisterEventHandler(%p)", handler);

    m_eventHandlers.erase(handler);

   // m_eventHandlers.remove(handler);
    return kResultOk;
}

#if 1
tresult VST3PluginHost::registerTimer (
	ITimerHandler *handler, TimerInterval msecs )
{
    DMESSAGE("registerTimer(%p, %u)", handler, uint(msecs));

    std::unordered_map<ITimerHandler *, TimerHandlerItem *>::const_iterator got
            = m_timerHandlers.find(handler);

    TimerHandlerItem *timer_handler = nullptr;
    if ( got == m_timerHandlers.end() )
    {
        timer_handler = new TimerHandlerItem(handler, msecs);

        std::pair<ITimerHandler *, TimerHandlerItem *> infos ( handler, timer_handler );
        m_timerHandlers.insert(infos);
    }
    else
    {
        timer_handler->reset(msecs);
    }
    
    m_plugin->timer_registered(true);
    
    DMESSAGE("GGOT HERE REG - (%p)", m_plugin);

//    g_VST3_Plugin->timer_registered(true);
//    DMESSAGE("GGOT HERE REG - (%p)", g_VST3_Plugin);

#if 0
    TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
    if (timer_handler) {
            timer_handler->reset(msecs);
    } else {
            timer_handler = new TimerHandlerItem(handler, msecs);
            m_timerHandlers.insert(handler, timer_handler);
    }

    m_pTimer->start(int(msecs));
#endif

    return kResultOk;
}


tresult VST3PluginHost::unregisterTimer ( ITimerHandler *handler )
{
    DMESSAGE("unregisterTimer(%p)", handler);

    std::unordered_map<ITimerHandler *, TimerHandlerItem *>::const_iterator got
            = m_timerHandlers.find(handler);

    if ( got != m_timerHandlers.end() )
    {
        m_timerHandlers.erase(handler);
    }

    if (m_timerHandlers.empty())
        m_pTimer->stop();
    
    
#if 0
    TimerHandlerItem *timer_handler = m_timerHandlers.value(handler, nullptr);
    if (timer_handler) {
            m_timerHandlers.remove(handler);
            m_timerHandlerItems.append(timer_handler);
    }
    if (m_timerHandlers.isEmpty())
            m_pTimer->stop();

#endif
    return kResultOk;
}

// Executive methods.
//
void VST3PluginHost::processTimers (void)
{
    for (auto const& pair : m_timerHandlers)
    {
        TimerHandlerItem *timer_handler = pair.second;
        timer_handler->counter += timerInterval();
        if (timer_handler->counter >= timer_handler->interval)
        {
            timer_handler->handler->onTimer();
            timer_handler->counter = 0;
        }
    }
#if 0
    foreach (TimerHandlerItem *timer_handler, m_timerHandlers) {
            timer_handler->counter += timerInterval();
            if (timer_handler->counter >= timer_handler->interval) {
                    timer_handler->handler->onTimer();
                    timer_handler->counter = 0;
            }
    }
#endif
}
#endif

void VST3PluginHost::processEventHandlers (void)
{
    int nfds = 0;

    fd_set rfds;
    fd_set wfds;
    fd_set efds;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

#ifdef CONFIG_VST3_XCB
    if (m_iXcbFileDescriptor) {
            FD_SET(m_iXcbFileDescriptor, &rfds);
            FD_SET(m_iXcbFileDescriptor, &wfds);
            FD_SET(m_iXcbFileDescriptor, &efds);
            nfds = qMax(nfds, m_iXcbFileDescriptor);
    }
#endif
    
    for (auto const& pair : m_eventHandlers)
    {
        auto fd = pair.second;
        FD_SET(fd, &rfds);
        FD_SET(fd, &wfds);
        FD_SET(fd, &efds);
        nfds = nfds > fd ? nfds : fd;
    }
    
    timeval timeout;
    timeout.tv_sec  = 0;
    timeout.tv_usec = 1000 * timerInterval();

    const int result = ::select(nfds, &rfds, &wfds, nullptr, &timeout);
    if (result > 0)
    {
        for (auto const& pair : m_eventHandlers)
        {
            auto fd = pair.second;
            if (FD_ISSET(fd, &rfds) ||
                    FD_ISSET(fd, &wfds) ||
                    FD_ISSET(fd, &efds))
            {
                IEventHandler *handler = pair.first;
                handler->onFDIsSet(fd);
            }
        }
    }

#if 0
    QMultiHash<IEventHandler *, int>::ConstIterator iter
            = m_eventHandlers.constBegin();
    for ( ; iter != m_eventHandlers.constEnd(); ++iter) {
            foreach (int fd, m_eventHandlers.values(iter.key())) {
                    FD_SET(fd, &rfds);
                    FD_SET(fd, &wfds);
                    FD_SET(fd, &efds);
                    nfds = qMax(nfds, fd);
            }
    }

    timeval timeout;
    timeout.tv_sec  = 0;
    timeout.tv_usec = 1000 * timerInterval();

    const int result = ::select(nfds, &rfds, &wfds, nullptr, &timeout);
    if (result > 0)	{
            iter = m_eventHandlers.constBegin();
            for ( ; iter != m_eventHandlers.constEnd(); ++iter) {
                    foreach (int fd, m_eventHandlers.values(iter.key())) {
                            if (FD_ISSET(fd, &rfds) ||
                                    FD_ISSET(fd, &wfds) ||
                                    FD_ISSET(fd, &efds)) {
                                    IEventHandler *handler = iter.key();
                                    handler->onFDIsSet(fd);
                            }
                    }
            }
    }
#endif
}

#ifdef CONFIG_VST3_XCB

void VST3PluginHost::openXcbConnection (void)
{
	if (m_pXcbConnection == nullptr) {
	#ifdef CONFIG_DEBUG
		qDebug("qtractorVst3PluginHost::openXcbConnection()");
	#endif
		m_pXcbConnection = ::xcb_connect(nullptr, nullptr);
		m_iXcbFileDescriptor = ::xcb_get_file_descriptor(m_pXcbConnection);
	}
}

void VST3PluginHost::closeXcbConnection (void)
{
	if (m_pXcbConnection) {
		::xcb_disconnect(m_pXcbConnection);
		m_pXcbConnection = nullptr;
		m_iXcbFileDescriptor = 0;
	#ifdef CONFIG_DEBUG
		qDebug("qtractorVst3PluginHost::closeXcbConnection()");
	#endif
	}
}

#endif	// CONFIG_VST3_XCB


// Common host time-keeper context accessor.
Vst::ProcessContext *VST3PluginHost::processContext (void)
{
    return &m_processContext;
}


void VST3PluginHost::processAddRef (void)
{
    ++m_processRefCount;
}


void VST3PluginHost::processReleaseRef (void)
{
    if (m_processRefCount > 0) --m_processRefCount;
}

// Common host time-keeper process context.
void VST3PluginHost::updateProcessContext (
	jack_position_t &pos, const bool &xport_changed, const bool &has_bbt )
{
    if (m_processRefCount < 1)
        return;

    if (xport_changed)
        m_processContext.state |=  Vst::ProcessContext::kPlaying;
    else
        m_processContext.state &= ~Vst::ProcessContext::kPlaying;
    
    if(has_bbt)
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;

        m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid;
        m_processContext.projectTimeMusic = pos.beat;
        m_processContext.state |= Vst::ProcessContext::kBarPositionValid;
        m_processContext.barPositionMusic = pos.bar;

        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo  = pos.beats_per_minute;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = pos.beats_per_bar;
        m_processContext.timeSigDenominator = pos.beat_type;
    }
    else
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;
        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo  = 120.0;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = 4;
        m_processContext.timeSigDenominator = 4;
    }
}

// Cleanup.
void VST3PluginHost::clear (void)
{
#ifdef CONFIG_VST3_XCB
    closeXcbConnection();
#endif

    m_timerRefCount = 0;
    m_processRefCount = 0;

    for (auto i : m_timerHandlerItems)
    {
        delete i;
    }

//    qDeleteAll(m_timerHandlerItems);
    m_timerHandlerItems.clear();
    m_timerHandlers.clear();

    m_eventHandlers.clear();

    ::memset(&m_processContext, 0, sizeof(Vst::ProcessContext));
}

IMPLEMENT_FUNKNOWN_METHODS (VST3PluginHost::AttributeList, IAttributeList, IAttributeList::iid)

IMPLEMENT_FUNKNOWN_METHODS (VST3PluginHost::Message, IMessage, IMessage::iid)

#endif // VST3_SUPPORT