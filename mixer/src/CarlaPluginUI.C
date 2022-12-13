/*
 * Carla Plugin UI
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

#include "CarlaPluginUI.H"
#include "debug.h"

#ifdef HAVE_X11
# include <pthread.h>
# include <sys/types.h>
# include <X11/Xatom.h>
# include <X11/Xlib.h>
# include <X11/Xutil.h>
# include "CarlaPluginUI_X11Icon.H"
#endif


// ---------------------------------------------------------------------------------------------------------------------
// X11

#ifdef HAVE_X11
typedef void (*EventProcPtr)(XEvent* ev);

static const uint X11Key_Escape = 9;
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

class X11PluginUI : public CarlaPluginUI
{
public:
    X11PluginUI(Callback* const cb, const uintptr_t parentId,
                const bool isStandalone, const bool isResizable, const bool canMonitorChildren) noexcept
        : CarlaPluginUI(cb, isStandalone, isResizable),
          fDisplay(nullptr),
          fHostWindow(0),
          fChildWindow(0),
          fChildWindowConfigured(false),
          fChildWindowMonitoring(isResizable || canMonitorChildren),
          fIsVisible(false),
          fFirstShow(true),
          fSetSizeCalledAtLeastOnce(false),
          fEventProc(nullptr)
     {
        fDisplay = XOpenDisplay(nullptr);
        CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);

        const int screen = DefaultScreen(fDisplay);

        XSetWindowAttributes attr;
        carla_zeroStruct(attr);

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

        XGrabKey(fDisplay, X11Key_Escape, AnyModifier, fHostWindow, 1, GrabModeAsync, GrabModeAsync);

        Atom wmDelete = XInternAtom(fDisplay, "WM_DELETE_WINDOW", True);
        XSetWMProtocols(fDisplay, fHostWindow, &wmDelete, 1);

        const pid_t pid = getpid();
        const Atom _nwp = XInternAtom(fDisplay, "_NET_WM_PID", False);
        XChangeProperty(fDisplay, fHostWindow, _nwp, XA_CARDINAL, 32, PropModeReplace, (const uchar*)&pid, 1);

        const Atom _nwi = XInternAtom(fDisplay, "_NET_WM_ICON", False);
        XChangeProperty(fDisplay, fHostWindow, _nwi, XA_CARDINAL, 32, PropModeReplace, (const uchar*)sCarlaX11Icon, sCarlaX11IconSize);

        const Atom _wt = XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE", False);

        // Setting the window to both dialog and normal will produce a decorated floating dialog
        // Order is important: DIALOG needs to come before NORMAL
        const Atom _wts[2] = {
            XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE_DIALOG", False),
            XInternAtom(fDisplay, "_NET_WM_WINDOW_TYPE_NORMAL", False)
        };
        XChangeProperty(fDisplay, fHostWindow, _wt, XA_ATOM, 32, PropModeReplace, (const uchar*)&_wts, 2);

        if (parentId != 0)
            setTransientWinId(parentId);
    }

    ~X11PluginUI() override
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

    void show() override
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

                    XWindowAttributes attrs;
                    carla_zeroStruct(attrs);

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
                        XSizeHints sizeHints;
                        carla_zeroStruct(sizeHints);

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

    void hide() override
    {
        CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

        fIsVisible = false;
        XUnmapWindow(fDisplay, fHostWindow);
        XFlush(fDisplay);
    }

    void idle() override
    {
        // prevent recursion
        if (fIsIdling) return;

        int nextWidth = 0;
        int nextHeight = 0;

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

                    if (event.xconfigure.window == fHostWindow)
                    {
                        const uint width  = static_cast<uint>(event.xconfigure.width);
                        const uint height = static_cast<uint>(event.xconfigure.height);

                        if (fChildWindow != 0)
                        {
                            if (! fChildWindowConfigured)
                            {
                                pthread_mutex_lock(&gErrorMutex);
                                const XErrorHandler oldErrorHandler = XSetErrorHandler(temporaryErrorHandler);
                                gErrorTriggered = false;

                                XSizeHints sizeHints;
                                carla_zeroStruct(sizeHints);

                                if (XGetNormalHints(fDisplay, fChildWindow, &sizeHints) && !gErrorTriggered)
                                {
                                    XSetNormalHints(fDisplay, fHostWindow, &sizeHints);
                                }
                                else
                                {
                                    WARNING("Caught errors while accessing child window");
                                    fChildWindow = 0;
                                }

                                fChildWindowConfigured = true;
                                XSetErrorHandler(oldErrorHandler);
                                pthread_mutex_unlock(&gErrorMutex);
                            }

                            if (fChildWindow != 0)
                                XResizeWindow(fDisplay, fChildWindow, width, height);
                        }

                        fCallback->handlePluginUIResized(width, height);
                    }
                    else if (fChildWindowMonitoring && event.xconfigure.window == fChildWindow && fChildWindow != 0)
                    {
                        nextWidth = event.xconfigure.width;
                        nextHeight = event.xconfigure.height;
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
                    carla_zeroStruct(wa);

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

        if (nextWidth != 0 && nextHeight != 0 && fChildWindow != 0)
        {
            XSizeHints sizeHints;
            carla_zeroStruct(sizeHints);

            if (XGetNormalHints(fDisplay, fChildWindow, &sizeHints))
                XSetNormalHints(fDisplay, fHostWindow, &sizeHints);

            XResizeWindow(fDisplay, fHostWindow, static_cast<uint>(nextWidth), static_cast<uint>(nextHeight));
            XFlush(fDisplay);
        }

        fIsIdling = false;
    }

    void focus() override
    {
        CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

        XWindowAttributes wa;
        carla_zeroStruct(wa);

        CARLA_SAFE_ASSERT_RETURN(XGetWindowAttributes(fDisplay, fHostWindow, &wa),);

        if (wa.map_state == IsViewable)
        {
            XRaiseWindow(fDisplay, fHostWindow);
            XSetInputFocus(fDisplay, fHostWindow, RevertToPointerRoot, CurrentTime);
            XSync(fDisplay, False);
        }
    }

    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild) override
    {
        CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

        fSetSizeCalledAtLeastOnce = true;
        XResizeWindow(fDisplay, fHostWindow, width, height);

        if (fChildWindow != 0 && resizeChild)
            XResizeWindow(fDisplay, fChildWindow, width, height);

        if (! fIsResizable)
        {
            XSizeHints sizeHints;
            carla_zeroStruct(sizeHints);

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

    void setTitle(const char* const title) override
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

    void setTransientWinId(const uintptr_t winId) override
    {
        CARLA_SAFE_ASSERT_RETURN(fDisplay != nullptr,);
        CARLA_SAFE_ASSERT_RETURN(fHostWindow != 0,);

        XSetTransientForHint(fDisplay, fHostWindow, static_cast<Window>(winId));
    }

    void setChildWindow(void* const winId) override
    {
        CARLA_SAFE_ASSERT_RETURN(winId != nullptr,);

        fChildWindow = (Window)winId;
    }

    void* getPtr() const noexcept override
    {
        return (void*)fHostWindow;
    }

    void* getDisplay() const noexcept override
    {
        return fDisplay;
    }

protected:
    Window getChildWindow() const
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

private:
    Display* fDisplay;
    Window   fHostWindow;
    Window   fChildWindow;
    bool     fChildWindowConfigured;
    bool     fChildWindowMonitoring;
    bool     fIsVisible;
    bool     fFirstShow;
    bool     fSetSizeCalledAtLeastOnce;
    EventProcPtr fEventProc;

    CARLA_DECLARE_NON_COPYABLE(X11PluginUI)
};
#endif // HAVE_X11

#ifdef HAVE_X11
CarlaPluginUI* CarlaPluginUI::newX11(Callback* const cb,
                                     const uintptr_t parentId,
                                     const bool isStandalone,
                                     const bool isResizable,
                                     const bool isLV2)
{
    return new X11PluginUI(cb, parentId, isStandalone, isResizable, isLV2);
}
#endif



// -----------------------------------------------------
