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

    if ( eventHandlers.find ( fd ) != eventHandlers.end ( ) )
    {
        DMESSAGE("ALREADY REGISTERED!: FD = %d: eventHandlers.end", fd);
        return kResultTrue;
    }

    m_plugin->get_runloop ( )->registerFileDescriptor (
        fd, [handler] (int fd)
    {
        handler->onFDIsSet ( fd );
    } );

    eventHandlers.emplace ( fd, handler );
    return kResultTrue;
}

//------------------------------------------------------------------------

tresult
ARunLoop::unregisterEventHandler( IEventHandler* handler )
{
    if ( !handler )
        return kInvalidArgument;

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

    auto id = m_plugin->get_runloop ( )->registerTimer (
        milliseconds, [handler] ( auto )
    {
        handler->onTimer ( );
    } );

    // Add to EditorFrame unordered pair
    timerHandlers.emplace ( id, handler );
    DMESSAGE("timerHandles size = %d", timerHandlers.size());

    return kResultTrue;
}

//------------------------------------------------------------------------

tresult
ARunLoop::unregisterTimer( ITimerHandler* handler )
{
    if ( !handler )
        return kInvalidArgument;

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

void
EditorFrame::handlePluginUIClosed( )
{
    m_plugin->set_visibility ( false );
}

void
EditorFrame::handlePluginUIResized( const uint width, const uint height )
{
    // DMESSAGE("Handle Resized W = %d: H = %d", width, height);

    ViewRect rect0;
    if ( m_plugView->getSize ( &rect0 ) != kResultOk )
        return;

    Size a_size = getSize ( );

    //   DMESSAGE("rect0 W = %d: H = %d", rect0.getHeight(), rect0.getHeight());
    //   DMESSAGE("a_size W = %d: H = %d", a_size.width, a_size.height);

    if ( rect0.getWidth ( ) != a_size.width || rect0.getHeight ( ) != a_size.height )
    {
        rect0.right = rect0.left + width;
        rect0.bottom = rect0.top + height;
        m_plugView->onSize ( &rect0 );
    }
}

#endif  // VST3_SUPPORT