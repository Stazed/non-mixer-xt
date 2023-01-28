
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
#ifdef USE_CMAKE
#include "../../FL/Fl_Flowpack.H"
#include "../../FL/Fl_Labelpad_Group.H"
#include "../../FL/Fl_Value_SliderX.H"
#include "../../FL/test_press.H"
#include "../../FL/menu_popup.H"
#include "../../nonlib/debug.h"
#else
#include "FL/Fl_Flowpack.H"
#include "FL/Fl_Labelpad_Group.H"
#include "FL/Fl_Value_SliderX.H"
#include "FL/test_press.H"
#include "FL/menu_popup.H"
#include "debug.h"
#endif
#include <FL/Fl_Scroll.H>
#include "Module.H"
#include "Module_Parameter_Editor.H"
#include "Controller_Module.H"
#include "Chain.H"
#include "Panner.H"
#include <FL/fl_ask.H>
#include <FL/Fl_Menu_Button.H>

#include "SpectrumView.H"
#include "string.h"

#ifdef LV2_WORKER_SUPPORT
#include <FL/Fl_File_Chooser.H>
#endif

bool
Module_Parameter_Editor::is_probably_eq ( void )
{
    if(_module->_is_lv2)
        return false;

    const char *name = _module->label();

    return strcasestr( name, "eq" ) ||
        strcasestr( name, "filter" ) ||
        strcasestr( name, "parametric" ) ||
        strcasestr( name, "band" );
}

Module_Parameter_Editor::Module_Parameter_Editor ( Module *module ) : Fl_Double_Window( 900,240)
{
    _module = module;
    _resized = false;
    _min_width = 100;
    _use_scroller = false;

    char lab[256];
    if ( strcmp( module->name(), module->label() ) )
    {
        snprintf( lab, sizeof( lab ), "%s : %s", module->name(), module->label() );
    }
    else
        strcpy( lab, module->label() );

    char title[512];
    snprintf( title, sizeof( title ), "%s - %s - %s", "Mixer", module->chain()->name(), lab );

    copy_label( title );

//    fl_font( FL_HELVETICA, 14 );

    _min_width = 30 + fl_width( module->label() );
    
    { Fl_Group *o = new Fl_Group( 0, 0, w(), 25 );

        if (_module->_is_lv2)
        {
#ifdef PRESET_SUPPORT
            LV2_Plugin *pm = static_cast<LV2_Plugin *> (_module);
            
            if( !pm->PresetList.empty() )
            {
                { Fl_Choice *o = LV2_presets_choice = new Fl_Choice( 5, 0, 200, 24 );
                    for(unsigned i = 0; i < pm->PresetList.size(); ++i)
                    {
                        std::string temp = pm->PresetList[i].Label;

                        /* FLTK assumes '/' to be sub-menu, so we have to search the Label and escape it */
                        for (unsigned ii = 0; ii < temp.size(); ++ii)
                        {
                            if ( temp[ii] == '/' )
                            {
                                temp.insert(ii, "\\");
                                ++ii;
                                continue;
                            }
                        }

                        o->add( temp.c_str() );
                    }

                    o->label( "Presets" );
                    o->align(FL_ALIGN_RIGHT);
                    o->value( 0 );
                    o->when( FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED );
                    o->callback( cb_preset_handle,  this );
                }
            }
#endif  // PRESET_SUPPORT

#ifdef LV2_STATE_SAVE
            Fl_Color fc = fl_color_add_alpha( FL_CYAN, 200 );

            { Fl_Button *o = new Fl_Button( 275, 0, 100, 24, "Save State" );
                o->selection_color( fc );
                o->type( FL_NORMAL_BUTTON );
                o->align(FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM);
                o->copy_label( "Save State" );
                o->callback( cb_save_state_handle, this );
            }

            { Fl_Button *o = new Fl_Button( 375, 0, 100, 24, "Restore State" );
                o->selection_color( fc );
                o->type( FL_NORMAL_BUTTON );
                o->align(FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM);
                o->copy_label( "Restore State" );
                o->callback( cb_restore_state_handle, this );
            }
#endif  // LV2_STATE_SAVE
        }   // if (_module->_is_lv2)

        o->resizable(0);
        o->end();
    }   // Fl_Group

    { Fl_Group *o = new Fl_Group( 0, 40, w(), h() - 40 );
        { Fl_Flowpack *o = control_pack = new Fl_Flowpack( 50, 40, w() - 100, h() - 40 );
            o->type( FL_HORIZONTAL );
            o->flow( true );
            o->vspacing( 5 );
            o->hspacing( 5 );

            o->end();   // control_pack
        }
        o->resizable( 0 );
        o->end();       // Fl_Group
    }

    end();

    make_controls();
}

Module_Parameter_Editor::~Module_Parameter_Editor ( )
{
}



void
Module_Parameter_Editor::update_spectrum ( void )
{
    nframes_t sample_rate = _module->sample_rate();

    SpectrumView *o = spectrum_view;

    o->sample_rate( sample_rate );

    nframes_t nframes = sample_rate / 10;

    float *buf = new float[nframes];

    memset( buf, 0, sizeof(float) * nframes );

    buf[0] = 1;

    bool show = false;

    if ( ! _module->get_impulse_response( buf, nframes ) )
        show = is_probably_eq();
    else
        show = true;

    o->data( buf, nframes );

    if ( show && ! o->parent()->visible() )
    {
        o->parent()->show();
        update_control_visibility();
    }

    o->redraw();
}

void
Module_Parameter_Editor::make_controls ( void )
{
    Module *module = _module;

    control_pack->clear();

    { SpectrumView *o = spectrum_view = new SpectrumView( 25, 40, 360, 300, "Spectrum" );
        o->labelsize(14);
        o->align(FL_ALIGN_TOP);

        Fl_Labelpad_Group *flg = new Fl_Labelpad_Group( (Fl_Widget*)o );

        flg->hide();

        control_pack->add( flg );
    }

    controls_by_port.clear();

    /* these are for detecting related parameter groups which can be
       better represented by a single control */
    azimuth_port_number = -1;
    float azimuth_value = 0.0f;
    elevation_port_number = -1;
    float elevation_value = 0.0f;
    radius_port_number = -1;
    float radius_value = 0.0f;
    
    Fl_Color fc = fl_color_add_alpha( FL_CYAN, 200 );
    Fl_Color bc = FL_BACKGROUND_COLOR;

    controls_by_port.resize( module->control_input.size() );
    
    /* FIXME we could probably simplify things and eliminate the flowpack now that we are 
       using the scroller and no longer have different modes. */
    control_pack->vspacing( 1 );
    control_pack->hspacing( 10 );
    control_pack->flow(true);
    control_pack->flowdown(true);
    control_pack->type( FL_HORIZONTAL );
    control_pack->size( 900, 300 );

     _use_scroller = false;
    /* If the parameter number is greater than 12, we use the scroller */
    if(module->control_input.size() > 12)
    {
        _use_scroller = true;
        control_scroll = new Fl_Scroll( 0, 0, 500, 320 );
        control_scroll->type(6);    // Type 6 - vertical scroll only

        control_pack->add( control_scroll );
    }

    /* Location counter - needed because some of the control_input items created may not
       be visible. We still create them, but hide them. This location counter does not
       increment on hidden items so the proper vertical alignment is kept. */
    unsigned int y_location = 0;

    for (unsigned int i = 0; i < module->control_input.size(); ++i )
    {
        Fl_Widget *w;

        Module::Port *p = &module->control_input[i];

        if ( !strcasecmp( "Azimuth", p->name() ) &&
             180.0f == p->hints.maximum &&
             -180.0f == p->hints.minimum )
        {
            azimuth_port_number = i;
            azimuth_value = p->control_value();
            continue;
        }
        else if ( !strcasecmp( "Elevation", p->name() ) &&
                  90.0f == p->hints.maximum &&
                  -90.0f == p->hints.minimum )
        {
            elevation_port_number = i;
            elevation_value = p->control_value();
            continue;
        } 
        else if ( !strcasecmp( "Radius", p->name() ) )
        {
            radius_port_number = i;
            radius_value = p->control_value();
            continue;
        }

        if ( p->hints.type == Module::Port::Hints::BOOLEAN )
        {
            Fl_Button *o = new Fl_Button( 75, (y_location*24) + 24, 200, 24, p->name() );
            w = o;
            o->selection_color( fc );
            o->type( FL_TOGGLE_BUTTON );
            o->value( p->control_value() );
            o->align(FL_ALIGN_RIGHT);
        }
        else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
        {
            Fl_Choice *o =  new Fl_Choice( 75, (y_location*24) + 24, 200, 24, p->name() );
            w = o;
            for(unsigned count = 0; count < module->control_input[i].hints.ScalePoints.size(); ++count)
            {
                o->add( module->control_input[i].hints.ScalePoints[count].Label.c_str() );
            }

            o->align(FL_ALIGN_RIGHT);
            
            /* We set the Fl_Choice menu according to the position in the ScalePoints vector */
            int menu = 0;

            for( unsigned i = 0; i < p->hints.ScalePoints.size(); ++i)
            {
                if ( (int) p->hints.ScalePoints[i].Value == (int) (p->control_value() + .5) )   // .5 for float rounding
                {
                    menu = i;
                    break;
                }
            }
            
            o->value( menu );
            o->selection_color( fc );
        }
        else if ( p->hints.type == Module::Port::Hints::INTEGER )
        {
            Fl_Counter *o = new Fl_Counter(75, (y_location*24) + 24, 200, 24, p->name() );
            w = o;
            
            o->type(1);
            o->step(1);
            o->align(FL_ALIGN_RIGHT);

            if ( p->hints.ranged )
            {
                o->minimum( p->hints.minimum );
                o->maximum( p->hints.maximum );
            }

            o->value( p->control_value() );
        }
        else
        {
            Fl_Value_SliderX *o = new Fl_Value_SliderX( 75, (y_location*24) + 24, 200, 24, p->name() );
            w = o;

            o->type( FL_HORIZONTAL );
            o->align( FL_ALIGN_RIGHT );

            if ( p->hints.ranged )
            {
                o->minimum( p->hints.minimum );
                o->maximum( p->hints.maximum );
            }

            if ( p->hints.type & Module::Port::Hints::LOGARITHMIC )
                o->log(true);

            if ( p->hints.type == Module::Port::Hints::LV2_INTEGER )
            {
                o->precision(0);
            }
            else    // floats
            {
                o->precision( 2 );
                /* a couple of plugins have ridiculously small units */
                float r =  fabs( p->hints.maximum - p->hints.minimum );

                if ( r  <= 0.01f )
                    o->precision( 4 );
                else if ( r <= 0.1f )
                    o->precision( 3 );
                else if ( r <= 100.0f )
                    o->precision( 2 );
                else if ( r <= 5000.0f )
                    o->precision( 1 );
                /* else if ( r <= 10000.0f ) */
                /*     o->precision( 1 ); */
                else
                    o->precision( 0 );
            }

            o->textsize( 8 );
//                o->box( FL_NO_BOX );
            o->slider( FL_UP_BOX );
            o->color( bc );
            o->selection_color( fc );
            o->value( p->control_value() );
            o->box( FL_BORDER_BOX );
        }
//        w->align(FL_ALIGN_TOP);
        w->labelsize( 14 );

        controls_by_port[i] = w;

        w->copy_tooltip( p->osc_path() );

        _callback_data.push_back( callback_data( this, i ) );

        if ( p->hints.type == Module::Port::Hints::BOOLEAN )
            w->callback( cb_button_handle, &_callback_data.back() );
        else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
            w->callback( cb_enumeration_handle, &_callback_data.back() );
        else
            w->callback( cb_value_handle, &_callback_data.back() );

        /* For ports that are not visible we create the widget and then hide them. This is because
           the controls_by_port and control_input vectors are set the same size. It would get
           really tedious to skip the creation of a widget and then try to keep the control_input
           and controls_by_port aligned. Also we don't increment the y_location counter */
        if ( !p->hints.visible )
        {
            /* Add to control_pack because update_control_visibility() needs a parent widget even if not visible */
            Fl_Labelpad_Group *flg = new Fl_Labelpad_Group( w );
            control_pack->add( flg );
            w->hide();
            continue;
        }

        if (_use_scroller)
        {
            control_scroll->add( w );
        }
        else
        {
            Fl_Labelpad_Group *flg = new Fl_Labelpad_Group( w );

            flg->set_visible_focus();

            control_pack->add( flg );
        }

        ++y_location;
    }

#ifdef LV2_WORKER_SUPPORT
    atom_port_controller.clear();
    atom_port_controller.resize( module->atom_input.size() );

    for ( unsigned int ii = 0; ii < module->atom_input.size(); ++ii )
    {
        Module::Port *p = &module->atom_input[ii];

        if ( p->hints.type != Module::Port::Hints::PATCH_MESSAGE )
            continue;

        Fl_Widget *w;

        Fl_Button *o = new Fl_Button( 75, (y_location*24) + 24, 200, 24, lilv_node_as_string(p->_symbol) );

        w = o;
        o->selection_color( fc );
        o->type( FL_NORMAL_BUTTON );
        o->align(FL_ALIGN_INSIDE | FL_ALIGN_BOTTOM);

        /* Put the filename on the button */
        if ( !p->_file.empty() )
        {
            std::string base_filename  = p->_file.substr(p->_file.find_last_of("/\\") + 1);
            o->copy_label( base_filename.c_str() );
        }

        _callback_data.push_back( callback_data( this, ii ) );
        w->callback( cb_filechooser_handle, &_callback_data.back() );

        atom_port_controller[ii] = w;    // so we can update the button label

        /* For ports that are not visible we create the widget and then hide them. This is because
           the atom_port_controller and atom_input vectors are set the same size. It would get
           really tedious to skip the creation of a widget and then try to keep the atom_input
           and atom_port_controller aligned. Also we don't increment the y_location counter */
        if ( !p->hints.visible )
        {
            w->hide();
            continue;
        }

        if (_use_scroller)
        {
            control_scroll->add( w );
        }
        else
        {
            Fl_Labelpad_Group *flg = new Fl_Labelpad_Group( w );

            flg->set_visible_focus();

            control_pack->add( flg );
        }

        ++y_location;   // increment the scroll widget counter to set for next item if any
    }
#endif

    if ( azimuth_port_number >= 0 && elevation_port_number >= 0 )
    {
        Panner *o = new Panner( 0,0, 502,502 );
        o->box(FL_FLAT_BOX);
        o->color(FL_GRAY0);
        o->selection_color(FL_BACKGROUND_COLOR);
        o->labeltype(FL_NORMAL_LABEL);
        o->labelfont(0);
        o->labelcolor(FL_FOREGROUND_COLOR);
        o->align(FL_ALIGN_TOP);
        o->when(FL_WHEN_CHANGED);
        o->label( "Spatialization" );
        o->labelsize( 14 );

        _callback_data.push_back( callback_data( this, azimuth_port_number, elevation_port_number, radius_port_number ) );
        o->callback( cb_panner_value_handle, &_callback_data.back() );

        o->point( 0 )->azimuth( azimuth_value );
        o->point( 0 )->elevation( elevation_value );
        if ( radius_port_number >= 0 )
        {
            o->point( 0 )->radius_enabled = true;
            o->point( 0 )->radius( radius_value );
        }

        Fl_Labelpad_Group *flg = new Fl_Labelpad_Group( o );

        flg->resizable(o);
        control_pack->add( flg );

        controls_by_port[azimuth_port_number] = o;
        controls_by_port[elevation_port_number] = o;
        if ( radius_port_number >= 0 )
            controls_by_port[radius_port_number] = o;
    }

    update_spectrum();

    update_control_visibility();
}

#ifdef PRESET_SUPPORT
void
Module_Parameter_Editor::set_preset_controls(int choice)
{
    LV2_Plugin *pm = static_cast<LV2_Plugin *> (_module);
    pm->update_control_parameters(choice);
}
#endif

void 
Module_Parameter_Editor::update_control_visibility ( void )
{
    for ( unsigned int i = 0; i < _module->control_input.size(); ++i )
    {
        const Module::Port *p = &_module->control_input[i];

        if ( p->hints.visible )
            controls_by_port[i]->parent()->show();
        else
            controls_by_port[i]->parent()->hide();
    }

    control_pack->dolayout();

    int width = control_pack->w() + 100; // LADSPA
    if (_module->_is_lv2)
    {
        /* When the scroller is not used, we need to expand width to account for 
           the preset, state save and restore button */
        if(!_use_scroller)
            width = control_pack->w() + 225;
    }

    int height = control_pack->h() + 60;

    if ( width < _min_width )
        width = _min_width;

    control_pack->parent()->size( control_pack->w() + 100, control_pack->h() );
    
    if(_use_scroller)
    {
        control_scroll->scroll_to(control_scroll->xposition(), control_scroll->yposition() -17);
    }

    size( width, height );
    size_range( width, height, width, height );

}

#ifdef LV2_WORKER_SUPPORT
void
Module_Parameter_Editor::cb_filechooser_handle ( Fl_Widget *w, void *v )
{
    callback_data *cd = (callback_data*)v;

    /* Set file chooser location based on previous selected file path */
    std::string previous_file = cd->base_widget->_module->atom_input[cd->port_number[0]]._file;
    size_t found = previous_file.find_last_of("/\\");
    std::string file_chooser_location = previous_file.substr(0, found);

    /* File chooser window title */
    std::string title = lilv_node_as_string(cd->base_widget->_module->atom_input[cd->port_number[0]]._label);

    char *filename;

    filename = fl_file_chooser(title.c_str(), "", file_chooser_location.c_str(), 0);
    if (filename == NULL)
        return;

    /* Put the file name on the button */
    std::string strfilepath (filename);
    std::string base_filename  = strfilepath.substr(strfilepath.find_last_of("/\\") + 1);

    ((Fl_Button*)w)->copy_label(base_filename.c_str());

    /* Send the file to the plugin */
    cd->base_widget->set_plugin_file( cd->port_number[0], filename );
}
#endif

void
Module_Parameter_Editor::cb_enumeration_handle ( Fl_Widget *w, void *v )
{
    callback_data *cd = (callback_data*)v;
    
    cd->base_widget->set_choice_value( cd->port_number[0], (int) ((Fl_Choice*)w)->value() );
}

void
Module_Parameter_Editor::cb_value_handle ( Fl_Widget *w, void *v )
{
    callback_data *cd = (callback_data*)v;

    cd->base_widget->set_value( cd->port_number[0], ((Fl_Valuator*)w)->value() );
}

void
Module_Parameter_Editor::cb_button_handle ( Fl_Widget *w, void *v )
{
    callback_data *cd = (callback_data*)v;

    cd->base_widget->set_value( cd->port_number[0], ((Fl_Button*)w)->value() );
}


void
Module_Parameter_Editor::cb_panner_value_handle ( Fl_Widget *w, void *v )
{
    callback_data *cd = (callback_data*)v;

    cd->base_widget->set_value( cd->port_number[0], ((Panner*)w)->point( 0 )->azimuth() );
    cd->base_widget->set_value( cd->port_number[1], ((Panner*)w)->point( 0 )->elevation() );
    cd->base_widget->set_value( cd->port_number[2], ((Panner*)w)->point( 0 )->radius() );

}

void
Module_Parameter_Editor::cb_mode_handle ( Fl_Widget *, void *v )
{
    ((Module_Parameter_Editor*)v)->make_controls();
}

#ifdef PRESET_SUPPORT
void
Module_Parameter_Editor::cb_preset_handle ( Fl_Widget *w, void *v )
{
    Fl_Choice *m = (Fl_Choice*)w;
    ((Module_Parameter_Editor*)v)->set_preset_controls( (int) m->value());
}
#endif

#ifdef LV2_STATE_SAVE
void
Module_Parameter_Editor::cb_save_state_handle ( Fl_Widget *, void *v )
{
    /* TODO Set file chooser location based on ... */
    std::string file_chooser_location = "";

    /* File chooser window title */
    std::string title = "LV2 State Save";

    char *filename;

    filename = fl_file_chooser(title.c_str(), "", file_chooser_location.c_str(), 0);
    if (filename == NULL)
        return;

    /* Save the state to location */
    ((Module_Parameter_Editor*)v)->save_plugin_state( filename );
}

void
Module_Parameter_Editor::save_plugin_state(const std::string filename)
{
    /* Change the filename to a directory */
    std::string directory = filename;
    directory.append("/");

    LV2_Plugin *pm = static_cast<LV2_Plugin *> (_module);
    pm->save_LV2_plugin_state(directory);
}

void
Module_Parameter_Editor::cb_restore_state_handle ( Fl_Widget *, void *v )
{
    /* TODO Set file chooser location based on ... */
    std::string file_chooser_location = "";

    /* File chooser window title */
    std::string title = "LV2 State Restore";

    char *directory;

    directory = fl_dir_chooser(title.c_str(), file_chooser_location.c_str(), 0);
    if (directory == NULL)
        return;

    /* Save the state to location */
    ((Module_Parameter_Editor*)v)->restore_plugin_state( directory );
}

void
Module_Parameter_Editor::restore_plugin_state(const std::string directory)
{
    LV2_Plugin *pm = static_cast<LV2_Plugin *> (_module);
    pm->restore_LV2_plugin_state(directory);
}
#endif

void
Module_Parameter_Editor::bind_control ( int i )
{
    Module::Port *p = &_module->control_input[i];

    if ( p->connected() )
        /* can only bind once */
        return;

    Controller_Module *o = new Controller_Module();
    o->label( p->name() );
    o->chain( _module->chain() );
    o->horizontal( true );
    o->connect_to( p );

    _module->chain()->add_control( o );
    _module->redraw();
}

/* Display changes initiated via automation or from other parts of the GUI */
void
Module_Parameter_Editor::handle_control_changed ( Module::Port *p )
{
    int i = _module->control_input_port_index( p );
   
    Fl_Widget *w = controls_by_port[i];

    if ( i == azimuth_port_number ||
         i == elevation_port_number ||
        i == radius_port_number )
    {
        Panner *_panner = (Panner*)w;

        if ( i == azimuth_port_number )
            _panner->point(0)->azimuth( p->control_value() );
        else if ( i == elevation_port_number )
            _panner->point(0)->elevation( p->control_value() );
        else if ( i == radius_port_number )
            _panner->point(0)->radius( p->control_value() );

        _panner->redraw();

        return;
    }


    if ( p->hints.type == Module::Port::Hints::BOOLEAN )
    {
        Fl_Button *v = (Fl_Button*)w;

        v->value( p->control_value() );
    }        
    else if ( p->hints.type == Module::Port::Hints::LV2_INTEGER_ENUMERATION )
    {
        Fl_Choice *v = (Fl_Choice*)w;
        
        /* We set the Fl_Choice menu according to the position in the ScalePoints vector */
        int menu = 0;
        
        for( unsigned i = 0; i < p->hints.ScalePoints.size(); ++i)
        {
            if ( (int) p->hints.ScalePoints[i].Value == (int) (p->control_value() + .5) )   // .5 for float rounding
            {
                menu = i;
                break;
            }
        }

        v->value( menu );
    }
    else
    {
        Fl_Valuator *v = (Fl_Valuator*)w;
    
        v->value( p->control_value() );
    }

    update_spectrum();
}

#ifdef LV2_WORKER_SUPPORT
void
Module_Parameter_Editor::refresh_file_button_label(int index)
{

    Module::Port *p =  &_module->atom_input[index];
    if ( p->hints.type == Module::Port::Hints::PATCH_MESSAGE && p->hints.visible )
    {
        std::string base_filename  = p->_file.substr(p->_file.find_last_of("/\\") + 1);
        Fl_Button *w =  (Fl_Button *) atom_port_controller[index];
        w->copy_label( base_filename.c_str() );
    }
}
#endif

void
Module_Parameter_Editor::reload ( void )
{
//    make_controls();
    update_control_visibility();
    redraw();
}

#ifdef LV2_WORKER_SUPPORT
void
Module_Parameter_Editor::set_plugin_file(int port, const std::string &filename )
{
    LV2_Plugin *pm = static_cast<LV2_Plugin *> (_module);
    pm->send_file_to_plugin(port, filename);
}
#endif

void
Module_Parameter_Editor::set_choice_value(int port, int menu)
{
    DMESSAGE("Menu = %d: ScalePoints Value = %f", menu, _module->control_input[port].hints.ScalePoints[menu].Value);

    /* We have to send the port ScalePoints value not menu choice value */
    set_value( port, _module->control_input[port].hints.ScalePoints[menu].Value );
}

void
Module_Parameter_Editor::set_value (int i, float value )
{
    if ( i >= 0 )
    {
        /* Is the port connected to a controller for automation? */
        if ( _module->control_input[i].connected() )
        {
            /* This sets the port value buffer and calls both the Editor and
               Controller_Module - parameter_control_changed() when connected  */
            _module->control_input[i].connected_port()->control_value( value );
        }
        else
        {
            /* This sets the port value buffer and only calls Editor
                parameter_control_changed() when not connected */
            _module->control_input[i].control_value( value );
        }
    }

    update_spectrum();
//    _module->handle_control_changed( &_module->control_input[i] );
}

void
Module_Parameter_Editor::menu_cb ( Fl_Widget *w, void *v )
{
    ((Module_Parameter_Editor*)v)->menu_cb((Fl_Menu_*)w);
}

void
Module_Parameter_Editor::menu_cb ( Fl_Menu_* m )
{
 char picked[256];

    if ( ! m->mvalue() || m->mvalue()->flags & FL_SUBMENU_POINTER || m->mvalue()->flags & FL_SUBMENU )
        return;

    strncpy( picked, m->mvalue()->label(), sizeof( picked ) -1 );

//    m->item_pathname( picked, sizeof( picked ) );

    DMESSAGE( "%s", picked );

    if ( ! strcmp( picked, "Bind" ) )
    {
        bind_control( _selected_control );
    }
}

Fl_Menu_Button &
Module_Parameter_Editor::menu ( void ) const
{
    static Fl_Menu_Button m( 0, 0, 0, 0, "Control" );

    m.clear();

    m.add( "Bind", 0, 0, 0, FL_MENU_RADIO | (_module->control_input[_selected_control].connected() ? FL_MENU_VALUE : 0 ));
//    m.add( "Unbind", 0, &Module::menu_cb, this, 0, FL_MENU_RADIO );

   m.callback( menu_cb, (void*)this );

    return m;
}

int
Module_Parameter_Editor::handle ( int m )
{
    switch ( m )
    {
        case FL_PUSH:
            if ( test_press( FL_BUTTON3 ) )
            {
                for ( unsigned int i = 0; i < controls_by_port.size(); i++ )
                {
                    if ( Fl::event_inside( controls_by_port[i] ) && controls_by_port[i]->visible() )
                    {
                        _selected_control = i;

                        Fl_Menu_Button &m = menu();
                        
                        menu_popup(&m,Fl::event_x(), Fl::event_y());
                
                        return 1;
                    }
                }
                return 0;
            }
    
    }
    
    return Fl_Group::handle(m);
}
