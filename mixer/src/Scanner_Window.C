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
 * File:   Scanner_Window.C
 * Author: sspresto
 * 
 * Created on July 17, 2024, 10:43 PM
 * 
 */

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Window.H>
#include "../../nonlib/debug.h"
#include "Scanner_Window.H"

// Global cache of all plugins scanned
std::list<Plugin_Info> g_plugin_cache;


#ifdef CLAP_SUPPORT
    #include "clap/Clap_Discovery.H"
#endif

#ifdef VST2_SUPPORT
    #include "vst2/Vst2_Discovery.H"
#endif

#ifdef VST3_SUPPORT
    #include "vst3/Vst3_Discovery.H"
#endif


static Fl_Window * g_scanner_window = 0;

static void scanner_timeout(void*)
{
    g_scanner_window->redraw();
    g_scanner_window->show();
}

extern char *user_config_dir;

static FILE *open_plugin_cache( const char *mode )
{
    char *path;

    asprintf( &path, "%s/%s", user_config_dir, "plugin_cache" );

    FILE *fp = fopen( path, mode );
        
    free( path );

    return fp;
}

Scanner_Window::Scanner_Window() :
    _box(nullptr)
{
    g_scanner_window = new Fl_Window(600,60,"Scanning Plugins");
    _box = new Fl_Box(20,10,560,40,"Scanning");
    _box->box(FL_UP_BOX);
    _box->labelsize(12);
    _box->labelfont(FL_BOLD);
    _box->show();
    g_scanner_window->end();
    g_scanner_window->set_modal();
}


Scanner_Window::~Scanner_Window()
{
}

void 
Scanner_Window::close_scanner_window()
{
    Fl::remove_timeout(scanner_timeout);
    g_scanner_window->hide();
    delete g_scanner_window;
    g_scanner_window = 0;
}

void
Scanner_Window::get_all_plugins ()
{
    // Remove any previous temp cache since we append to it
    char *path;
    asprintf( &path, "%s/%s", user_config_dir, "plugin_cache" );

    std::string remove_temp = "exec rm -r '";
    remove_temp += path;
    remove_temp += "'";
    system(remove_temp.c_str());

    free( path );

    Fl::add_timeout(0.03, scanner_timeout);
#ifdef LADSPA_SUPPORT
    if(_box)
    {
        _box->copy_label("Scanning LADSPA Plugins");
        _box->redraw();
        Fl::check();
    }

    system("nmxt-plugin-scan LADSPA");
#endif

#ifdef LV2_SUPPORT
    if(_box)
    {
        _box->copy_label("Scanning LV2 Plugins");
        _box->redraw();
        Fl::check();
    }

    system("nmxt-plugin-scan LV2");
#endif

#ifdef CLAP_SUPPORT
    auto clap_sp = clap_discovery::installedCLAPs();   // This to get paths

    for (const auto &q : clap_sp)
    {
        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
        }

        std::string s_command = "nmxt-plugin-scan CLAP ";
        s_command += q.u8string().c_str();

        system(s_command.c_str());
    }
#endif

#ifdef VST2_SUPPORT
    auto vst2_sp = vst2_discovery::installedVST2s();   // This to get paths

    for (const auto &q : vst2_sp)
    {
        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
        }

        std::string s_command = "nmxt-plugin-scan VST2 ";
        s_command += q.u8string().c_str();

        system(s_command.c_str());
    }
#endif

#ifdef VST3_SUPPORT
    auto vst3_sp = vst3_discovery::installedVST3s();   // This to get paths

    for (const auto &q : vst3_sp)
    {
        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
        }

        std::string s_command = "nmxt-plugin-scan VST3 ";
        s_command += q.u8string().c_str();

        system(s_command.c_str());
    }
#endif

    close_scanner_window();
}

bool
Scanner_Window::load_plugin_cache ( void )
{
    FILE *fp = open_plugin_cache( "r" );
    
    if ( !fp )
    {
        return false;
    }

    char *c_type;
    char *c_unique_id;
    unsigned long u_id;
    char *c_plug_path;
    char *c_name;
    char *c_author;
    char *c_category;
    int i_audio_inputs;
    int i_audio_outputs;

    g_plugin_cache.clear();

    while ( 9 == fscanf( fp, "%m[^|]|%m[^|]|%lu|%m[^|]|%m[^|]|%m[^|]|%m[^|]|%d|%d\n]\n",
            &c_type, &c_unique_id, &u_id, &c_plug_path, &c_name, &c_author,
            &c_category, &i_audio_inputs, &i_audio_outputs ) )
    {
        Plugin_Info pi(c_type);
        pi.s_unique_id = c_unique_id;
        pi.id = u_id;
        pi.plug_path = c_plug_path;
        pi.name = c_name;
        pi.author = c_author;
        pi.category = c_category;
        pi.audio_inputs = i_audio_inputs;
        pi.audio_outputs = i_audio_outputs;
        
        g_plugin_cache.push_back(pi);

        free(c_type);
        free(c_unique_id);
        free(c_plug_path);
        free(c_name);
        free(c_author);
        free(c_category);
    }

    fclose(fp);
    
    g_plugin_cache.sort();

    return true;
}