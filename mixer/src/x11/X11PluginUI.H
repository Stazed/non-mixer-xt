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
 * File:   X11PluginUI.h
 * Author: sspresto
 *
 * Created on November 20, 2023, 3:08 PM
 */

#pragma once

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT)

typedef void (*EventProcPtr)(XEvent* ev);

class X11PluginUI
{
public:

    class Callback {
    public:
        virtual ~Callback() {}
        virtual void handlePluginUIClosed() = 0;
        virtual void handlePluginUIResized(const uint width, const uint height) = 0;
    };

    X11PluginUI(Callback* const cb, const bool isResizable, const bool canMonitorChildren);

    ~X11PluginUI();

    void show();
    void hide();
    void idle();
    void focus();
    void setMinimumSize(const uint width, const uint height);
    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild);
    void setTitle(const char* const title);
    void setTransientWinId(const uintptr_t winId);
    void setChildWindow(void* const winId);
    void* getPtr() const;
    void* getDisplay() const;

protected:
    Callback* fCallback;
    bool fIsIdling;
    bool fIsResizable;
    void applyHintsFromChildWindow();
    Window getChildWindow() const;

private:
    Display* fDisplay;
    Window   fHostWindow;
    Window   fChildWindow;
    bool     fChildWindowConfigured;
    bool     fChildWindowMonitoring;
    bool     fIsVisible;
    bool     fFirstShow;
    bool     fSetSizeCalledAtLeastOnce;
    uint     fMinimumWidth;
    uint     fMinimumHeight;
    EventProcPtr fEventProc;

};

#endif // defined(LV2_SUPPORT) || defined(CLAP_SUPPORT)