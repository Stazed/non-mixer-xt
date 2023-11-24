/*
 * Copyright (C) 2014-2022 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the doc/GPL.txt file.
 */


/* 
 * File:   X11PluginUI.cpp
 * Author: sspresto
 * 
 * Created on November 20, 2023, 3:08 PM
 */

#include <unistd.h> // getpid()
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <cstdint>
#include "X11PluginUI.H"
#include "XTUtils.H"
#include "NonMixerPluginUI_X11Icon.h"

typedef unsigned char uchar;

static constexpr const uint X11Key_Escape = 9;
static const uint X11Key_W      = 25;

typedef void (*EventProcPtr)(XEvent* ev);

static bool gErrorTriggered = false;
# if defined(__GNUC__) && (__GNUC__ >= 5) && ! defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
# endif
static pthread_mutex_t gErrorMutex = PTHREAD_MUTEX_INITIALIZER;
# if defined(__GNUC__) && (__GNUC__ >= 5) && ! defined(__clang__)
#  pragma GCC diagnostic pop
# endif

static int temporaryErrorHandler(Display*, XErrorEvent*)
{
    gErrorTriggered = true;
    return 0;
}

X11PluginUI::X11PluginUI(Callback* const cb, const bool isResizable, const bool canMonitorChildren):
    fCallback(cb),
    fIsIdling(false),
    fIsResizable(isResizable),
    fDisplay(nullptr),
    fHostWindow(0),
    fChildWindow(0),
    fChildWindowConfigured(false),
    fChildWindowMonitoring(isResizable || canMonitorChildren),
    fIsVisible(false),
    fFirstShow(true),
    fSetSizeCalledAtLeastOnce(false),
    fMinimumWidth(0),
    fMinimumHeight(0),
    fEventProc(nullptr)
 {
    fDisplay = XOpenDisplay(nullptr);
    NON_SAFE_ASSERT_RETURN(fDisplay != nullptr,);

    const int screen = DefaultScreen(fDisplay);

    XSetWindowAttributes attr;
    non_zeroStruct(attr);

    attr.event_mask = KeyPressMask|KeyReleaseMask|FocusChangeMask;

    if (fChildWindowMonitoring)
        attr.event_mask |= StructureNotifyMask|SubstructureNotifyMask;

    fHostWindow = XCreateWindow(fDisplay, RootWindow(fDisplay, screen),
                                0, 0, 300, 300, 0,
                                DefaultDepth(fDisplay, screen),
                                InputOutput,
                                DefaultVisual(fDisplay, screen),
                                CWBorderPixel|CWEventMask, &attr);

    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);
    
//    XSetStandardProperties(fDisplay, fHostWindow, label(), label(), None, NULL, 0, NULL);

    XGrabKey(fDisplay, X11Key_Escape, AnyModifier, fHostWindow, 1, GrabModeAsync, GrabModeAsync);
    XGrabKey(fDisplay, X11Key_W, AnyModifier, fHostWindow, 1, GrabModeAsync, GrabModeAsync);

    Atom wmDelete = XInternAtom(fDisplay, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(fDisplay, fHostWindow, &wmDelete, 1);

    const pid_t pid = getpid();
    const Atom _nwp = XInternAtom(fDisplay, "_NET_WM_PID", False);
    XChangeProperty(fDisplay, fHostWindow, _nwp, XA_CARDINAL, 32, PropModeReplace, (const uchar*)&pid, 1);

    const Atom _nwi = XInternAtom(fDisplay, "_NET_WM_ICON", False);
    XChangeProperty(fDisplay, fHostWindow, _nwi, XA_CARDINAL, 32, PropModeReplace, (const uchar*)sNonMixerX11Icon, sNonMixerX11IconSize);

    const Atom _wt = XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE", False);

    // Setting the window to both dialog and normal will produce a decorated floating dialog
    // Order is important: DIALOG needs to come before NORMAL
    const Atom _wts[2] = {
        XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE_DIALOG", False),
        XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE_NORMAL", False)
    };
    XChangeProperty(fDisplay, fHostWindow, _wt, XA_ATOM, 32, PropModeReplace, (const uchar*)&_wts, 2);

//    if (parentId != 0)
//        setTransientWinId(parentId);
}


X11PluginUI::~X11PluginUI()
{
    CARLA_SAFE_ASSERT(! fIsVisible);

    if (fDisplay == nullptr)
        return;

    if (fIsVisible)
    {
        XUnmapWindow(fDisplay, fHostWindow);
        fIsVisible = false;
    }

    if (fHostWindow != 0)
    {
        XDestroyWindow(fDisplay, fHostWindow);
        fHostWindow = 0;
    }

    XCloseDisplay(fDisplay);
    fDisplay = nullptr;
}


void
X11PluginUI::show()
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    if (fFirstShow)
    {
        if (const Window childWindow = getChildWindow())
        {
            if (! fSetSizeCalledAtLeastOnce)
            {
                int width = 0;
                int height = 0;

                XWindowAttributes attrs = {};

                pthread_mutex_lock(&gErrorMutex);
                const XErrorHandler oldErrorHandler = XSetErrorHandler(temporaryErrorHandler);
                gErrorTriggered = false;

                if (XGetWindowAttributes(fDisplay, childWindow, &attrs))
                {
                    width = attrs.width;
                    height = attrs.height;
                }

                XSetErrorHandler(oldErrorHandler);
                pthread_mutex_unlock(&gErrorMutex);

                if (width == 0 && height == 0)
                {
                    XSizeHints sizeHints = {};

                    if (XGetNormalHints(fDisplay, childWindow, &sizeHints))
                    {
                        if (sizeHints.flags & PSize)
                        {
                            width = sizeHints.width;
                            height = sizeHints.height;
                        }
                        else if (sizeHints.flags & PBaseSize)
                        {
                            width = sizeHints.base_width;
                            height = sizeHints.base_height;
                        }
                    }
                }

                if (width > 1 && height > 1)
                    setSize(static_cast<uint>(width), static_cast<uint>(height), false, false);
            }

            const Atom _xevp = XInternAtom(fDisplay, "_XEventProc", False);

            pthread_mutex_lock(&gErrorMutex);
            const XErrorHandler oldErrorHandler(XSetErrorHandler(temporaryErrorHandler));
            gErrorTriggered = false;

            Atom actualType;
            int actualFormat;
            ulong nitems, bytesAfter;
            uchar* data = nullptr;

            XGetWindowProperty(fDisplay, childWindow, _xevp, 0, 1, False, AnyPropertyType,
                               &actualType, &actualFormat, &nitems, &bytesAfter, &data);

            XSetErrorHandler(oldErrorHandler);
            pthread_mutex_unlock(&gErrorMutex);

            if (nitems == 1 && ! gErrorTriggered)
            {
                fEventProc = *reinterpret_cast<EventProcPtr*>(data);
                XMapRaised(fDisplay, childWindow);
            }
        }
    }

    fIsVisible = true;
    fFirstShow = false;

    XMapRaised(fDisplay, fHostWindow);
    XSync(fDisplay, False);
}

void
X11PluginUI::hide()
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    fIsVisible = false;
    XUnmapWindow(fDisplay, fHostWindow);
    XFlush(fDisplay);
}

void
X11PluginUI::idle()
{
    // prevent recursion
    if (fIsIdling) return;

    uint nextChildWidth = 0;
    uint nextChildHeight = 0;

    uint nextHostWidth = 0;
    uint nextHostHeight = 0;

    fIsIdling = true;

    for (XEvent event; XPending(fDisplay) > 0;)
    {
        XNextEvent(fDisplay, &event);

        if (! fIsVisible)
            continue;

        char* type = nullptr;

        switch (event.type)
        {
        case ConfigureNotify:
                CARLA_SAFE_ASSERT_CONTINUE(fCallback != nullptr);
                CARLA_SAFE_ASSERT_CONTINUE(event.xconfigure.width > 0);
                CARLA_SAFE_ASSERT_CONTINUE(event.xconfigure.height > 0);

                if (event.xconfigure.window == fHostWindow && fHostWindow != 0)
                {
                    nextHostWidth = static_cast<uint>(event.xconfigure.width);
                    nextHostHeight = static_cast<uint>(event.xconfigure.height);
                }
                else if (event.xconfigure.window == fChildWindow && fChildWindow != 0)
                {
                    nextChildWidth = static_cast<uint>(event.xconfigure.width);
                    nextChildHeight = static_cast<uint>(event.xconfigure.height);
                }
                break;

        case ClientMessage:
            type = XGetAtomName(fDisplay, event.xclient.message_type);
            CARLA_SAFE_ASSERT_CONTINUE(type != nullptr);

            if (std::strcmp(type, "WM_PROTOCOLS") == 0)
            {
                fIsVisible = false;
                CARLA_SAFE_ASSERT_CONTINUE(fCallback != nullptr);
                fCallback->handlePluginUIClosed();
            }
            break;

        case KeyRelease:
            if (event.xkey.keycode == X11Key_Escape)
            {
                fIsVisible = false;
                CARLA_SAFE_ASSERT_CONTINUE(fCallback != nullptr);
                fCallback->handlePluginUIClosed();
            }
            break;

        case FocusIn:
            if (fChildWindow == 0)
                fChildWindow = getChildWindow();

            if (fChildWindow != 0)
            {
                XWindowAttributes wa;
                non_zeroStruct(wa);

                if (XGetWindowAttributes(fDisplay, fChildWindow, &wa) && wa.map_state == IsViewable)
                    XSetInputFocus(fDisplay, fChildWindow, RevertToPointerRoot, CurrentTime);
            }
            break;
        }

        if (type != nullptr)
            XFree(type);
        else if (fEventProc != nullptr && event.type != FocusIn && event.type != FocusOut)
            fEventProc(&event);
    }

    if (nextChildWidth != 0 && nextChildHeight != 0 && fChildWindow != 0)
    {
        applyHintsFromChildWindow();
        XResizeWindow(fDisplay, fHostWindow, nextChildWidth, nextChildHeight);
        // XFlush(fDisplay);
    }
    else if (nextHostWidth != 0 && nextHostHeight != 0)
    {
        if (fChildWindow != 0 && ! fChildWindowConfigured)
        {
            applyHintsFromChildWindow();
            fChildWindowConfigured = true;
        }

        if (fChildWindow != 0)
            XResizeWindow(fDisplay, fChildWindow, nextHostWidth, nextHostHeight);

        fCallback->handlePluginUIResized(nextHostWidth, nextHostHeight);
    }

    fIsIdling = false;
}

void
X11PluginUI::focus()
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    XWindowAttributes wa;
    non_zeroStruct(wa);

    CARLA_SAFE_ASSERT_RETURN(XGetWindowAttributes(fDisplay, fHostWindow, &wa),);

    if (wa.map_state == IsViewable)
    {
        XRaiseWindow(fDisplay, fHostWindow);
        XSetInputFocus(fDisplay, fHostWindow, RevertToPointerRoot, CurrentTime);
        XSync(fDisplay, False);
    }
}

void
X11PluginUI::setMinimumSize(const uint width, const uint height)
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    fMinimumWidth = width;
    fMinimumHeight = height;

    XSizeHints sizeHints = {};
    if (XGetNormalHints(fDisplay, fHostWindow, &sizeHints))
    {
        sizeHints.flags     |= PMinSize;
        sizeHints.min_width  = static_cast<int>(width);
        sizeHints.min_height = static_cast<int>(height);
        XSetNormalHints(fDisplay, fHostWindow, &sizeHints);
    }
}

void
X11PluginUI::setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild)
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    fSetSizeCalledAtLeastOnce = true;
    XResizeWindow(fDisplay, fHostWindow, width, height);

    if (fChildWindow != 0 && resizeChild)
        XResizeWindow(fDisplay, fChildWindow, width, height);

    if (! fIsResizable)
    {
        XSizeHints sizeHints = {};
        sizeHints.flags      = PSize|PMinSize|PMaxSize;
        sizeHints.width      = static_cast<int>(width);
        sizeHints.height     = static_cast<int>(height);
        sizeHints.min_width  = static_cast<int>(width);
        sizeHints.min_height = static_cast<int>(height);
        sizeHints.max_width  = static_cast<int>(width);
        sizeHints.max_height = static_cast<int>(height);

        XSetNormalHints(fDisplay, fHostWindow, &sizeHints);
    }

    if (forceUpdate)
        XSync(fDisplay, False);
}

void
X11PluginUI::setTitle(const char* const title)
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    XStoreName(fDisplay, fHostWindow, title);

    const Atom _nwn = XInternAtom(fDisplay, "_NET_WM_NAME", False);
    const Atom utf8 = XInternAtom(fDisplay, "UTF8_STRING", True);

    XChangeProperty(fDisplay, fHostWindow, _nwn, utf8, 8,
                    PropModeReplace,
                    (const uchar*)(title),
                    (int)strlen(title));
}

void
X11PluginUI::setTransientWinId(const uintptr_t winId)
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

    XSetTransientForHint(fDisplay, fHostWindow, static_cast<Window>(winId));
}

void
X11PluginUI::setChildWindow(void* const winId)
{
    CARLA_SAFE_ASSERT_RETURN(winId != nullptr,);

    fChildWindow = (Window)winId;
}

void*
X11PluginUI::getPtr() const
{
    return (void*)fHostWindow;
}

void*
X11PluginUI::getDisplay() const
{
    return fDisplay;
}

void
X11PluginUI::applyHintsFromChildWindow()
{
    pthread_mutex_lock(&gErrorMutex);
    const XErrorHandler oldErrorHandler = XSetErrorHandler(temporaryErrorHandler);
    gErrorTriggered = false;

    XSizeHints sizeHints = {};
    if (XGetNormalHints(fDisplay, fChildWindow, &sizeHints) && !gErrorTriggered)
    {
        if (fMinimumWidth != 0 && fMinimumHeight != 0)
        {
            sizeHints.flags |= PMinSize;
            sizeHints.min_width = fMinimumWidth;
            sizeHints.min_height = fMinimumHeight;
        }

        XSetNormalHints(fDisplay, fHostWindow, &sizeHints);
    }

    if (gErrorTriggered)
    {
        WARNING("Caught errors while accessing child window");
        fChildWindow = 0;
    }

    XSetErrorHandler(oldErrorHandler);
    pthread_mutex_unlock(&gErrorMutex);
}

Window
X11PluginUI::getChildWindow() const
{
    CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr, 0);
    CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0, 0);

    Window rootWindow, parentWindow, ret = 0;
    Window* childWindows = nullptr;
    uint numChildren = 0;

    XQueryTree(fDisplay, fHostWindow, &rootWindow, &parentWindow, &childWindows, &numChildren);

    if (numChildren > 0 && childWindows != nullptr)
    {
        ret = childWindows[0];
        XFree(childWindows);
    }

    return ret;
}
