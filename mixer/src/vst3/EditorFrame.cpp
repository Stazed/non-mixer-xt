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
inline tresult RunLoop::registerEventHandler (IEventHandler* handler,
                                                             FileDescriptor fd)
{
    if (!handler || eventHandlers.find (fd) != eventHandlers.end ())
            return kInvalidArgument;

    Vst::EditorHost::RunLoop::instance ().registerFileDescriptor (
        fd, [handler] (int fd) { handler->onFDIsSet (fd); });

    eventHandlers.emplace (fd, handler);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult RunLoop::unregisterEventHandler (IEventHandler* handler)
{
    if (!handler)
        return kInvalidArgument;

    auto it = std::find_if (eventHandlers.begin (), eventHandlers.end (),
            [&] (const auto& elem) { return elem.second == handler; });
    if (it == eventHandlers.end ())
        return kResultFalse;

    Vst::EditorHost::RunLoop::instance ().unregisterFileDescriptor (it->first);

    eventHandlers.erase (it);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult RunLoop::registerTimer (ITimerHandler* handler,
                                                      TimerInterval milliseconds)
{
    if (!handler || milliseconds == 0)
        return kInvalidArgument;

    auto id = Vst::EditorHost::RunLoop::instance ().registerTimer (
        milliseconds, [handler] (auto) { handler->onTimer (); });

    timerHandlers.emplace (id, handler);
    return kResultTrue;
}

//------------------------------------------------------------------------
inline tresult RunLoop::unregisterTimer (ITimerHandler* handler)
{
    if (!handler)
        return kInvalidArgument;

    auto it = std::find_if (timerHandlers.begin (), timerHandlers.end (),
            [&] (const auto& elem) { return elem.second == handler; });
    if (it == timerHandlers.end ())
        return kResultFalse;

    Vst::EditorHost::RunLoop::instance ().unregisterTimer (it->first);

    timerHandlers.erase (it);
    return kResultTrue;
}


void EditorFrame::handlePluginUIClosed()
{
  //  Vst::EditorHost::RunLoop::instance().stop();
  //  m_plugin->hide_custom_ui();
    m_plugin->set_visibility(false);
}

void EditorFrame::handlePluginUIResized(const uint /*width*/, const uint /*height*/)
{
   // DMESSAGE("Handle Resized W = %d: H = %d", width, height);

    ViewRect rect0;
    if (m_plugView->getSize(&rect0) != kResultOk)
        return;

    m_plugView->onSize(&rect0);
}
