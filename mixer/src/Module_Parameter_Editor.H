
/*******************************************************************************/
/* Copyright (C) 2008-2021 Jonathan Moore Liles (as "Non-Mixer")               */
/* Copyright (C) 2021- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
/*                                                                             */
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

#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Choice.H>

class Fl_Flowpack;
class Module;
class Fl_Menu_Button;
class Panner;
class Fl_Scroll;
class SpectrumView;

#include <vector>
#include <list>
#include <unordered_map>

#ifdef LV2_SUPPORT
#include "lv2/LV2_Plugin.H"
#endif

#ifdef VST3_SUPPORT
#include "vst3/VST3_Plugin.H"
#endif

class Module_Parameter_Editor : public Fl_Double_Window
{
    Module *_module;

    struct callback_data
    {
        Module_Parameter_Editor *base_widget;

        int port_number[3];

        callback_data ( Module_Parameter_Editor *base_widget, int port_number )
        {
            this->base_widget = base_widget;
            this->port_number[0] = port_number;
            this->port_number[1] = -1;
            this->port_number[2] = -1;
        }

        callback_data ( Module_Parameter_Editor *base_widget, int port_number1, int port_number2 )
        {
            this->base_widget = base_widget;
            this->port_number[0] = port_number1;
            this->port_number[1] = port_number2;
            this->port_number[2] = -1;
        }

        callback_data ( Module_Parameter_Editor *base_widget, int port_number1, int port_number2, int port_number3 )
        {
            this->base_widget = base_widget;
            this->port_number[0] = port_number1;
            this->port_number[1] = port_number2;
            this->port_number[2] = port_number3;
        }

    };

    void update_control_visibility ( bool b_resize = false );
    static void cb_button_handle ( Fl_Widget *w, void *v );
    static void cb_value_handle ( Fl_Widget *w, void *v );
    static void cb_panner_value_handle ( Fl_Widget *w, void *v );
    static void cb_mode_handle ( Fl_Widget *w, void *v );
#if defined LV2_SUPPORT || defined VST2_SUPPORT || defined VST3_SUPPORT
    static void cb_enumeration_handle ( Fl_Widget *w, void *v );
    static void cb_preset_handle ( Fl_Widget *w, void *v );
    void set_preset_controls(int choice);
#endif
#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
    static void cb_save_state_handle ( Fl_Widget *, void *v );
    void save_plugin_state(const std::string &filename);
    static void cb_restore_state_handle ( Fl_Widget *, void *v );
    void restore_plugin_state(const std::string &directory);
#endif
    static void cb_bound_handle ( Fl_Widget *w, void *v );
#ifdef LV2_SUPPORT
    static void cb_filechooser_handle ( Fl_Widget *w, void *v );
    void set_plugin_file(int port, const std::string &filename);
    void set_choice_value(int port, int menu);
#endif
    void set_value (int i, float value );
    void bind_control ( int i );
    void make_controls ( void );
    void update_spectrum ( void );

    bool is_probably_eq ( void );

    static void menu_cb ( Fl_Widget *w, void *v );
    void menu_cb ( Fl_Menu_ *m );

    SpectrumView *spectrum_view;
    Fl_Scroll *control_scroll;
    Fl_Flowpack *control_pack;
#if defined LV2_SUPPORT || defined VST2_SUPPORT || defined VST3_SUPPORT
    Fl_Choice *_presets_choice_button;
#endif
    bool _resized;
    int _min_width;
    int _selected_control;
    bool _use_scroller;

    int _azimuth_port_number;
    int _elevation_port_number;
    int _radius_port_number;

    std::list<callback_data> _callback_data;
    std::vector<Fl_Widget*> controls_by_port;
    std::vector<Fl_Widget*> atom_port_controller;

    Fl_Menu_Button &menu ( void ) const;
    std::unordered_map<int, int> _mPreset_index;

public:

    void reload ( bool b_resize = false );
    void resize(int,int,int,int);
    void handle_control_changed ( Module::Port *p );
#ifdef LV2_SUPPORT
    void refresh_file_button_label(int index);
#endif
    int handle ( int m );
    explicit Module_Parameter_Editor ( Module *module );
    virtual ~Module_Parameter_Editor ( );
};
