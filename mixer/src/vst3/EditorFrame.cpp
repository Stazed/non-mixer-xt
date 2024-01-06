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
 * File:   EditorFrame.cpp
 * Author: sspresto
 * 
 * Created on January 4, 2024, 6:01 PM
 */

#include "EditorFrame.h"
#include "runloop.h"

//------------------------------------------------------------------------
inline tresult ARunLoop::registerEventHandler (IEventHandler* handler,
                                                             FileDescriptor fd)
{
    DMESSAGE("HAVE REGISTER FROM PLUGIN");
    if (!handler || eventHandlers.find (fd) != eventHandlers.end ())
            return kInvalidArgument;

    m_plugin->get_runloop()->registerFileDescriptor (
        fd, [handler] (int fd) { handler->onFDIsSet (fd); });

    eventHandlers.emplace (fd, handler);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult ARunLoop::unregisterEventHandler (IEventHandler* handler)
{
    if (!handler)
        return kInvalidArgument;

    auto it = std::find_if (eventHandlers.begin (), eventHandlers.end (),
            [&] (const auto& elem) { return elem.second == handler; });
    if (it == eventHandlers.end ())
        return kResultFalse;

    m_plugin->get_runloop()->unregisterFileDescriptor (it->first);

    eventHandlers.erase (it);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult ARunLoop::registerTimer (ITimerHandler* handler,
                                                      TimerInterval milliseconds)
{
    if (!handler || milliseconds == 0)
        return kInvalidArgument;

    auto id = m_plugin->get_runloop()->registerTimer (
        milliseconds, [handler] (auto) { handler->onTimer (); });

    timerHandlers.emplace (id, handler);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult ARunLoop::unregisterTimer (ITimerHandler* handler)
{
    if (!handler)
        return kInvalidArgument;

    auto it = std::find_if (timerHandlers.begin (), timerHandlers.end (),
            [&] (const auto& elem) { return elem.second == handler; });
    if (it == timerHandlers.end ())
        return kResultFalse;

    m_plugin->get_runloop()->unregisterTimer (it->first);

    timerHandlers.erase (it);
    return kResultTrue;
}


void EditorFrame::handlePluginUIClosed()
{
    m_plugin->set_visibility(false);
}

void EditorFrame::handlePluginUIResized(const uint width, const uint height)
{
   // DMESSAGE("Handle Resized W = %d: H = %d", width, height);

    ViewRect rect0;
    if (m_plugView->getSize(&rect0) != kResultOk)
        return;

    Size a_size = getSize();

 //   DMESSAGE("rect0 W = %d: H = %d", rect0.getHeight(), rect0.getHeight());
 //   DMESSAGE("a_size W = %d: H = %d", a_size.width, a_size.height);
    
    if(rect0.getWidth() != a_size.width || rect0.getHeight() != a_size.height)
    {
        rect0.right = rect0.left + width;
        rect0.bottom = rect0.top + height;
        m_plugView->onSize(&rect0);
    }
}
