
/*******************************************************************************/
/* Copyright (C) 2009 Jonathan Moore Liles                                     */
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

#include <FL/Fl.H>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <FL/fl_draw.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Counter.H>

#include "../../FL/Fl_Flowpack.H"
#include "../../FL/Fl_Labelpad_Group.H"
#include "../../FL/Fl_Value_SliderX.H"
#include "../../FL/test_press.H"
#include "../../FL/menu_popup.H"
#include "../../nonlib/file.h"
#include "../../nonlib/debug.h"

#include <FL/Fl_Scroll.H>
#include "Module.H"
#include "Module_Parameter_Editor.H"
#include "Controller_Module.H"
#include "Chain.H"
#include "Panner.H"

#ifdef CLAP_SUPPORT
#include "clap/CLAP_Plugin.H"
#endif

#ifdef VST2_SUPPORT
#include "vst2/VST2_Plugin.H"
#endif

#include <FL/fl_ask.H>
#include <FL/Fl_Menu_Button.H>

#include "SpectrumView.H"
#include "string.h"

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
#include <FL/Fl_File_Chooser.H>
#endif

extern char *user_config_dir;

bool
Module_Parameter_Editor::is_probably_eq( void )
{
    if ( _module->_plug_type != Type_LADSPA )
        return false;

    const char *name = _module->label ( );

    return strcasestr ( name, "eq" ) ||
            strcasestr ( name, "filter" ) ||
            strcasestr ( name, "parametric" ) ||
            strcasestr ( name, "band" );
}

Module_Parameter_Editor::Module_Parameter_Editor( Module *module ) :
    Fl_Double_Window( 900, 240 ),
    _module( module ),
    _resized( false ),
    _min_width( 100 ),
    _selected_control( 0 ),
    _use_scroller( false ),
    _azimuth_port_number( -1 ),
    _elevation_port_number( -1 ),
    _radius_port_number( -1 )
{
    char lab[256];
    if ( strcmp ( module->name ( ), module->label ( ) ) )
    {
        snprintf ( lab, sizeof ( lab ), "%s : %s", module->name ( ), module->label ( ) );
    }
    else
        strcpy ( lab, module->label ( ) );

    char title[512];
    snprintf ( title, sizeof ( title ), "%s - %s - %s", "Mixer", module->chain ( )->name ( ), lab );

    copy_label ( title );

    //    fl_font( FL_HELVETICA, 14 );

    _min_width = 30 + fl_width ( module->label ( ) );

    {
        Fl_Group *g = new Fl_Group ( 0, 0, w ( ), 25 );
#ifdef LV2_SUPPORT
        if ( _module->_plug_type == Type_LV2 )
        {
            LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );

            if ( !pm->_PresetList.empty ( ) )
            {
                {
                    Fl_Choice *o = _presets_choice_button = new Fl_Choice ( 5, 0, 200, 24 );
                    for ( unsigned i = 0; i < pm->_PresetList.size ( ); ++i )
                    {
                        o->add ( pm->_PresetList[i].Label.c_str ( ) );
                    }

                    o->label ( "Presets" );
                    o->align ( FL_ALIGN_RIGHT );
                    o->value ( 0 );
                    o->when ( FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED );
                    o->callback ( cb_preset_handle, this );

                    /* The presets that have submenus are indexed by NTK including NULL labels. 
                       We want the index based on valid selection items which does not include
                       NULL labels or FL_SUBMENU. So we create a map with the NTK menu index
                       as the key and the preset_index as incremented for each actual menu item.
                       The cb_preset_handle then uses the NTK menu value which == key to find
                       the preset_index to query the actual LV2 preset. */
                    int preset_index = 0;
                    for ( int key = 0; key < o->size ( ); key++ )
                    {
                        const Fl_Menu_Item &item = o->menu ( )[key]; // get each item

                        std::pair<int, int> prm ( key, preset_index );
                        _mPreset_index.insert ( prm );

                        if ( item.label ( ) && !( ( item.flags & FL_SUBMENU ) ) )
                            preset_index++;

                        DMESSAGE ( "item #%d -- label=%s, value=%s type=%s",
                                key,
                                item.label ( ) ? item.label ( ) : "(Null)", // menu terminators have NULL labels
                                ( item.flags & FL_MENU_VALUE ) ? "set" : "clear", // value of toggle or radio items
                                ( item.flags & FL_SUBMENU ) ? "Submenu" : "Item" ); // see if item is a submenu or actual item
                    }
                }
            }
        }
#endif  // LV2_SUPPORT

#ifdef VST2_SUPPORT
        if ( _module->_plug_type == Type_VST2 )
        {
            VST2_Plugin *pm = static_cast<VST2_Plugin *> ( _module );

            if ( !pm->_PresetList.empty ( ) )
            {
                {
                    Fl_Choice *o = _presets_choice_button = new Fl_Choice ( 5, 0, 200, 24 );
                    for ( unsigned i = 0; i < pm->_PresetList.size ( ); ++i )
                    {
                        std::string temp = pm->_PresetList[i];

                        /* FLTK assumes '/' to be sub-menu, so we have to search the Label and escape it */
                        for ( unsigned ii = 0; ii < temp.size ( ); ++ii )
                        {
                            if ( temp[ii] == '/' )
                            {
                                temp.insert ( ii, "\\" );
                                ++ii;
                                continue;
                            }
                        }

                        o->add ( temp.c_str ( ) );
                    }

                    o->label ( "Presets" );
                    o->align ( FL_ALIGN_RIGHT );
                    o->value ( 0 );
                    o->when ( FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED );
                    o->callback ( cb_preset_handle, this );
                }
            }
        }
#endif  // VST2_SUPPORT

#ifdef VST3_SUPPORT
        if ( _module->_plug_type == Type_VST3 )
        {
            VST3_Plugin *pm = static_cast<VST3_Plugin *> ( _module );

            if ( !pm->_PresetList.empty ( ) )
            {
                {
                    Fl_Choice *o = _presets_choice_button = new Fl_Choice ( 5, 0, 200, 24 );
                    for ( unsigned i = 0; i < pm->_PresetList.size ( ); ++i )
                    {
                        std::string temp = pm->_PresetList[i];

                        /* FLTK assumes '/' to be sub-menu, so we have to search the Label and escape it */
                        for ( unsigned ii = 0; ii < temp.size ( ); ++ii )
                        {
                            if ( temp[ii] == '/' )
                            {
                                temp.insert ( ii, "\\" );
                                ++ii;
                                continue;
                            }
                        }

                        o->add ( temp.c_str ( ) );
                    }

                    o->label ( "Presets" );
                    o->align ( FL_ALIGN_RIGHT );
                    o->value ( 0 );
                    o->when ( FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED );
                    o->callback ( cb_preset_handle, this );
                }
            }
        }
#endif  // VST3_SUPPORT

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
        if ( ( _module->_plug_type == Type_LV2 ) || ( _module->_plug_type == Type_CLAP )
             || _module->_plug_type == Type_VST2 || _module->_plug_type == Type_VST3 )
        {
#ifdef FLTK_SUPPORT
            Fl_Color fc = FL_CYAN;
#else
            Fl_Color fc = fl_color_add_alpha ( FL_CYAN, 200 );
#endif

            {
                Fl_Button *o = new Fl_Button ( 275, 0, 100, 24, "Save State" );
                o->selection_color ( fc );
                o->type ( FL_NORMAL_BUTTON );
                o->align ( FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM );
                o->copy_label ( "Save State" );
                o->callback ( cb_save_state_handle, this );
            }

            {
                Fl_Button *o = new Fl_Button ( 375, 0, 100, 24, "Restore State" );
                o->selection_color ( fc );
                o->type ( FL_NORMAL_BUTTON );
                o->align ( FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM );
                o->copy_label ( "Restore State" );
                o->callback ( cb_restore_state_handle, this );
            }
        }
#endif  // defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)

        g->resizable ( 0 );
        g->end ( );
    } // Fl_Group

    {
        Fl_Group *g = new Fl_Group ( 0, 40, w ( ), h ( ) - 40 );
        {
            Fl_Flowpack *o = control_pack = new Fl_Flowpack ( 50, 40, w ( ) - 100, h ( ) - 40 );
            o->type ( FL_HORIZONTAL );
            o->flow ( true );
            o->vspacing ( 5 );
            o->hspacing ( 5 );

            o->end ( ); // control_pack
        }
        g->resizable ( 0 );
        g->end ( ); // Fl_Group
    }

    end ( );

    make_controls ( );
}

Module_Parameter_Editor::~Module_Parameter_Editor( )
{
}

void
Module_Parameter_Editor::update_spectrum( void )
{
    nframes_t sample_rate = _module->sample_rate ( );

    SpectrumView *o = spectrum_view;

    o->sample_rate ( sample_rate );

    nframes_t nframes = sample_rate / 10;

    float *buf = new float[nframes];

    memset ( buf, 0, sizeof (float ) * nframes );

    buf[0] = 1;

    bool show = false;

    if ( !_module->get_impulse_response ( buf, nframes ) )
        show = is_probably_eq ( );
    else
        show = true;

    o->data ( buf, nframes );

    if ( show && !o->parent ( )->visible ( ) )
    {
        o->parent ( )->show ( );
        update_control_visibility ( );
    }

    o->redraw ( );
}

void
Module_Parameter_Editor::make_controls( void )
{
    Module *module = _module;

    control_pack->clear ( );

    {
        SpectrumView *o = spectrum_view = new SpectrumView ( 25, 40, 360, 300, "Spectrum" );
        o->labelsize ( 14 );
        o->align ( FL_ALIGN_TOP );

        Fl_Labelpad_Group *flg = new Fl_Labelpad_Group ( static_cast<Fl_Widget*> ( o ) );

        flg->hide ( );

        control_pack->add ( flg );
    }

    controls_by_port.clear ( );

    /* these are for detecting related parameter groups which can be
       better represented by a single control */
    _azimuth_port_number = -1;
    float azimuth_value = 0.0f;
    _elevation_port_number = -1;
    float elevation_value = 0.0f;
    _radius_port_number = -1;
    float radius_value = 0.0f;

#ifdef FLTK_SUPPORT
    Fl_Color fc = FL_CYAN;
    Fl_Color bc = FL_BACKGROUND_COLOR;
#else
    Fl_Color fc = fl_color_add_alpha ( FL_CYAN, 200 );
    Fl_Color bc = FL_BACKGROUND_COLOR;
#endif

    controls_by_port.resize ( module->control_input.size ( ) );

    /* FIXME we could probably simplify things and eliminate the flowpack now that we are 
       using the scroller and no longer have different modes. */
    control_pack->vspacing ( 1 );
    control_pack->hspacing ( 10 );
    control_pack->flow ( true );
    control_pack->flowdown ( true );
    control_pack->type ( FL_HORIZONTAL );
    control_pack->size ( 900, 300 );

    _use_scroller = false;
    /* If the parameter number is greater than 12, we use the scroller */
    if ( module->control_input.size ( ) > 12 )
    {
        _use_scroller = true;
        control_scroll = new Fl_Scroll ( 0, 0, 500, 320 );
        control_scroll->type ( 6 ); // Type 6 - vertical scroll only

        control_pack->add ( control_scroll );
    }

    /* Location counter - needed because some of the control_input items created may not
       be visible. We still create them, but hide them. This location counter does not
       increment on hidden items so the proper vertical alignment is kept. */
    unsigned int y_location = 0;

    for ( unsigned int i = 0; i < module->control_input.size ( ); ++i )
    {
        Fl_Widget *w;

        Module::Port *p = &module->control_input[i];

        if ( !strcasecmp ( "Azimuth", p->name ( ) ) &&
             180.0f == p->hints.maximum &&
             -180.0f == p->hints.minimum )
        {
            _azimuth_port_number = i;
            azimuth_value = p->control_value ( );
            continue;
        }
        else if ( !strcasecmp ( "Elevation", p->name ( ) ) &&
                  90.0f == p->hints.maximum &&
                  -90.0f == p->hints.minimum )
        {
            _elevation_port_number = i;
            elevation_value = p->control_value ( );
            continue;
        }
        else if ( !strcasecmp ( "Radius", p->name ( ) ) )
        {
            _radius_port_number = i;
            radius_value = p->control_value ( );
            continue;
        }

        if ( p->hints.type == Module::Port::Hints::BOOLEAN )
        {
            Fl_Button *o = new Fl_Button ( 75, ( y_location * 24 ) + 24, 200, 24, p->name ( ) );
            w = o;
            o->selection_color ( fc );
            o->type ( FL_TOGGLE_BUTTON );
            o->value ( p->control_value ( ) );
            o->align ( FL_ALIGN_RIGHT );
        }
#ifdef LV2_SUPPORT
        else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
        {
            Fl_Choice *o = new Fl_Choice ( 75, ( y_location * 24 ) + 24, 200, 24, p->name ( ) );
            w = o;
            for ( unsigned count = 0; count < module->control_input[i].hints.ScalePoints.size ( ); ++count )
            {
                o->add ( module->control_input[i].hints.ScalePoints[count].Label.c_str ( ) );
            }

            o->align ( FL_ALIGN_RIGHT );

            /* We set the Fl_Choice menu according to the position in the ScalePoints vector */
            int menu = 0;

            for ( unsigned ii = 0; ii < p->hints.ScalePoints.size ( ); ++ii )
            {
                if ( (int) p->hints.ScalePoints[ii].Value == (int) ( p->control_value ( ) + .5 ) ) // .5 for float rounding
                {
                    menu = ii;
                    break;
                }
            }

            o->value ( menu );
            o->selection_color ( fc );
        }
#endif  // LV2_SUPPORT
        else if ( p->hints.type == Module::Port::Hints::INTEGER )
        {
            Fl_Counter *o = new Fl_Counter ( 75, ( y_location * 24 ) + 24, 200, 24, p->name ( ) );
            w = o;

            o->type ( 1 );
            o->step ( 1 );
            o->align ( FL_ALIGN_RIGHT );

            if ( p->hints.ranged )
            {
                o->minimum ( p->hints.minimum );
                o->maximum ( p->hints.maximum );
            }

            o->value ( p->control_value ( ) );
        }
        else
        {
            Fl_Value_SliderX *o = new Fl_Value_SliderX ( 75, ( y_location * 24 ) + 24, 200, 24, p->name ( ) );
            w = o;

            o->type ( FL_HORIZONTAL );
            o->align ( FL_ALIGN_RIGHT );

            if ( p->hints.ranged )
            {
                o->minimum ( p->hints.minimum );
                o->maximum ( p->hints.maximum );
            }

            if ( p->hints.type & Module::Port::Hints::LOGARITHMIC )
                o->log ( true );
#if defined(LV2_SUPPORT) || defined(VST2_SUPPORT)
            if ( p->hints.type == Module::Port::Hints::LV2_INTEGER )
            {
                o->precision ( 0 );
            }
            else // floats
#endif
            {
                o->precision ( 2 );
                /* a couple of plugins have ridiculously small units */
                float r = fabs ( p->hints.maximum - p->hints.minimum );

                if ( r <= 0.01f )
                    o->precision ( 4 );
                else if ( r <= 0.1f )
                    o->precision ( 3 );
                else if ( r <= 100.0f )
                    o->precision ( 2 );
                else if ( r <= 5000.0f )
                    o->precision ( 1 );
                    /* else if ( r <= 10000.0f ) */
                    /*     o->precision( 1 ); */
                else
                    o->precision ( 0 );
            }

            o->textsize ( 8 );
            //                o->box( FL_NO_BOX );
            o->slider ( FL_UP_BOX );
            o->color ( bc );
            o->selection_color ( fc );
            o->value ( p->control_value ( ) );
            o->box ( FL_BORDER_BOX );
        }
        //        w->align(FL_ALIGN_TOP);
        w->labelsize ( 14 );

        controls_by_port[i] = w;

        w->copy_tooltip ( p->osc_path ( ) );

        _callback_data.push_back ( callback_data ( this, i ) );

        if ( p->hints.type == Module::Port::Hints::BOOLEAN )
            w->callback ( cb_button_handle, &_callback_data.back ( ) );
#ifdef LV2_SUPPORT
        else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
            w->callback ( cb_enumeration_handle, &_callback_data.back ( ) );
#endif
        else
            w->callback ( cb_value_handle, &_callback_data.back ( ) );

        /* For ports that are not visible we create the widget and then hide them. This is because
           the controls_by_port and control_input vectors are set the same size. It would get
           really tedious to skip the creation of a widget and then try to keep the control_input
           and controls_by_port aligned. Also we don't increment the y_location counter */
        if ( !p->hints.visible )
        {
            /* Add to control_pack because update_control_visibility() needs a parent widget even if not visible */
            Fl_Labelpad_Group *flg = new Fl_Labelpad_Group ( w );
            control_pack->add ( flg );
            w->hide ( );
            continue;
        }

        if ( _use_scroller )
        {
            control_scroll->add ( w );
        }
        else
        {
            Fl_Labelpad_Group *flg = new Fl_Labelpad_Group ( w );

            flg->set_visible_focus ( );

            control_pack->add ( flg );
        }

        ++y_location;
    }

#ifdef LV2_SUPPORT
    if ( module->_plug_type == Type_LV2 )
    {
        LV2_Plugin *pm = static_cast<LV2_Plugin *> ( module );
        atom_port_controller.clear ( );
        atom_port_controller.resize ( pm->atom_input.size ( ) );

        for ( unsigned int ii = 0; ii < pm->atom_input.size ( ); ++ii )
        {
            Module::Port *p = &pm->atom_input[ii];

            if ( p->hints.type != Module::Port::Hints::PATCH_MESSAGE )
                continue;

            Fl_Widget *w;

            Fl_Button *o = new Fl_Button ( 75, ( y_location * 24 ) + 24, 200, 24, lilv_node_as_string ( p->_lilv_symbol ) );

            w = o;
            o->selection_color ( fc );
            o->type ( FL_NORMAL_BUTTON );
            o->align ( FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM );

            /* Put the filename on the button */
            if ( !p->_file.empty ( ) )
            {
                std::string base_filename = p->_file.substr ( p->_file.find_last_of ( "/\\" ) + 1 );
                o->copy_label ( base_filename.c_str ( ) );
            }

            _callback_data.push_back ( callback_data ( this, ii ) );
            w->callback ( cb_filechooser_handle, &_callback_data.back ( ) );

            atom_port_controller[ii] = w; // so we can update the button label

            /* For ports that are not visible we create the widget and then hide them. This is because
               the atom_port_controller and atom_input vectors are set the same size. It would get
               really tedious to skip the creation of a widget and then try to keep the atom_input
               and atom_port_controller aligned. Also we don't increment the y_location counter */
            if ( !p->hints.visible )
            {
                w->hide ( );
                continue;
            }
            else
            {
                module->_b_have_visible_atom_control_port = true;
            }

            if ( _use_scroller )
            {
                control_scroll->add ( w );
            }
            else
            {
                Fl_Labelpad_Group *flg = new Fl_Labelpad_Group ( w );

                flg->set_visible_focus ( );

                control_pack->add ( flg );
            }

            ++y_location; // increment the scroll widget counter to set for next item if any
        }
    }
#endif  // LV2_SUPPORT

    if ( _azimuth_port_number >= 0 && _elevation_port_number >= 0 )
    {
        Panner *o = new Panner ( 0, 0, 502, 502 );
        o->box ( FL_FLAT_BOX );
        o->color ( FL_GRAY0 );
        o->selection_color ( FL_BACKGROUND_COLOR );
        o->labeltype ( FL_NORMAL_LABEL );
        o->labelfont ( 0 );
        o->labelcolor ( FL_FOREGROUND_COLOR );
        o->align ( FL_ALIGN_TOP );
        o->when ( FL_WHEN_CHANGED );
        o->label ( "Spatialization" );
        o->labelsize ( 14 );

        _callback_data.push_back ( callback_data ( this, _azimuth_port_number, _elevation_port_number, _radius_port_number ) );
        o->callback ( cb_panner_value_handle, &_callback_data.back ( ) );

        o->point ( 0 )->azimuth ( azimuth_value );
        o->point ( 0 )->elevation ( elevation_value );
        if ( _radius_port_number >= 0 )
        {
            o->point ( 0 )->radius_enabled = true;
            o->point ( 0 )->radius ( radius_value );
        }

        Fl_Labelpad_Group *flg = new Fl_Labelpad_Group ( o );

        flg->resizable ( o );
        control_pack->add ( flg );

        controls_by_port[_azimuth_port_number] = o;
        controls_by_port[_elevation_port_number] = o;
        if ( _radius_port_number >= 0 )
            controls_by_port[_radius_port_number] = o;
    }

    update_spectrum ( );

    update_control_visibility ( );
}

#if defined LV2_SUPPORT || defined VST2_SUPPORT || defined VST3_SUPPORT

void
Module_Parameter_Editor::set_preset_controls( int choice )
{
#ifdef LV2_SUPPORT
    if ( _module->_plug_type == Type_LV2 )
    {
        LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );
        pm->update_control_parameters ( choice );
    }
#endif
#ifdef VST2_SUPPORT
    if ( _module->_plug_type == Type_VST2 )
    {
        VST2_Plugin *pm = static_cast<VST2_Plugin *> ( _module );
        pm->setProgram ( choice );
    }
#endif
#ifdef VST3_SUPPORT
    if ( _module->_plug_type == Type_VST3 )
    {
        VST3_Plugin *pm = static_cast<VST3_Plugin *> ( _module );
        pm->setProgram ( choice );
    }
#endif
}
#endif

void
Module_Parameter_Editor::update_control_visibility( bool b_resize )
{
    for ( unsigned int i = 0; i < _module->control_input.size ( ); ++i )
    {
        const Module::Port *p = &_module->control_input[i];

        if ( p->hints.visible )
            controls_by_port[i]->parent ( )->show ( );
        else
            controls_by_port[i]->parent ( )->hide ( );
    }

    control_pack->dolayout ( );

    int width = control_pack->w ( ) + 100; // LADSPA

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)
    if ( ( _module->_plug_type == Type_LV2 ) || ( _module->_plug_type == Type_CLAP ) ||
         ( _module->_plug_type == Type_VST2 ) || ( _module->_plug_type == Type_VST3 ) )
    {
        /* When the scroller is not used, we need to expand width to account for 
           the preset, state save and restore button */
        if ( !_use_scroller )
        {
            width = 485;
        }
    }
#endif

    int height = control_pack->h ( ) + 60;

    if ( width < _min_width )
        width = _min_width;

    control_pack->parent ( )->size ( control_pack->w ( ) + 100, control_pack->h ( ) );

    if ( _use_scroller )
    {
        if ( !b_resize )
            control_scroll->scroll_to ( control_scroll->xposition ( ), control_scroll->yposition ( ) - 17 );
    }

    if ( !b_resize )
    {
        size ( width, height );

        if ( _use_scroller )
        {
            if ( !spectrum_view->parent ( )->visible ( ) )
                size_range ( width, height, 0, 0 ); // allow vertical & horizontal resizing
            else
                size_range ( width, height, width, 0 ); // allow vertical resizing only
        }
        else
            size_range ( width, height, width, height ); // no resizing in no scroller
    }
}

#ifdef LV2_SUPPORT

void
Module_Parameter_Editor::cb_filechooser_handle( Fl_Widget *w, void *v )
{
    callback_data *cd = static_cast<callback_data*> ( v );

    /* Set file chooser location based on previous selected file path */
    LV2_Plugin * pm = static_cast<LV2_Plugin *> ( cd->base_widget->_module );

    std::string previous_file = pm->atom_input[cd->port_number[0]]._file;
    size_t found = previous_file.find_last_of ( "/\\" );
    std::string file_chooser_location = previous_file.substr ( 0, found );

    /* File chooser window title */
    std::string title = lilv_node_as_string ( pm->atom_input[cd->port_number[0]]._lilv_label );

    char *filename;

    filename = fl_file_chooser ( title.c_str ( ), "", file_chooser_location.c_str ( ), 0 );
    if ( filename == NULL )
        return;

    /* Put the file name on the button */
    std::string strfilepath ( filename );
    std::string base_filename = strfilepath.substr ( strfilepath.find_last_of ( "/\\" ) + 1 );

    ( (Fl_Button*) w )->copy_label ( base_filename.c_str ( ) );

    /* Send the file to the plugin */
    cd->base_widget->set_plugin_file ( cd->port_number[0], filename );
}

void
Module_Parameter_Editor::cb_enumeration_handle( Fl_Widget *w, void *v )
{
    callback_data *cd = static_cast<callback_data*> ( v );

    cd->base_widget->set_choice_value ( cd->port_number[0], (int) ( (Fl_Choice*) w )->value ( ) );
}
#endif  // LV2_SUPPORT

void
Module_Parameter_Editor::cb_value_handle( Fl_Widget *w, void *v )
{
    callback_data *cd = static_cast<callback_data*> ( v );

    cd->base_widget->set_value ( cd->port_number[0], ( (Fl_Valuator*) w )->value ( ) );
}

void
Module_Parameter_Editor::cb_button_handle( Fl_Widget *w, void *v )
{
    callback_data *cd = static_cast<callback_data*> ( v );

    cd->base_widget->set_value ( cd->port_number[0], ( (Fl_Button*) w )->value ( ) );
}

void
Module_Parameter_Editor::cb_panner_value_handle( Fl_Widget *w, void *v )
{
    callback_data *cd = static_cast<callback_data*> ( v );

    cd->base_widget->set_value ( cd->port_number[0], ( (Panner*) w )->point ( 0 )->azimuth ( ) );
    cd->base_widget->set_value ( cd->port_number[1], ( (Panner*) w )->point ( 0 )->elevation ( ) );
    cd->base_widget->set_value ( cd->port_number[2], ( (Panner*) w )->point ( 0 )->radius ( ) );

}

void
Module_Parameter_Editor::cb_mode_handle( Fl_Widget *, void *v )
{
    ( (Module_Parameter_Editor*) v )->make_controls ( );
}

#if defined LV2_SUPPORT || defined VST2_SUPPORT || defined VST3_SUPPORT

void
Module_Parameter_Editor::cb_preset_handle( Fl_Widget *w, void *v )
{
    Fl_Choice *m = static_cast<Fl_Choice*> ( w );

    int index = (int) m->value ( ); // VST2 & VST3

    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_LV2 )
    {
        std::unordered_map<int, int> preset_index = ( (Module_Parameter_Editor*) v )->_mPreset_index;

        std::unordered_map<int, int>::const_iterator got
                = preset_index.find ( (int) m->value ( ) );

        if ( got == preset_index.end ( ) )
        {
            return;
        }

        index = got->second;
    }

    ( (Module_Parameter_Editor*) v )->set_preset_controls ( index );
}
#endif

#if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST2_SUPPORT) || defined(VST3_SUPPORT)

void
Module_Parameter_Editor::cb_save_state_handle( Fl_Widget *, void *v )
{
    char *path = read_line ( user_config_dir, "default_path" );

    /* File chooser window title */
    std::string title = "State Save";

    char *filename = NULL;

#ifdef CLAP_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_CLAP )
    {
#define EXT ".state"
        filename = fl_file_chooser ( title.c_str ( ), "(*" EXT")", path, 0 );

        if ( filename == NULL )
            return;

        filename = fl_filename_setext ( filename, EXT );
#undef EXT
    }
#endif  // CLAP_SUPPORT
#ifdef LV2_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_LV2 )
    {
        filename = fl_file_chooser ( title.c_str ( ), "", path, 0 );
    }
#endif
#ifdef VST2_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_VST2 )
    {
#define EXT ".fxp"
        filename = fl_file_chooser ( title.c_str ( ), "(*" EXT")", path, 0 );

        if ( filename == NULL )
            return;

        filename = fl_filename_setext ( filename, EXT );
#undef EXT
    }
#endif  // VST2_SUPPORT
#ifdef VST3_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_VST3 )
    {
#define EXT ".state"
        filename = fl_file_chooser ( title.c_str ( ), "(*" EXT")", path, 0 );

        if ( filename == NULL )
            return;

        filename = fl_filename_setext ( filename, EXT );
#undef EXT
    }
#endif  // VST3_SUPPORT

    if ( path )
        free ( path );

    if ( filename == NULL )
        return;

    /* Save the state to location */
    ( (Module_Parameter_Editor*) v )->save_plugin_state ( filename );
}

void
Module_Parameter_Editor::save_plugin_state( const std::string &filename )
{
    /* Change the filename to a directory */
    std::string directory = filename;
    directory.append ( "/" ); // LV2 only

#ifdef CLAP_SUPPORT
    if ( _module->_plug_type == Type_CLAP )
    {
        CLAP_Plugin *pm = static_cast<CLAP_Plugin *> ( _module );
        pm->save_CLAP_plugin_state ( filename );
    }
#endif
#ifdef LV2_SUPPORT
    if ( _module->_plug_type == Type_LV2 )
    {
        LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );
        pm->save_LV2_plugin_state ( directory );
    }
#endif
#ifdef VST2_SUPPORT
    if ( _module->_plug_type == Type_VST2 )
    {
        VST2_Plugin *pm = static_cast<VST2_Plugin *> ( _module );
        pm->save_VST2_plugin_state ( filename );
    }
#endif
#ifdef VST3_SUPPORT
    if ( _module->_plug_type == Type_VST3 )
    {
        VST3_Plugin *pm = static_cast<VST3_Plugin *> ( _module );
        pm->save_VST3_plugin_state ( filename );
    }
#endif
}

void
Module_Parameter_Editor::cb_restore_state_handle( Fl_Widget *, void *v )
{
    char *path = read_line ( user_config_dir, "default_path" );

    /* File chooser window title */
    std::string title = "State Restore";

    char *directory = NULL; // or file

#ifdef CLAP_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_CLAP )
    {
        directory = fl_file_chooser ( title.c_str ( ), "*.state", path, 0 );
    }
#endif
#ifdef LV2_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_LV2 )
    {
        directory = fl_dir_chooser ( title.c_str ( ), path, 0 );
    }
#endif
#ifdef VST2_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_VST2 )
    {
        directory = fl_file_chooser ( title.c_str ( ), "*.fxp", path, 0 );
    }
#endif
#ifdef VST3_SUPPORT
    if ( ( (Module_Parameter_Editor*) v )->_module->_plug_type == Type_VST3 )
    {
        directory = fl_file_chooser ( title.c_str ( ), "*.state", path, 0 );
    }
#endif

    if ( path )
        free ( path );

    if ( directory == NULL )
        return;

    /* Save the state to location */
    ( (Module_Parameter_Editor*) v )->restore_plugin_state ( directory );
}

void
Module_Parameter_Editor::restore_plugin_state( const std::string &directory )
{
#ifdef CLAP_SUPPORT
    if ( _module->_plug_type == Type_CLAP )
    {
        CLAP_Plugin *pm = static_cast<CLAP_Plugin *> ( _module );
        pm->restore_CLAP_plugin_state ( directory );
    }
#endif
#ifdef LV2_SUPPORT
    if ( _module->_plug_type == Type_LV2 )
    {
        LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );
        pm->restore_LV2_plugin_state ( directory );
    }
#endif
#ifdef VST2_SUPPORT
    if ( _module->_plug_type == Type_VST2 )
    {
        VST2_Plugin *pm = static_cast<VST2_Plugin *> ( _module );
        pm->restore_VST2_plugin_state ( directory );
    }
#endif
#ifdef VST3_SUPPORT
    if ( _module->_plug_type == Type_VST3 )
    {
        VST3_Plugin *pm = static_cast<VST3_Plugin *> ( _module );
        pm->restore_VST3_plugin_state ( directory );
    }
#endif
}
#endif  // #if defined(LV2_SUPPORT) || defined(CLAP_SUPPORT) || defined(VST3_SUPPORT)

void
Module_Parameter_Editor::bind_control( int i )
{
    Module::Port *p = &_module->control_input[i];

    if ( p->connected ( ) )
        /* can only bind once */
        return;

    Controller_Module *o = new Controller_Module ( );
    o->label ( p->name ( ) );
    o->chain ( _module->chain ( ) );
    o->horizontal ( true );
    o->connect_to ( p );

    _module->chain ( )->add_control ( o );
    _module->redraw ( );
}

/* Display changes initiated via automation or from other parts of the GUI */
void
Module_Parameter_Editor::handle_control_changed( Module::Port *p )
{
    int i = _module->control_input_port_index ( p );

    Fl_Widget *w = controls_by_port[i];

    if ( i == _azimuth_port_number ||
         i == _elevation_port_number ||
         i == _radius_port_number )
    {
        Panner *_panner = static_cast<Panner*> ( w );

        if ( i == _azimuth_port_number )
            _panner->point ( 0 )->azimuth ( p->control_value ( ) );
        else if ( i == _elevation_port_number )
            _panner->point ( 0 )->elevation ( p->control_value ( ) );
        else if ( i == _radius_port_number )
            _panner->point ( 0 )->radius ( p->control_value ( ) );

        _panner->redraw ( );

        return;
    }


    if ( p->hints.type == Module::Port::Hints::BOOLEAN )
    {
        Fl_Button *v = static_cast<Fl_Button*> ( w );

        v->value ( p->control_value ( ) );
    }
#ifdef LV2_SUPPORT
    else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
    {
        Fl_Choice *v = static_cast<Fl_Choice*> ( w );

        /* We set the Fl_Choice menu according to the position in the ScalePoints vector */
        int menu = 0;

        for ( unsigned ii = 0; ii < p->hints.ScalePoints.size ( ); ++ii )
        {
            if ( (int) p->hints.ScalePoints[ii].Value == (int) ( p->control_value ( ) + .5 ) ) // .5 for float rounding
            {
                menu = ii;
                break;
            }
        }

        v->value ( menu );
    }
#endif  // LV2_SUPPORT
    else
    {
        Fl_Valuator *v = static_cast<Fl_Valuator*> ( w );

        v->value ( p->control_value ( ) );
    }

    update_spectrum ( );
}

#ifdef LV2_SUPPORT

void
Module_Parameter_Editor::refresh_file_button_label( int index )
{
    LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );

    Module::Port *p = &pm->atom_input[index];
    if ( p->hints.type == Module::Port::Hints::PATCH_MESSAGE && p->hints.visible )
    {
        std::string base_filename = p->_file.substr ( p->_file.find_last_of ( "/\\" ) + 1 );
        Fl_Button *w = static_cast<Fl_Button *> ( atom_port_controller[index] );
        w->copy_label ( base_filename.c_str ( ) );
    }
}
#endif

void
Module_Parameter_Editor::reload( bool b_resize )
{
    //    make_controls();
    update_control_visibility ( b_resize );
    redraw ( );
}

void
Module_Parameter_Editor::resize( int X, int Y, int W, int H )
{
    if ( _use_scroller )
    {
        if ( !spectrum_view->parent ( )->visible ( ) ) // if no spectrum_view, then resize vertical and horizontal
        {
            control_scroll->resize ( 0, 0, W - 60, H - 100 );

            for ( int i = 0; i < control_scroll->children ( ) - 2; ++i ) // -2 skip scrollbars
            {
                Fl_Widget *w = control_scroll->child ( i );

                w->resize ( w->x ( ), w->y ( ), W - 400, w->h ( ) ); // W-400: leave room for scrollbar & label
            }
        }
        else // have spectrum, only allow vertical resize
        {
            control_scroll->resize ( 0, 0, 500, H - 100 );
        }

        reload ( true );
    }

    Fl_Double_Window::resize ( X, Y, W, H );
}

#ifdef LV2_SUPPORT

void
Module_Parameter_Editor::set_plugin_file( int port, const std::string &filename )
{
    LV2_Plugin *pm = static_cast<LV2_Plugin *> ( _module );
    pm->send_file_to_plugin ( port, filename );
}

void
Module_Parameter_Editor::set_choice_value( int port, int menu )
{
    DMESSAGE ( "Menu = %d: ScalePoints Value = %f", menu, _module->control_input[port].hints.ScalePoints[menu].Value );

    /* We have to send the port ScalePoints value not menu choice value */
    set_value ( port, _module->control_input[port].hints.ScalePoints[menu].Value );
}
#endif  // LV2_SUPPORT

void
Module_Parameter_Editor::set_value( int i, float value )
{
    if ( i >= 0 )
    {
        /* Is the port connected to a controller for automation? */
        if ( _module->control_input[i].connected ( ) )
        {
            /* This sets the port value buffer and calls both the Editor and
               Controller_Module - parameter_control_changed() when connected  */
            _module->control_input[i].connected_port ( )->control_value ( value );
        }
        else
        {
            /* This sets the port value buffer and only calls Editor
                parameter_control_changed() when not connected */
            _module->control_input[i].control_value ( value );
        }
    }

    update_spectrum ( );
    //    _module->handle_control_changed( &_module->control_input[i] );
}

void
Module_Parameter_Editor::menu_cb( Fl_Widget *w, void *v )
{
    ( (Module_Parameter_Editor*) v )->menu_cb ( (Fl_Menu_*) w );
}

void
Module_Parameter_Editor::menu_cb( Fl_Menu_* m )
{
    char picked[256];

    if ( !m->mvalue ( ) || m->mvalue ( )->flags & FL_SUBMENU_POINTER || m->mvalue ( )->flags & FL_SUBMENU )
        return;

    strncpy ( picked, m->mvalue ( )->label ( ), sizeof ( picked ) - 1 );

    //    m->item_pathname( picked, sizeof( picked ) );

    DMESSAGE ( "%s", picked );

    if ( !strcmp ( picked, "Bind" ) )
    {
        bind_control ( _selected_control );
    }
}

Fl_Menu_Button &
Module_Parameter_Editor::menu( void ) const
{
    static Fl_Menu_Button m ( 0, 0, 0, 0, "Control" );

    m.clear ( );

    m.add ( "Bind", 0, 0, 0, FL_MENU_RADIO | ( _module->control_input[_selected_control].connected ( ) ? FL_MENU_VALUE : 0 ) );
    //    m.add( "Unbind", 0, &Module::menu_cb, this, 0, FL_MENU_RADIO );

    m.callback ( menu_cb, (void*) this );

    return m;
}

int
Module_Parameter_Editor::handle( int m )
{
    if ( _module->has_name_change ( ) )
    {
        _module->has_name_change ( false );
        for ( unsigned int i = 0; i < controls_by_port.size ( ); i++ )
        {
            Module::Port *p = &_module->control_input[i];
            controls_by_port[i]->copy_tooltip ( p->osc_path ( ) ); // update the OSC path
        }
    }

    switch ( m )
    {
        case FL_PUSH:
            if ( test_press ( FL_BUTTON3 ) )
            {
                for ( unsigned int i = 0; i < controls_by_port.size ( ); i++ )
                {
                    if ( Fl::event_inside ( controls_by_port[i] ) && controls_by_port[i]->visible ( ) )
                    {
                        _selected_control = i;

                        Fl_Menu_Button &mb = menu ( );

                        menu_popup ( &mb, Fl::event_x ( ), Fl::event_y ( ) );

                        return 1;
                    }
                }
                return 0;
            }
            break;

        case FL_KEYBOARD:
        {
            if ( ( Fl::event_key ( FL_Control_L ) || Fl::event_key ( FL_Control_R ) ) && Fl::event_key ( 119 ) )
            {
                // ctrl + w -> close editor
                hide ( );
                return 1;
            }
        }
    }

    return Fl_Group::handle ( m );
}
