
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

#include "const.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Pack.H>

#include "../../nonlib/Thread.H"
#include "../../nonlib/debug.h"
#include "../../nonlib/Loggable.H"

#include "Mixer.H"
#include "Project.H"

/* for registration */
#include "Module.H"
#include "Gain_Module.H"
#include "Spatializer_Module.H"
#include "Plugin_Module.H"

#ifdef LADSPA_SUPPORT
#include "ladspa/LADSPA_Plugin.H"
#endif
#ifdef LV2_SUPPORT
#include "lv2/LV2_Plugin.H"
#endif
#ifdef CLAP_SUPPORT
#include "clap/CLAP_Plugin.H"
#endif
#ifdef VST2_SUPPORT
#include "vst2/VST2_Plugin.H"
#endif
#ifdef VST3_SUPPORT
#include "vst3/VST3_Plugin.H"
#endif

#include "JACK_Module.H"
#include "Meter_Module.H"
#include "Meter_Indicator_Module.H"
#include "Controller_Module.H"
#include "Mono_Pan_Module.H"
#include "Chain.H"
#include "Mixer_Strip.H"
#include "AUX_Module.H"
#include "NSM.H"
#include "Spatialization_Console.H"
#include "Group.H"

#include <signal.h>
#include <unistd.h>

#include "FL/Fl.H"
#include "FL/x.H"
#include "FL/Fl_PNG_Image.H"

#ifdef FLTK_SUPPORT
#include "../../FL/themes.H"
#endif

#define USER_CONFIG_DIR NMXT_CONFIG_DIRECTORY

const double NSM_CHECK_INTERVAL = 0.25f;

const char COPYRIGHT[] = "Copyright (C) 2008-2021 Jonathan Moore Liles (as Non-Mixer)";
const char COPYRIGHT2[] = "Copyright (C) 2021- Stazed (as Non-Mixer-XT)";

char *user_config_dir;
char *clipboard_dir;
Mixer *mixer;
NSM_Client *nsm;

char *instance_name;
std::string project_directory = "";
std::string export_import_strip = "";
std::vector<std::string>remove_custom_data_directories;

/* Maximum number of audio, aux, control ports*/
const int MAX_PORTS = 100; // extern

#include <errno.h>

static int
ensure_dirs( void )
{
    asprintf ( &user_config_dir, "%s/.config/%s", getenv ( "HOME" ), USER_CONFIG_DIR );
    asprintf ( &clipboard_dir, "%s/%s", user_config_dir, "clipboard" );

    int r = mkdir ( user_config_dir, 0777 );
    r = mkdir ( clipboard_dir, 0777 );

    return r == 0 || errno == EEXIST;
}

#include <signal.h>

static void
cb_main( Fl_Double_Window *, void * )
{
    if ( Fl::event ( ) == FL_SHORTCUT && Fl::event_key ( ) == FL_Escape )
        return;

    mixer->command_quit ( );
}

void
check_nsm( void * v )
{
    nsm->check ( );
    Fl::repeat_timeout ( NSM_CHECK_INTERVAL, check_nsm, v );
}

static volatile int got_sigterm = 0;

void
sigterm_handler( int )
{
    stop_process = true; // if NSM, stop jack process calls
    got_sigterm = 1;
}

void
check_sigterm( void * )
{
    if ( got_sigterm )
    {
        MESSAGE ( "Got SIGTERM, quitting..." );
        mixer->quit ( );
    }
    Fl::repeat_timeout ( 0.1f, check_sigterm );
}

int
main( int argc, char **argv )
{
#ifdef LV2_SUPPORT
    suil_init ( &argc, &argv, SUIL_ARG_NONE );
#endif
    bool no_ui = false;

    fl_display = 0;

    printf ( "%s %s\n", APP_TITLE, VERSION );
    printf ( "%s\n%s\n", COPYRIGHT, COPYRIGHT2 );

    /* Test JACK - check to see if jack is running before we go further.
       We show a fl_message and exit if not.*/
    jack_client_t *test_client;
    if (( test_client = jack_client_open ( "TestJACK", (jack_options_t)0, NULL )) == 0 )
    {
        fl_message("Cannot make a Jack client. Is JACK running?");
        return 0;
    }
    else    // jack is running so close the test client and continue.
    {
        jack_client_close ( test_client );
    }

    Thread::init ( );

    Thread thread ( "UI" );
    thread.set ( );

    ensure_dirs ( );

    signal ( SIGTERM, sigterm_handler );
    signal ( SIGHUP, sigterm_handler );
    signal ( SIGINT, sigterm_handler );

    Fl_Tooltip::color ( FL_BLACK );
    Fl_Tooltip::textcolor ( FL_YELLOW );
    Fl_Tooltip::size ( 14 );
    Fl_Tooltip::hoverdelay ( 0.1f );


    LOG_REGISTER_CREATE ( Mixer_Strip );
    LOG_REGISTER_CREATE ( Chain );
    LOG_REGISTER_CREATE ( Plugin_Module );
#ifdef LV2_SUPPORT
    LOG_REGISTER_CREATE ( LV2_Plugin );
#endif
#ifdef CLAP_SUPPORT
    LOG_REGISTER_CREATE ( CLAP_Plugin );
#endif
#ifdef LADSPA_SUPPORT
    LOG_REGISTER_CREATE ( LADSPA_Plugin );
#endif
#ifdef VST2_SUPPORT
    LOG_REGISTER_CREATE ( VST2_Plugin );
#endif
#ifdef VST3_SUPPORT
    LOG_REGISTER_CREATE ( VST3_Plugin );
#endif
    LOG_REGISTER_CREATE ( Gain_Module );
    LOG_REGISTER_CREATE ( Spatializer_Module );
    LOG_REGISTER_CREATE ( Meter_Module );
    LOG_REGISTER_CREATE ( JACK_Module );
    LOG_REGISTER_CREATE ( Mono_Pan_Module );
    LOG_REGISTER_CREATE ( Meter_Indicator_Module );
    LOG_REGISTER_CREATE ( Controller_Module );
    LOG_REGISTER_CREATE ( AUX_Module );
    LOG_REGISTER_CREATE ( Spatialization_Console );
    LOG_REGISTER_CREATE ( Group );

    signal ( SIGPIPE, SIG_IGN );


    const char *osc_port = NULL;

    nsm = new NSM_Client;

    instance_name = strdup ( APP_NAME );
    bool instance_override = false;

    static struct option long_options[] =
    {
        { "help", no_argument, 0, '?' },
        { "instance", required_argument, 0, 'i' },
        { "osc-port", required_argument, 0, 'p' },
        { "no-ui", no_argument, 0, 'u' },
        { 0, 0, 0, 0 }
    };

    int option_index = 0;
    int c = 0;


    while ( ( c = getopt_long_only ( argc, argv, "", long_options, &option_index ) ) != -1 )
    {
        switch ( c )
        {
        case 'p':
            DMESSAGE ( "Using OSC port %s", optarg );
            osc_port = optarg;
            break;
        case 'i':
            DMESSAGE ( "Using OSC port %s", optarg );
            free ( instance_name );
            instance_name = strdup ( optarg );
            instance_override = true;
            break;
        case 'u':
            DMESSAGE ( "Disabling user interface" );
            no_ui = true;
            break;
        case '?':
            printf ( "\nUsage: %s [--instance instance_name] [--osc-port portnum] [path_to_project]\n\n", argv[0] );
            exit ( 0 );
            break;
        }
    }

    {
#if 0
        /* There is no need for this - it's the same as 'non-mixer-xt -u' from command line */
        char *name = strdup ( argv[0] );
        char *n = basename ( name );

        if ( !strcmp ( n, "non-mixer-noui" ) )
        {
            DMESSAGE ( "Not running UI: invoked as non-mixer-noui" );
            no_ui = true;
        }

        free ( name );
#endif
        if ( NULL == getenv ( "DISPLAY" ) )
        {
            DMESSAGE ( "Not running UI: $DISPLAY environment variable unset" );
            no_ui = true;
        }
    }

    if ( !no_ui )
    {
        Fl::visual ( FL_DOUBLE | FL_RGB );

        Fl::visible_focus ( 0 );

        fl_register_images ( );
    }

    // "The main thread must call lock() to initialize the threading support in FLTK."
    Fl::lock ( );

    const char *nsm_url = getenv ( "NSM_URL" );

    Fl_Double_Window *main_window;


    {
        Fl_Double_Window *o = main_window = new Fl_Double_Window ( 800, 600, "Non Mixer XT" );
        {
            main_window->xclass ( APP_NAME );

            {
                Fl_Widget *m = mixer = new Mixer ( 0, 0, main_window->w ( ), main_window->h ( ), NULL );
                Fl_Group::current ( )->resizable ( m );
            }
        }
        o->end ( );

        o->size_range ( main_window->w ( ), mixer->min_h ( ), 0, 0 );

        o->callback ( (Fl_Callback*) cb_main, main_window );

        if ( !no_ui && !nsm_url )
        {
            o->show ( );
        }

    }

    // This unlock will freeze child locks in FLTK at least... It seemed to be needed for NTK???
    // But docs say this is only for unlocking child locks not the main lock used above for initializing.
    // Fl::unlock();

    mixer->init_osc ( osc_port );

    if ( nsm_url )
    {
        if ( !nsm->init ( nsm_url ) )
        {
            if ( instance_override )
                WARNING ( "--instance option is not available when running under session management, ignoring." );

            if ( optind < argc )
                WARNING ( "Loading files from the command-line is incompatible with session management, ignoring." );

            nsm->announce ( APP_NAME, ":optional-gui:switch:dirty:", argv[0] );

            /* if ( ! no_ui ) */
            /* { */
            // poll so we can keep OSC handlers running in the GUI thread and avoid extra sync
            Fl::add_timeout ( NSM_CHECK_INTERVAL, check_nsm, NULL );
            /* } */
        }
    }
    else
    {
        if ( optind < argc )
        {
            MESSAGE ( "Loading \"%s\"", argv[optind] );

            if ( !mixer->command_load ( argv[optind] ) )
            {
                fl_alert ( "Error opening project specified on commandline" );
            }
        }
    }

    Fl::add_timeout ( 0.1f, check_sigterm );
    Fl::dnd_text_ops ( 0 );

#ifdef FLTK_SUPPORT
    fl_register_themes ( USER_CONFIG_DIR );
#endif

    if ( !no_ui && !nsm_url )
    {
        DMESSAGE ( "Running UI..." );

        Fl::run ( );
    }
    else
    {
        while ( !got_sigterm )
        {
            Fl::wait ( 2147483.648 ); /* magic number means forever */
        }
    }

    delete main_window;
    main_window = NULL;

    /* Delete clipboard contents because if the strip contains custom data then it will accumulate */
    if ( clipboard_dir )
    {
        std::string remove_clipboard = "exec rm -r '";
        remove_clipboard += clipboard_dir;
        remove_clipboard += "'";
        system ( remove_clipboard.c_str ( ) );
    }

    MESSAGE ( "Your fun is over" );
}
