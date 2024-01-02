
/*******************************************************************************/
/* Copyright (C) 2013 Jonathan Moore Liles                                     */
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
#include "Plugin_Module.H"
#include <vector>

extern const int MAX_PORTS;

class Plugin_Chooser_UI;
class Plugin_Chooser : public Fl_Double_Window
{
    Plugin_Chooser_UI *ui;
    
    std::list <Plugin_Module::Plugin_Info> _plugins;
    
    static void cb_handle ( Fl_Widget *w, void *v );
    void cb_handle ( Fl_Widget *w );
    static void cb_table ( Fl_Widget *w, void *v );
    void cb_table ( Fl_Widget *w );

    void search ( const char *name, const char *author, const char *category,
                    int ninputs, int noutputs, bool favorites, const char *plug_type );

    unsigned int _plugin_type;  // LADSPA, LV2, etc
    std::string _s_unique_id;
    unsigned long _value;
    std::string _plug_path;
    
    int load_favorites ( void );

    void save_favorites ( void );

    void load_categories ( void );
public:

    unsigned int plug_type ( void ) const { return _plugin_type; }
    std::string s_unique_id ( void ) const { return _s_unique_id; }
    unsigned long value ( void ) const { return _value; }
    std::string plug_path ( void ) const { return _plug_path; }
   
    Plugin_Chooser ( int X,int Y,int W,int H, const char *L=0 );

    virtual ~Plugin_Chooser( );

    static Module::Picked plugin_chooser ( int ninputs );
};


