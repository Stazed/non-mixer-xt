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
 * File:   EditorFrame.h
 * Author: sspresto
 *
 * Created on January 4, 2024, 6:01 PM
 */

#pragma once

#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/base/ftypes.h"

#include <unordered_map>
#include "../x11/X11PluginUI.H"
#include "VST3_Plugin.H"

class RunLoop : public IRunLoop
{
public:
    
    RunLoop() {}

    //--- IRunLoop ---
    //
    tresult PLUGIN_API registerEventHandler (IEventHandler *handler, FileDescriptor fd) override;
    tresult PLUGIN_API unregisterEventHandler (IEventHandler *handler) override;
    tresult PLUGIN_API registerTimer (ITimerHandler *handler, TimerInterval msecs) override;
    tresult PLUGIN_API unregisterTimer (ITimerHandler *handler) override;

    tresult PLUGIN_API queryInterface (const TUID _iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
                FUnknownPrivate::iidEqual(_iid, IRunLoop::iid))
        {
            addRef();
            *obj = this;
            return kResultOk;
        }

        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef  () override { return 1001; }
    uint32 PLUGIN_API release () override { return 1001; }

private:

//    VST3_Plugin * m_plugin;
    using TimerID = uint64_t;
    using EventHandler = IPtr<Linux::IEventHandler>;
    using TimerHandler = IPtr<Linux::ITimerHandler>;
    using EventHandlers = std::unordered_map<Linux::FileDescriptor, EventHandler>;
    using TimerHandlers = std::unordered_map<TimerID, TimerHandler>;

    EventHandlers eventHandlers;
    TimerHandlers timerHandlers;
};


class EditorFrame : public X11PluginUI, public IPlugFrame,
    private X11PluginUI::Callback
{
public:

    // Constructor.
    EditorFrame (VST3_Plugin * plug, IPlugView *plugView, bool resizeable) 
        : X11PluginUI(this, resizeable, false), m_plugin(plug), m_plugView(plugView),
                m_runLoop(nullptr), m_resizing(false)
    {
        m_runLoop = owned(NEW RunLoop());

        m_plugView->setFrame(this);

        ViewRect rect;
        if (m_plugView->getSize(&rect) == kResultOk)
        {
            m_resizing = true;
            setSize(rect.right - rect.left, rect.bottom - rect.top, false, false);
            m_resizing = false;
        }
    }

    // Destructor.
    virtual ~EditorFrame ()
    {
        m_plugView->setFrame(nullptr);
        m_runLoop = nullptr;
    }
    
    // Accessors.
    IPlugView *plugView () const
            { return m_plugView; }
    RunLoop *runLoop () const
            { return m_runLoop; }

    //--- IPlugFrame ---
    //
    tresult PLUGIN_API resizeView (IPlugView *plugView, ViewRect *rect) override
    {
        if (!rect || !plugView || plugView != m_plugView)
                return kInvalidArgument;

        if (m_resizing)
            return kResultFalse;

        m_resizing = true;

        int width = rect->right  - rect->left;
        int height = rect->bottom - rect->top;
        DMESSAGE("EditorFrame[%p]::resizeView(%p, %p) size=(%d, %d)",
                this, plugView, rect, width, height);

      //  if (m_plugView->canResize() == kResultOk)
        setSize(width, height, false, false);

        m_resizing = false;

        ViewRect rect0;
        if (m_plugView->getSize(&rect0) != kResultOk)
            return kInternalError;

        int width0 = rect0.right  - rect0.left;
        int height0 = rect0.bottom - rect0.top;

        if ( (width0 != width) && (height0 != height) )
            m_plugView->onSize(&rect0);

        return kResultOk;
    }

    tresult PLUGIN_API queryInterface (const TUID _iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
                FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid))
        {
            addRef();
            *obj = this;
            return kResultOk;
        }

        return m_runLoop->queryInterface(_iid, obj);
    }

    uint32 PLUGIN_API addRef  () override { return 1002; }
    uint32 PLUGIN_API release () override { return 1002; }

private:

    // Instance members.
    VST3_Plugin * m_plugin;
    IPlugView *m_plugView;
    IPtr<RunLoop> m_runLoop;
    bool m_resizing;
    
protected:
    void handlePluginUIClosed() override;
    void handlePluginUIResized(const uint width, const uint height) override;
};


