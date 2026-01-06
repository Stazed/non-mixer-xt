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
 * File:   EditorFrame.H
 * Author: sspresto
 *
 * Created on January 4, 2024, 6:01 PM
 */

#pragma once

#ifdef VST3_SUPPORT

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
#include <atomic>
#include <thread>
#include "../x11/X11PluginUI.H"
#include "VST3_Plugin.H"

class ARunLoop : public IRunLoop
{
public:

    ARunLoop(VST3_Plugin *plug) : m_plugin(plug) {}
    virtual ~ARunLoop() = default; // avoid -Wdelete-non-virtual-dtor

    void beginDestruction();

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

    uint32 PLUGIN_API addRef() override;
    uint32 PLUGIN_API release() override;

private:

    VST3_Plugin * m_plugin;
    std::atomic<uint32> m_refCount{1};
    std::atomic<bool>   m_destroying { false };
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
        : X11PluginUI(this, resizeable, false, true), m_plugin(plug), m_plugView(plugView),
          m_runLoop(nullptr), m_resizing(false)
    {
        m_runLoop = owned(NEW ARunLoop(m_plugin));

        m_plugView->setFrame(this);

        ViewRect rect;
        m_resizing = true;

        /* getSize will get current or old size */
        if (m_plugView->getSize(&rect) == kResultOk)
        {
            /* checkSizeConstraint may alter the rect sizes here */
            if (m_plugView->checkSizeConstraint(&rect) == kResultOk)
            {
                const int32 x_W = rect.right - rect.left;
                const int32 x_H = rect.bottom - rect.top;
                DMESSAGE("Width = %d: Height = %d", x_W, x_H );

                Size x_size = getSize();
                if( (x_size.width != x_W) || x_size.height != x_H )
                {
                    setMinimumSize(x_W, x_H);
                    setSize(x_W, x_H, false, false);
                }
            }
            else    // not altered
            {
                setSize(rect.right - rect.left, rect.bottom - rect.top, false, false);
            }
        }

        m_resizing = false;
    }

    // Destructor.
    virtual ~EditorFrame ()
    {
        beginDestruction();
    }

    // Accessors.
    IPlugView *plugView () const
    {
        return m_plugView;
    }
    ARunLoop *runLoop () const
    {
        return m_runLoop;
    }

    //--- IPlugFrame ---
    //
    tresult PLUGIN_API resizeView (IPlugView *plugView, ViewRect *rect) override
    {
        if (!rect || !plugView || plugView != m_plugView)
            return kInvalidArgument;

        if (m_resizing)
            return kResultFalse;

        m_resizing = true;

        /* checkSizeConstraint may alter the rect sizes here */
        if (m_plugView->checkSizeConstraint(rect) == kResultOk)
        {
            const int32 width = rect->right - rect->left;
            const int32 height = rect->bottom - rect->top;
            DMESSAGE("Constraint Width = %d: Height = %d", width, height );

            Size x_size = getSize();
            if( (x_size.width != width) || (x_size.height != height) )
            {
                setMinimumSize(width, height);
                setSize(width, height, false, false);
            }

            m_plugView->onSize(rect);
        }
        else    // If no checkSizeConstraint then just use the unaltered rect
        {
            const int32 width = rect->right - rect->left;
            const int32 height = rect->bottom - rect->top;
            DMESSAGE("Width = %d: Height = %d", width, height );
            Size x_size = getSize();
            if( (x_size.width != width) || (x_size.height != height) )
            {
                setSize(width, height, false, false);
            }

            m_plugView->onSize(rect);
        }

        m_resizing = false;
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

    uint32 PLUGIN_API addRef() override;
    uint32 PLUGIN_API release() override;

private:

    // Instance members.
    VST3_Plugin * m_plugin;
    IPlugView *m_plugView;
    IPtr<ARunLoop> m_runLoop;
    bool m_resizing;
    std::atomic<uint32> m_refCount{1};
    std::atomic<bool>   m_destroying { false };
    std::atomic<int>    m_callbackDepth { 0 };

    void beginDestruction();

protected:
    void handlePluginUIClosed() override;
    void handlePluginUIResized(const uint width, const uint height) override;
};


#endif  // VST3_SUPPORT