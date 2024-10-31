/*******************************************************************************/
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
 * File:   Scanner_Window.C
 * Author: sspresto
 *
 * Created on July 17, 2024, 10:43 PM
 *
 */
#include <thread>
#include <unistd.h>
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

#define SCANNER_BINARY "nmxt-plugin-scan"

static Fl_Window * g_scanner_window = 0;
static bool _scan_complete = false;

static void
window_cb( Fl_Widget *, void * )
{
    // don't allow user to close the window with X box because scan will
    // continue and we lose the modal. Make them use the cancel button.
    return;
}

static void
scanner_timeout( void* )
{
    g_scanner_window->redraw ( );
    g_scanner_window->show ( );

    Fl::repeat_timeout ( 0.03f, &scanner_timeout );
}

extern char *user_config_dir;

static FILE *
open_plugin_cache( const char *mode )
{
    char *path;

    asprintf ( &path, "%s/%s", user_config_dir, PLUGIN_CACHE );

    FILE *fp = fopen ( path, mode );

    free ( path );

    return fp;
}

static void 
scan_bundle(std::string s_command)
{
    system ( s_command.c_str ( ) );
    _scan_complete = true;
}

Scanner_Window::Scanner_Window( ) :
    _box( nullptr ),
    _cancel_button( nullptr ),
    _skip_button( nullptr )
{
    g_scanner_window = new Fl_Window ( 720, 60, "Scanning Plugins" );
    _box = new Fl_Box ( 20, 10, 560, 40, "Scanning" );
    _box->box ( FL_UP_BOX );
    _box->labelsize ( 12 );
    _box->labelfont ( FL_BOLD );
    _box->show ( );
    _skip_button = new Fl_Button ( 590, 10, 50, 40, "Skip" );
    _skip_button->type ( FL_NORMAL_BUTTON );
    _skip_button->labelsize ( 12 );
    _skip_button->labelfont( FL_BOLD );
    _skip_button->color( FL_DARK_BLUE );
    _skip_button->show();
    _cancel_button = new Fl_Button ( 650, 10, 50, 40, "Cancel" );
    _cancel_button->type ( FL_TOGGLE_BUTTON );
    _cancel_button->labelsize ( 12 );
    _cancel_button->labelfont ( FL_BOLD );
    _cancel_button->color ( FL_RED );
    _cancel_button->show ( );

    g_scanner_window->callback ( (Fl_Callback*) window_cb );
    g_scanner_window->end ( );
    g_scanner_window->set_modal ( );
}

Scanner_Window::~Scanner_Window( )
{
}

void
Scanner_Window::close_scanner_window( )
{
    Fl::remove_timeout ( &scanner_timeout );
    g_scanner_window->hide ( );
    delete g_scanner_window;
    g_scanner_window = 0;
}

bool
Scanner_Window::get_all_plugins( )
{
    // if present remove any previous temp cache since we append to it
    remove_temporary_cache ( );

    Fl::add_timeout ( 0.03f, &scanner_timeout );

#ifdef CLAP_SUPPORT
    auto clap_sp = clap_discovery::installedCLAPs ( ); // This to get paths

    for ( const auto &q : clap_sp )
    {
        if ( _box )
        {
            _box->copy_label ( q.u8string ( ).c_str ( ) );
            _box->redraw ( );
            Fl::check ( );
        }

        std::string s_command(SCANNER_BINARY);
        s_command += " CLAP '";
        s_command += q.u8string ( ).c_str ( );
        s_command += "'";

        if( !run_scanner( s_command ) )
        {
            cancel_scanning ( );
            return false;
        }
    }
#endif

#ifdef LADSPA_SUPPORT
    if ( _box )
    {
        _box->copy_label ( "Scanning LADSPA Plugins" );
        _box->redraw ( );
        Fl::check ( );
    }

    std::string s_command(SCANNER_BINARY);
    s_command += " LADSPA";
    if( !run_scanner( s_command ) )
    {
        cancel_scanning ( );
        return false;
    }
#endif

#ifdef LV2_SUPPORT
    if ( _box )
    {
        _box->copy_label ( "Scanning LV2 Plugins" );
        _box->redraw ( );
        Fl::check ( );
    }

    s_command = SCANNER_BINARY;
    s_command += " LV2";
    if( !run_scanner( s_command ) )
    {
        cancel_scanning ( );
        return false;
    }
#endif

#ifdef VST2_SUPPORT
    auto vst2_sp = vst2_discovery::installedVST2s ( ); // This to get paths

    for ( const auto &q : vst2_sp )
    {
        if ( _box )
        {
            _box->copy_label ( q.u8string ( ).c_str ( ) );
            _box->redraw ( );
            Fl::check ( );
        }

        std::string s_command(SCANNER_BINARY);
        s_command += " VST2 '";
        s_command += q.u8string ( ).c_str ( );
        s_command += "'";

        if( !run_scanner( s_command ) )
        {
            cancel_scanning ( );
            return false;
        }
    }
#endif

#ifdef VST3_SUPPORT
    auto vst3_sp = vst3_discovery::installedVST3s ( ); // This to get paths

    for ( const auto &q : vst3_sp )
    {
        if ( _box )
        {
            _box->copy_label ( q.u8string ( ).c_str ( ) );
            _box->redraw ( );
            Fl::check ( );
        }

        std::string s_command(SCANNER_BINARY);
        s_command += " VST3 '";
        s_command += q.u8string ( ).c_str ( );
        s_command += "'";

        if( !run_scanner( s_command ) )
        {
            cancel_scanning ( );
            return false;
        }
    }
#endif

    close_scanner_window ( );

    // Rename temp cache to real cache if we did not cancel
    char *path_temp;
    asprintf ( &path_temp, "%s/%s", user_config_dir, PLUGIN_CACHE_TEMP );

    char *path_real;
    asprintf ( &path_real, "%s/%s", user_config_dir, PLUGIN_CACHE );

    if ( rename ( path_temp, path_real ) )
        WARNING ( "Rename of temporary cache file failed" );

    free ( path_temp );
    free ( path_real );

    return true;
}

bool
Scanner_Window::load_plugin_cache( void )
{
    FILE *fp = open_plugin_cache ( "r" );

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
    int i_midi_inputs;
    int i_midi_outputs;

    g_plugin_cache.clear ( );

    while ( 11 == fscanf ( fp, "%m[^|]|%m[^|]|%lu|%m[^|]|%m[^|]|%m[^|]|%m[^|]|%d|%d|%d|%d\n]\n",
        &c_type, &c_unique_id, &u_id, &c_plug_path, &c_name, &c_author,
        &c_category, &i_audio_inputs, &i_audio_outputs, &i_midi_inputs, &i_midi_outputs ) )
    {
        Plugin_Info pi ( c_type );
        pi.s_unique_id = c_unique_id;
        pi.id = u_id;
        pi.plug_path = c_plug_path;
        pi.name = c_name;
        pi.author = c_author;
        pi.category = c_category;
        pi.audio_inputs = i_audio_inputs;
        pi.audio_outputs = i_audio_outputs;
        pi.midi_inputs = i_midi_inputs;
        pi.midi_outputs = i_midi_outputs;

        g_plugin_cache.push_back ( pi );

        free ( c_type );
        free ( c_unique_id );
        free ( c_plug_path );
        free ( c_name );
        free ( c_author );
        free ( c_category );
    }

    fclose ( fp );

    if ( g_plugin_cache.empty ( ) )
        return false;

    g_plugin_cache.sort ( );

    return true;
}

void
Scanner_Window::remove_temporary_cache( )
{
    char *path;
    asprintf ( &path, "%s/%s", user_config_dir, PLUGIN_CACHE_TEMP );

    if ( remove ( path ) )
    {
        // most of the time the temp file will be renamed, so fail is expected
        // WARNING("Could not remove plugin_cache_temp file, does not exist");
    }

    free ( path );
}

void
Scanner_Window::cancel_scanning( )
{
    remove_temporary_cache ( );
    close_scanner_window ( );
}

bool
Scanner_Window::run_scanner(std::string s_command)
{
    _scan_complete = false;
    std::thread t(scan_bundle, s_command.c_str());

    bool continue_scan = true;

    std::string s_kill_command( "pkill -f " );
    s_kill_command += SCANNER_BINARY;

    while(!_scan_complete)
    {
        Fl::check ( );
        if ( _skip_button->value( ) )
        {
            system( s_kill_command.c_str() );
            continue_scan = true;

            // gotta reset manually or it will still be set on next plugin
            _skip_button->value(0);
            break;
        }

        if ( _cancel_button->value ( ) )
        {
            system( s_kill_command.c_str() );
            continue_scan = false;
            break;
        }

        usleep(1500);
    }

    t.join();

    return continue_scan;
}
