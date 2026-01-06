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
 * File:   EditorFrame.C
 * Author: sspresto
 *
 * Created on January 4, 2024, 6:01 PM
 */

#ifdef VST3_SUPPORT

#include "EditorFrame.H"
#include "runloop.h"

//------------------------------------------------------------------------

tresult
ARunLoop::registerEventHandler( IEventHandler* handler,
    FileDescriptor fd )
{
    DMESSAGE ( "HAVE REGISTER FROM PLUGIN: fd == %d: PLUGIN PTR = %p", fd, m_plugin );

    if (handler == nullptr)
        return kInvalidArgument;

    if (m_destroying.load(std::memory_order_acquire))
        return kResultFalse;

    // NOTE: Some plugins destroy their handler objects without unregistering.
    // Capturing an IPtr here can crash later when callbacks are cleared.
    if ( eventHandlers.find ( fd ) != eventHandlers.end ( ) )
    {
        DMESSAGE("ALREADY REGISTERED!: FD = %d: eventHandlers.end", fd);
        return kResultTrue;
    }

    // Store a strong ref for host-side bookkeeping / equality checks.
    // (If this still proves problematic for BYOD, switch the map to raw pointers too.)
    EventHandler handlerRef(handler);
    IEventHandler* rawHandler = handler;

    m_plugin->get_runloop ( )->registerFileDescriptor (
        fd, [this, rawHandler] (int fd)
    {
        if (m_destroying.load(std::memory_order_acquire))
            return;
        if (rawHandler)
            rawHandler->onFDIsSet ( fd );
    } );

    eventHandlers.emplace ( fd, handlerRef );
    return kResultTrue;
}

//------------------------------------------------------------------------

tresult
ARunLoop::unregisterEventHandler( IEventHandler* handler )
{
    if ( !handler )
        return kInvalidArgument;

    if (m_destroying.load(std::memory_order_acquire))
        return kResultFalse;

    auto it = std::find_if ( eventHandlers.begin ( ), eventHandlers.end ( ),
        [&] (const auto& elem )
    {
        return elem.second == handler;
    } );
    if ( it == eventHandlers.end ( ) )
        return kResultFalse;

    m_plugin->get_runloop ( )->unregisterFileDescriptor ( it->first );

    eventHandlers.erase ( it );
    return kResultTrue;
}

//------------------------------------------------------------------------

tresult
ARunLoop::registerTimer( ITimerHandler* handler,
    TimerInterval milliseconds )
{
    if ( !handler || milliseconds == 0 )
        return kInvalidArgument;

    if (m_destroying.load(std::memory_order_acquire))
        return kResultFalse;

    // Check if already registered. This does not matter for
    // the unordered map, but does matter for the runloop which
    // is a vector.
    for (const auto& pair : timerHandlers)
    {
        if(pair.second == handler)
        {
            DMESSAGE("Got duplicate TIMER");
            return kResultTrue;
        }
    }

    // No duplicates so add it to the runloop vector
    DMESSAGE("REGISTER TIMER EditorFrame %p", handler);

    // IMPORTANT:
    // handler is plugin-provided. Do NOT use owned(handler) because owned() does not AddRef.
    TimerHandler handlerRef(handler); // AddRef (normal IPtr behavior)

    auto id = m_plugin->get_runloop ( )->registerTimer (
        milliseconds, [this, handlerRef] ( auto )
    {
        if (m_destroying.load(std::memory_order_acquire))
            return;
        if (handlerRef)
            handlerRef->onTimer ( );
    } );

    // Add to EditorFrame unordered pair
    timerHandlers.emplace ( id, handlerRef );
    DMESSAGE("timerHandles size = %d", timerHandlers.size());

    return kResultTrue;
}

//------------------------------------------------------------------------

tresult
ARunLoop::unregisterTimer( ITimerHandler* handler )
{
    if ( !handler )
        return kInvalidArgument;

    if (m_destroying.load(std::memory_order_acquire))
        return kResultFalse;

    auto it = std::find_if ( timerHandlers.begin ( ), timerHandlers.end ( ),
        [&] (const auto& elem )
    {
        return elem.second == handler;
    } );
    if ( it == timerHandlers.end ( ) )
        return kResultFalse;

    DMESSAGE("UN_REGISTER TIMER EditorFrame = %p", handler);
    m_plugin->get_runloop ( )->unregisterTimer ( it->first );

    timerHandlers.erase ( it );
    return kResultTrue;
}

uint32 ARunLoop::addRef()
{
    return ++m_refCount;
}

uint32 ARunLoop::release()
{
    uint32 r = --m_refCount;
    if (r == 0)
        delete this;
    return r;
}

void ARunLoop::beginDestruction()
{
    m_destroying.store(true, std::memory_order_release);

    for (auto& it : eventHandlers)
        m_plugin->get_runloop()->unregisterFileDescriptor(it.first);

    for (auto& it : timerHandlers)
        m_plugin->get_runloop()->unregisterTimer(it.first);

    eventHandlers.clear();
    timerHandlers.clear();
}

uint32 EditorFrame::addRef()
{
    return ++m_refCount;
}

uint32 EditorFrame::release()
{
    uint32 r = --m_refCount;
    if (r == 0)
        delete this;
    return r;
}

void EditorFrame::beginDestruction()
{
    m_destroying.store(true, std::memory_order_release);

    // IMPORTANT:
    // Unregister timers/FD handlers BEFORE notifying the plugin view that the frame is removed.
    // Some plugins destroy their handler objects in IPlugView::removed() without unregistering,
    // and will crash later when we clear callbacks (lambda destroys captured IPtr -> release()).
    if (m_runLoop)
        m_runLoop->beginDestruction();

    if (m_plugView)
    {
        m_plugView->removed();
        m_plugView->setFrame(nullptr);
    }

    while (m_callbackDepth.load(std::memory_order_acquire) > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(100));

    m_runLoop = nullptr;
}

void
EditorFrame::handlePluginUIClosed( )
{
    if (m_destroying.load())
        return;

    m_callbackDepth.fetch_add(1);
    m_plugin->set_visibility ( false );
    m_callbackDepth.fetch_sub(1);
}

void
EditorFrame::handlePluginUIResized( const uint width, const uint height )
{
    // DMESSAGE("Handle Resized W = %d: H = %d", width, height);
    if (m_destroying.load())
        return;

    m_callbackDepth.fetch_add(1);

    ViewRect rect0;
    if ( m_plugView->getSize ( &rect0 ) != kResultOk )
    {
        m_callbackDepth.fetch_sub(1);
        return;
    }

    Size a_size = getSize ( );

    //   DMESSAGE("rect0 W = %d: H = %d", rect0.getHeight(), rect0.getHeight());
    //   DMESSAGE("a_size W = %d: H = %d", a_size.width, a_size.height);

    if ( rect0.getWidth ( ) != a_size.width || rect0.getHeight ( ) != a_size.height )
    {
        rect0.right = rect0.left + width;
        rect0.bottom = rect0.top + height;
        m_plugView->onSize ( &rect0 );
    }

    m_callbackDepth.fetch_sub(1);
}

#endif  // VST3_SUPPORT