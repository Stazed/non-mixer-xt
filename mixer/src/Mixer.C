
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

/* This is the main mixer group. It contains and manages Mixer_Strips. */
#include "const.h"

#include "Mixer.H"
#include "Mixer_Strip.H"

#include <FL/Fl_Pack.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <FL/Fl.H>

#include "../../FL/New_Project_Dialog.H"
#include "../../FL/Fl_Flowpack.H"
#include "../../FL/Fl_Menu_Settings.H"
#include "../../FL/About_Dialog.H"
#include "../../FL/Fl_Value_SliderX.H"
#include "../../nonlib/file.h"
#include "../../nonlib/debug.h"
#include "../../nonlib/OSC/Endpoint.H"
#include "../../nonlib/string_util.h"

#include "Project.H"
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Theme_Chooser.H>
#include <FL/Fl_Tooltip.H>
#include "Spatialization_Console.H"
#include "Group.H"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <lo/lo.h>

#include "Controller_Module.H"
#include "NSM.H"
#include "Chain.H"

/* const double FEEDBACK_UPDATE_FREQ = 1.0f; */
const double FEEDBACK_UPDATE_FREQ = 1.0f / 30.0f;

static bool is_startup = true;

extern char *user_config_dir;
extern char *instance_name;


extern NSM_Client *nsm;
extern std::vector<std::string>remove_custom_data_directories;

Spatialization_Console *Mixer::spatialization_console = 0;

struct both_slashes {
    bool operator()(char a, char b) const {
        return a == '/' && b == '/';
    }
};

void
Mixer::show_tooltip ( const char *s )
{
    mixer->_status->label( s );
}

void
Mixer::hide_tooltip ( void )
{
    mixer->_status->label( 0 );
}


/************************/
/* OSC Message Handlers */
/************************/

#undef OSC_REPLY_OK
#undef OSC_REPLY_ERR
#undef OSC_REPLY

#define OSC_REPLY_OK() ((OSC::Endpoint*)user_data)->send( lo_message_get_source( msg ), path, 0, "OK" )
#define OSC_REPLY( value ) ((OSC::Endpoint*)user_data)->send( lo_message_get_source( msg ), path, value )
#define OSC_REPLY_ERR(errcode, value) ((OSC::Endpoint*)user_data)->send( lo_message_get_source( msg ), path,errcode, value )
#define OSC_ENDPOINT() ((OSC::Endpoint*)user_data)

static int osc_add_strip ( const char *path, const char *, lo_arg **, int , lo_message msg, void *user_data )
{
   OSC_DMSG();

   Fl::lock();

   ((Mixer*)(OSC_ENDPOINT())->owner)->command_add_strip();

   Fl::unlock();

   OSC_REPLY_OK();

   return 0;
}

 int
Mixer::osc_non_hello ( const char *, const char *, lo_arg **, int , lo_message msg, void * )
{
    mixer->handle_hello( msg );
    return 0;
}


void
Mixer::handle_hello ( lo_message msg )
{    
    int argc = lo_message_get_argc( msg );
    lo_arg **argv = lo_message_get_argv( msg );
    
    if ( argc >= 4 )
    {
        const char *url = &argv[0]->s;
        const char *name = &argv[1]->s;
        const char *version = &argv[2]->s;
        const char *id = &argv[3]->s;
        
        MESSAGE( "Got hello from NON peer %s (%s) @ %s with ID \"%s\"", name, version, url, id );
                        
        mixer->osc_endpoint->handle_hello( id, url );
    }
}

void
Mixer::say_hello ( void )
{
    lo_message m = lo_message_new();

    lo_message_add( m, "sssss",
                    "/non/hello",
                    osc_endpoint->url(),
                    APP_NAME,
                    VERSION,
                    instance_name );

    nsm->broadcast( m );
    
    lo_message_free( m );
    
    // needed to indicate that for raysession
    nsm->nsm_send_is_hidden ( nsm );
}



static 
Fl_Menu_Item *
find_item( Fl_Menu_ *menu, const char *path )
 {
     return const_cast<Fl_Menu_Item*>(menu->find_item( path ));
 }

void
Mixer::sm_active ( bool b )
{
    sm_blinker->value( b );
    sm_blinker->tooltip( nsm->session_manager_name() );

    if ( b )
    {
        find_item( menubar, "&Project/&Open" )->deactivate();
        find_item( menubar, "&Project/&New" )->deactivate();
    }
}


void
Mixer::redraw_windows ( void )
{
    window()->redraw();

    if ( Fl::first_window() )
        for ( Fl_Window *w = Fl::first_window(); ( w = Fl::next_window( w ) ); )
            w->redraw();
}

void Mixer::command_new ( void )
{
    DMESSAGE( "New project" );
    
    char *default_path = read_line( user_config_dir, "default_path" );
    
    char *result_path = default_path;

    char *path = new_project_chooser( &result_path );
    
    if ( path )
    {
        project_directory = path;

        /* Clean the path of any extra slashes since it can cause problems with plugin link saving */
        project_directory.erase(std::unique(project_directory.begin(), project_directory.end(), both_slashes()), project_directory.end());

        if ( ! Project::create( project_directory.c_str(), NULL ) )
        {
            fl_alert( "Error creating project!" );
            project_directory = "";
        }

        DMESSAGE("project_directory = %s", project_directory.c_str());
        free( path );
    }
    
    load_project_settings();

    update_window_title();

    update_menu();
    
    if ( result_path != default_path )
        free(default_path);

    if ( result_path )
    {
        write_line( user_config_dir, "default_path", result_path );
        free( result_path );
    }
}

void Mixer::cb_menu(Fl_Widget* o) {
    Fl_Menu_Bar *menu = (Fl_Menu_Bar*)o;

/*     const Fl_Menu_Item *mi = &menu->menu()[menu->value()]; */

     char picked[256];
     // const char *picked = menu->text();

    menu->item_pathname( picked, sizeof( picked ) );

    DMESSAGE( "Picked %s", picked );

    if (! strcmp( picked, "&Project/&New") )
    {
        if(!is_valid_open_new())
            return;

        command_new();
    }
    else if (! strcmp( picked, "&Project/&Open" ) )
    {
        if(!is_valid_open_new())
            return;

        char *path = read_line( user_config_dir, "default_path");

        const char *name = fl_dir_chooser( "Open Project", path );

        free( path );

        mixer->hide();

        if(name)
            project_directory = name;

        if ( int err = Project::open( name ) )
        {
            fl_alert( "Error opening project: %s", Project::errstr( err ) );
            project_directory = "";
        }

        update_menu();

        update_window_title();

        mixer->show();
    }
    else if (! strcmp( picked, "&Project/&Save" ) )
    {
        command_save();
    }
    else if (! strcmp( picked, "&Project/&Quit") )
    {
        command_quit();
    }
    else if ( !strcmp( picked, "&Mixer/&Add Strip" ) )
    {
        command_add_strip();
    }
    else if ( !strcmp( picked, "&Mixer/Add &N Strips" ) )
    {
        const char *s = fl_input( "Enter number of strips to add" );

        if ( s )
        {
            for ( int i = atoi( s ); i > 0; i-- )
                command_add_strip();
        }
    }
    else if ( !strcmp( picked, "&Mixer/&Import Strip" ) )
    {
        const char *s = fl_file_chooser( "Import strip filename:", "*.strip", NULL, 0 );

        if ( s )
        {
            export_import_strip = s;
            if (! Mixer_Strip::import_strip( s ) )
                fl_alert( "%s", "Failed to import strip!" );

            export_import_strip = "";
        }
    }
    else if ( ! strcmp( picked, "&Project/Se&ttings/Learn/By Strip Name" ) )
    {
        Controller_Module::learn_by_number = false;
    }
    else if ( ! strcmp( picked, "&Project/Se&ttings/Learn/By Strip Number" ) )
    {
        Controller_Module::learn_by_number = true;
    }
    else if ( ! strcmp( picked, "&Remote Control/Start Learning" ) )
    {
        Controller_Module::learn_mode( true );
        tooltip( "Now in learn mode. Click on a highlighted control to teach it something." );
        redraw();
    }
    else if ( ! strcmp( picked, "&Remote Control/Stop Learning" ) )
    {
        Controller_Module::learn_mode( false );
        tooltip( "Learning complete" );
        redraw();
    }
    else if ( !strcmp( picked, "&Remote Control/Send State" ) )
    {
	send_feedback(true);
    }
    else if ( ! strcmp( picked, "&Remote Control/Clear All Mappings" ) )
    {
        if ( 1 == fl_choice( "This will remove all mappings, are you sure?", "No", "Yes", NULL ) )
        {
            command_clear_mappings();
        }
    }
    else if ( !strcmp( picked, "&Mixer/Paste" ) )
    {
        Fl::paste(*this);
    }
    else if (! strcmp( picked, "&Project/Se&ttings/&Rows/One") )
    {
        rows( 1 );
    }
    else if (! strcmp( picked, "&Project/Se&ttings/&Rows/Two") )
    {
        rows( 2 );
    }
    else if (! strcmp( picked, "&Project/Se&ttings/&Rows/Three") )
    {
        rows( 3 );
    }
    else if (! strcmp( picked, "&Mixer/&Spatialization Console") )
    {
        if ( ! spatialization_console )
        {
            Spatialization_Console *o = new Spatialization_Console();
            spatialization_console = o;            
        }
        
        if ( ! menu->mvalue()->value() )
            spatialization_console->hide();
        else
            spatialization_console->show();
    }
    else if (! strcmp( picked, "&Project/Se&ttings/Make Default") )
    {
        save_default_project_settings();
    }
    else if (! strcmp( picked, "&View/&Theme") )
    {
        fl_theme_chooser();
    }
    else if ( ! strcmp( picked, "&Mixer/Toggle &Fader View" ) )
    {
        command_toggle_fader_view();
    }
    else if ( ! strcmp( picked, "&Help/&About" ) )
    {
        About_Dialog ab( PIXMAP_PATH "/non-mixer-xt/icon-256x256.png" );

        ab.logo_box->label( VERSION );

        ab.title->label( "Non Mixer XT" );

        ab.copyright->label( "Copyright (C) 2008-2021 Jonathan Moore Liles\n"
                             "Copyright (C) 2022-2023 Stazed" );
        ab.credits->label(
            "Legacy Non Mixer by Jonathan Moore Liles.\n"
            "Filipe Coelho - initial LV2 implementation and\n"
            "X11 embedded support from the Carla project.\n"
            "David Robillard for LV2 atom ports and MIDI\n"
            "event support from the Jalv project.\n"
            "Rui Nuno Capela for LV2 showInterface, external\n"
            "UI, and presets from the Qtractor project.\n"
            "Jean-Emmanuel Doucet - Extended OSC support.\n"
            "Non Mixer XT modifications by Stazed.\n"
        );

        /* The FL submodule label is set to timeline, so reset it here to the mixer */
        ab.website_url->label( WEBSITE );
        ab.run();
    }
    else if ( !strcmp( picked, "&Help/&Manual" ))
    {
        char *pat;

        asprintf( &pat, "file://%s.html", DOCUMENT_PATH "/non-mixer-xt/MANUAL" );

        open_url( pat );

        free( pat );
    }
}

void Mixer::cb_menu(Fl_Widget* o, void* v) {
    ((Mixer*)(v))->cb_menu(o);
}

bool Mixer::is_valid_open_new()
{
    if(mixer_strips->children() || !project_directory.empty())
    {
        fl_alert("Error: You cannot open/create a new project\n"
                "if any existing project is open or\n"
                "if any mixer strips are present.");
        return false;
    }

    return true;
}

void Mixer::update_frequency ( float v )
{
    _update_interval = 1.0f / v;

    Fl::remove_timeout( &Mixer::update_cb, this );
    Fl::add_timeout( _update_interval, &Mixer::update_cb, this );
}

void 
Mixer::update_cb ( void *v )
{
    ((Mixer*)v)->update_cb();
}

void
Mixer::update_cb ( void )
{
    Fl::repeat_timeout( _update_interval, &Mixer::update_cb, this );

    /* if ( active_r() && visible_r() ) */
    {
        for ( int i = 0; i < mixer_strips->children(); i++ )
        {
            ((Mixer_Strip*)mixer_strips->child(i))->update();
        }
    }
}


static void
progress_cb ( int p, void * )
{
    static int oldp = 0;

    if ( p != oldp )
    {
        oldp = p;
        if ( nsm )
        {
            nsm->progress( p / 100.0f );
        }

        /* When the mixer is first starting under NSM, the call to Fl::check would sometimes
           occur before the initial main() Fl:wait() which would cause an intermittent segfault.
           So the usleep(50000) is to allow the main() time to initialize before. */
        if(is_startup)
        {
            is_startup = false;
            usleep(50000);
        }

        Fl::check();    // Not sure why this is needed here...
    }
}

void
Mixer::save_default_project_settings ( void )
{
    char path[256];
    snprintf( path, sizeof( path ), "%s/%s", user_config_dir, ".default_project_settings" );
    
    ((Fl_Menu_Settings*)menubar)->dump( menubar->find_item( "&Project/Se&ttings" ), path );
}

void
Mixer::load_default_project_settings ( void )
{
    char path[256];
    snprintf( path, sizeof( path ), "%s/%s", user_config_dir, ".default_project_settings" );
    
    ((Fl_Menu_Settings*)menubar)->load( menubar->find_item( "&Project/Se&ttings" ), path );
}

void
Mixer::reset_project_settings ( void )
{
    rows(1);

    load_default_project_settings();
}

void
Mixer::save_project_settings ( void )
{
    if ( ! Project::open() )
	return;
    
    ((Fl_Menu_Settings*)menubar)->dump( menubar->find_item( "&Project/Se&ttings" ), "options" );
}

void
Mixer::load_project_settings ( void )
{
    reset_project_settings();

//    if ( Project::open() )
	((Fl_Menu_Settings*)menubar)->load( menubar->find_item( "&Project/Se&ttings" ), "options" );

    update_menu();
}

Mixer::Mixer ( int X, int Y, int W, int H, const char *L ) :
    Fl_Group( X, Y, W, H, L )
{
    Loggable::dirty_callback( &Mixer::handle_dirty, this );
    Loggable::progress_callback( progress_cb, NULL );

    Fl_Tooltip::hoverdelay( 0 );
    Fl_Tooltip::delay( 0 );
    fl_show_tooltip = &Mixer::show_tooltip;
    fl_hide_tooltip = &Mixer::hide_tooltip;
    /* Fl_Tooltip::size( 11 ); */
    /* Fl_Tooltip::textcolor( FL_FOREGROUND_COLOR ); */
    /* Fl_Tooltip::color( fl_color_add_alpha( FL_DARK1, 0 ) ); */
//    fl_tooltip_docked = 1;

//    _groups.resize(16);

    _rows = 1;
    _strip_height = 0;
    box( FL_FLAT_BOX );
    labelsize( 96 );
    { Fl_Group *o = new Fl_Group( X, Y, W, 24 );

        { Fl_Menu_Bar *o = menubar = new Fl_Menu_Bar( X, Y, W, 24 );
            o->add( "&Project/&New" );
            o->add( "&Project/&Open" );
            o->add( "&Project/Se&ttings/&Rows/One", '1', 0, 0, FL_MENU_RADIO | FL_MENU_VALUE );
            o->add( "&Project/Se&ttings/&Rows/Two", '2', 0, 0, FL_MENU_RADIO );
            o->add( "&Project/Se&ttings/&Rows/Three", '3', 0, 0, FL_MENU_RADIO );
            o->add( "&Project/Se&ttings/Learn/By Strip Number", 0, 0, 0, FL_MENU_RADIO );
            o->add( "&Project/Se&ttings/Learn/By Strip Name", 0, 0, 0, FL_MENU_RADIO | FL_MENU_VALUE );
            o->add( "&Project/Se&ttings/Make Default", 0,0,0);
            o->add( "&Project/&Save", FL_CTRL + 's', 0, 0 );
            o->add( "&Project/&Quit", FL_CTRL + 'q', 0, 0 );
            o->add( "&Mixer/&Add Strip", 'a', 0, 0 );
            o->add( "&Mixer/Add &N Strips" );
            o->add( "&Mixer/&Import Strip" );
            o->add( "&Mixer/Paste", FL_CTRL + 'v', 0, 0 );
            o->add( "&Mixer/&Spatialization Console", FL_F + 8, 0, 0, FL_MENU_TOGGLE );
            o->add( "&Mixer/Toggle &Fader View", FL_ALT + 'f', 0, 0, FL_MENU_TOGGLE );
//            o->add( "&Mixer/&Signal View", FL_ALT + 's', 0, 0, FL_MENU_TOGGLE );
            o->add( "&Remote Control/Start Learning", FL_F + 9, 0, 0 );
            o->add( "&Remote Control/Stop Learning", FL_F + 10, 0, 0 );
            o->add( "&Remote Control/Send State" );
            o->add( "&Remote Control/Clear All Mappings", 0, 0, 0 );
            o->add( "&View/&Theme", 0, 0, 0 );
            o->add( "&Help/&Manual" );
            o->add( "&Help/&About" );
            o->callback( cb_menu, this );
        }
        { Fl_Box *o = project_name = new Fl_Box( X + 150, Y, W, 24 );
            o->labelfont( FL_HELVETICA_ITALIC );
            o->label( 0 );
            o->align( FL_ALIGN_INSIDE | FL_ALIGN_CENTER );
            o->labeltype( FL_SHADOW_LABEL );
            Fl_Group::current()->resizable( o );
        }
        { sm_blinker = new Fl_Button( ( X + W) - 37, Y + 4, 35, 15, "SM");
            sm_blinker->box(FL_ROUNDED_BOX);
            sm_blinker->down_box(FL_ROUNDED_BOX);
            sm_blinker->color(FL_DARK2);
            sm_blinker->selection_color((Fl_Color)93);
            sm_blinker->labeltype(FL_NORMAL_LABEL);
            sm_blinker->labelfont(3);
            sm_blinker->labelsize(14);
            sm_blinker->labelcolor(FL_DARK3);
            sm_blinker->align(Fl_Align(FL_ALIGN_CENTER));
            sm_blinker->when(FL_WHEN_RELEASE);
            sm_blinker->deactivate();

        } // Fl_Blink_Button* sm_blinker
        o->end();
    }
    { Fl_Scroll *o = scroll = new Fl_Scroll( X, Y + 24, W, H - ( 100 ) );
        o->box( FL_FLAT_BOX );
//        o->type( Fl_Scroll::HORIZONTAL_ALWAYS );
//        o->box( Fl_Scroll::BOTH );
        {
            Fl_Flowpack *o = mixer_strips = new Fl_Flowpack( X, Y + 24, W, H - ( 18*2 + 24 ));
//            label( "Non-Mixer-XT" );
            align( (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_INSIDE) );
            o->flow( false );
            o->box( FL_FLAT_BOX );
	    o->color( fl_darker(FL_BACKGROUND_COLOR ));
            o->type( Fl_Pack::HORIZONTAL );
            o->hspacing( 2 );
            o->vspacing( 2 );
            o->end();
            Fl_Group::current()->resizable( o );
        }
        o->end();
        Fl_Group::current()->resizable( o );
    }
    { Fl_Box *o = _status = new Fl_Box( X, Y + H - 18, W, 18 );
        o->align( FL_ALIGN_LEFT | FL_ALIGN_INSIDE );
        o->labelsize( 10 );
        o->box( FL_FLAT_BOX );
        o->color( FL_DARK1 );
    }
    end();

    Mixer::resize( X,Y,W,H );

    update_frequency( 24 );


    update_menu();

    load_options();
}

/* translate message addressed to strip number to appropriate strip */
int
Mixer::osc_strip_by_number ( const char *path, const char * /*types*/, lo_arg ** /*argv*/, int /*argc*/, lo_message msg, void *user_data )
{
    int n;
    char *rem;
    char *client_name;

    OSC::Endpoint *ep = (OSC::Endpoint*)user_data;
    
    if ( 3 != sscanf( path, "%m[^/]/strip#/%d/%m[^\n]", &client_name, &n, &rem ) )
        return -1;

    Mixer_Strip *o = mixer->track_by_number( n );

    if ( ! o )
    {
        DMESSAGE( "No strip by number %i", n );
        return 0;
    }

    char *new_path;

    char *stripname = escape_url( o->name() );
    
    asprintf( &new_path, "%s/strip/%s/%s", client_name, stripname, rem );

    free( stripname );
    
    /* DMESSAGE( "Forwarding by-number OSC path: %s === %s", path, new_path ); */

    free( rem );

    lo_send_message( ep->address(), new_path, msg );

    free( new_path );

    return 0;
}

void
Mixer::load_translations ( void )
{
    FILE *fp = fopen( "mappings", "r" );

    if ( ! fp )
    {
        WARNING( "Error opening mappings file for reading" );
        return;
    }

    char *to;
    char *from;

    while ( 2 == fscanf( fp, "%m[^|> ] |> %m[^ \n]\n", &from, &to ) )
    {
        osc_endpoint->add_translation( from, to );
        free(from);
        free(to);
    }

    fclose( fp );
}

void
Mixer::save_translations ( void )
{
    FILE *fp = fopen( "mappings", "w" );

    if ( ! fp )
    {
        WARNING( "Error opening mappings file for writing" );
        return;
    }

    for ( int i = 0; i < osc_endpoint->ntranslations(); i++ )
    {
        const char *to;
        const char *from;

        if ( osc_endpoint->get_translation( i, &to, &from ) )
        {
            fprintf( fp, "%s |> %s\n", to, from );
        }
    }

    fclose( fp );
}

int
Mixer::init_osc ( const char *osc_port )
{
    osc_endpoint = new OSC::Endpoint();

    if ( int r = osc_endpoint->init( LO_UDP, osc_port ) )
        return r;

    osc_endpoint->owner = this;
    
    printf( "OSC=%s\n", osc_endpoint->url() );

    osc_endpoint->add_method( "/non/hello", "ssss", &Mixer::osc_non_hello, osc_endpoint, "" );
    
//  
    osc_endpoint->add_method( "/non/mixer/add_strip", "", osc_add_strip, osc_endpoint, "" );
  
    osc_endpoint->start();

    osc_endpoint->add_method( NULL, NULL, osc_strip_by_number, osc_endpoint, "");
    
    return 0;
}


Mixer::~Mixer ( )
{
    DMESSAGE( "Destroying mixer" );

    save_options();

    Fl::remove_timeout( &Mixer::update_cb, this );

    Fl::remove_timeout( &Mixer::send_feedback_cb, this );
 
/* FIXME: teardown */
    mixer_strips->clear();
}

void
Mixer::add_group ( Group *g )
{
    groups.push_back( g );

    for ( int i = mixer_strips->children(); i--; )
        ((Mixer_Strip*)mixer_strips->child(i))->update_group_choice();
}

void
Mixer::remove_group ( Group *g )
{
    groups.remove(g);

    for ( int i = mixer_strips->children(); i--; )
        ((Mixer_Strip*)mixer_strips->child(i))->update_group_choice();
}

void Mixer::resize ( int X, int Y, int W, int H )
{
    Fl_Group::resize( X, Y, W, H );

    sm_blinker->resize( X + W - 40, Y + 5, 35, 15 );
    
    scroll->resize( X, Y + 24, W, H - 24 - 18 );

    mixer_strips->resize( X, Y + 24, W, H - (18*2) - 24 );

    rows( _rows );
}

void Mixer::add ( Mixer_Strip *ms )
{
    MESSAGE( "Add mixer strip \"%s\"", ms->name() );

    mixer_strips->add( ms );

    ms->size( ms->w(), _strip_height );
    ms->redraw();
    ms->take_focus();

    renumber_strips();
}

int
Mixer::find_strip ( const Mixer_Strip *m ) const
{
    return mixer_strips->find( m );
}

void
Mixer::quit ( void )
{
    if ( nsm->is_active() )
    {
        nsm->nsm_send_is_hidden ( nsm );
    }
    else    // we really are quitting, not just hiding
    {
        /* Under high DSP load, on quit the chain modules may get deleted before jack is deactivated.
           The jack process callback would then be called on a deleted module and segfault.
           This flag will tell jack to return on all process calls, rather than waiting for
           the deactivation to occur, which may be too late.  */
        stop_process = true;
    }

    while ( Fl::first_window() ) Fl::first_window()->hide();
}

void
Mixer::renumber_strips ( void )
{
    for ( int i = mixer_strips->children(); i--; )
    {
	Mixer_Strip *o = (Mixer_Strip*)mixer_strips->child(i);
	
        o->number( find_strip( o ) );
    }
}
		      

void
Mixer::insert ( Mixer_Strip *ms, Mixer_Strip *before )
{
//    mixer_strips->remove( ms );
    mixer_strips->insert( *ms, before );
    renumber_strips();
    schedule_feedback();
//    scroll->redraw();
}
void
Mixer::insert ( Mixer_Strip *ms, int i )
{
    Mixer_Strip *before = (Mixer_Strip*)mixer_strips->child( i );

    insert( ms, before);
    renumber_strips();
}

void
Mixer::move_left ( Mixer_Strip *ms )
{
    int i = mixer_strips->find( ms );

    if ( i > 0 )
        insert( ms, i - 1 );

    renumber_strips();
    /* FIXME: do better */
    mixer_strips->redraw();
}

void
Mixer::move_right ( Mixer_Strip *ms )
{
    int i = mixer_strips->find( ms );

    if ( i < mixer_strips->children() - 1 )
        insert( ms, i + 2 );

    renumber_strips();
    
    /* FIXME: do better */
    mixer_strips->redraw();
}

void Mixer::remove ( Mixer_Strip *ms )
{
    MESSAGE( "Remove mixer strip \"%s\"", ms->name() );

    mixer_strips->remove( ms );

    if ( parent() )
        parent()->redraw();

    renumber_strips();
    schedule_feedback();
}


Mixer_Strip *
Mixer::event_inside ( void )
{
    for ( int i = mixer_strips->children(); i--; )
        if ( Fl::event_inside( mixer_strips->child(i) ) )
            return (Mixer_Strip*)mixer_strips->child(i);

    return NULL;
}   

bool
Mixer::contains ( Mixer_Strip *ms )
{
    return ms->parent() == mixer_strips;
}

/* set the ideal number of rows... All may not actually fit. */
void
Mixer::rows ( int ideal_rows )
{
    int sh = 0;

    int actual_rows = 1;

    /* calculate how many rows will actually fit */
    int can_fit = scroll->h() / ( Mixer_Strip::min_h() );
    
    actual_rows = can_fit > 0 ? can_fit : 1;
    
    if ( actual_rows > ideal_rows )
        actual_rows = ideal_rows;
    
    /* calculate strip height */
    if ( actual_rows > 1 )
    {
        sh = ( scroll->h() / (float)actual_rows ) - ( mixer_strips->vspacing() * ( actual_rows - 2 ));
        mixer_strips->flow(true);
    }
    else
        actual_rows = 1;

    if ( 1 == actual_rows )
    {
      sh = (scroll->h() - 18);
      mixer_strips->flow( false );
    }

    int tw = 0;

    for ( int i = 0; i < mixer_strips->children(); ++i )
    {
        Mixer_Strip *t = (Mixer_Strip*)mixer_strips->child( i );

        t->size( t->w(), sh );

        tw += t->w() + mixer_strips->hspacing();
    }

    if ( actual_rows > 1 )
        mixer_strips->size( scroll->w() - 18, mixer_strips->h() );
    else
        mixer_strips->size( tw, sh );

    _rows = ideal_rows;
    
    if ( _strip_height != sh )
    {
        mixer_strips->redraw();
        scroll->redraw();
        _strip_height = sh;
    }
}

int
Mixer::nstrips ( void ) const
{
    return mixer_strips->children();
}

/** retrun a pointer to the track named /name/, or NULL if no track is named /name/ */
Mixer_Strip *
Mixer::track_by_name ( const char *name )
{
    for ( int i = mixer_strips->children(); i-- ; )
    {
        Mixer_Strip *t = (Mixer_Strip*)mixer_strips->child( i );

        if ( ! strcmp( name, t->name() ) )
            return t;
    }

    return NULL;
}
/** retrun a pointer to the track named /name/, or NULL if no track is named /name/ */
Mixer_Strip *
Mixer::track_by_number ( int n )
{
    if ( n < 0 || n >= mixer_strips->children() )
        return NULL;
    
    return (Mixer_Strip*)mixer_strips->child(n);
}

/** return a malloc'd string representing a unique name for a new track */
char *
Mixer::get_unique_track_name ( const char *name )
{
    char pat[256];

    strcpy( pat, name );

    for ( int i = 1; track_by_name( pat ); ++i )
        snprintf( pat, sizeof( pat ), "%s.%d", name, i );

    return strdup( pat );
}

Group *
Mixer::group_by_name ( const char *name )
{
    for ( std::list<Group*>::iterator i = groups.begin();
          i != groups.end();
          ++i )
        if ( !strcmp( (*i)->name(), name ))
            return *i;

    return NULL;
}

char *
Mixer::get_unique_group_name ( const char *name )
{
    char pat[256];

    strcpy( pat, name );

    for ( int i = 1; group_by_name( pat ); ++i )
        snprintf( pat, sizeof( pat ), "%s.%d", name, i );

    return strdup( pat );
}

void
Mixer::handle_dirty ( int d, void * )
{
    //Mixer *m = (Mixer*)v;
    if ( !nsm )
        return;
    
    if ( d == 1 )
        nsm->is_dirty();
    else if ( d == 0 )
        nsm->is_clean();
}


void
Mixer::snapshot ( void )
{
    if ( spatialization_console )
        spatialization_console->log_create();

    for ( std::list<Group*>::iterator i = groups.begin(); i != groups.end(); ++i )
        (*i)->log_create();

    for ( int i = 0; i < mixer_strips->children(); ++i )
        ((Mixer_Strip*)mixer_strips->child( i ))->log_children();
}


void
Mixer::new_strip ( void )
{
    add( new Mixer_Strip( get_unique_track_name( "Unnamed" ) ) );
}

bool
Mixer::save ( void )
{
    MESSAGE( "Saving state" );
    Loggable::snapshot_callback( &Mixer::snapshot, this );

    std::string full_path = project_directory;
    full_path += "/snapshot";

    Loggable::snapshot( full_path.c_str() );

    save_translations();

    if(!remove_custom_data_directories.empty())
    {
        for(unsigned int i = 0; i < remove_custom_data_directories.size(); ++i)
        {
            std::string remove_custom_data = "exec rm -r '";
            remove_custom_data += remove_custom_data_directories[i];
            remove_custom_data += "'";
            DMESSAGE("Remove = %s", remove_custom_data.c_str());
            system(remove_custom_data.c_str());
        }
        remove_custom_data_directories.clear();
    }
    return true;
}

static const char options_filename[] = "options";

void
Mixer::load_options ( void )
{
// save options

    /* char *path; */
    /* asprintf( &path, "%s/options", user_config_dir ); */
    /* ((Fl_Menu_Settings*)menubar)->load( menubar->find_item( "&Options" ), path ); */
    /* free( path ); */
}

void
Mixer::save_options ( void )
{
    /* char *path; */
    /* asprintf( &path, "%s/%s", user_config_dir, options_filename ); */
    /* ((Fl_Menu_Settings*)menubar)->dump( menubar->find_item( "&Options" ), path ); */
    /* free( path ); */
}

void
Mixer::update_menu ( void )
{
    project_name->label( Project::name() );

    const_cast<Fl_Menu_Item*>(menubar->find_item( "&Mixer/&Spatialization Console" ))
        ->flags = FL_MENU_TOGGLE | ( ( spatialization_console && spatialization_console->shown() ) ? FL_MENU_VALUE : 0 );
}

void
Mixer::update_window_title()
{
    std::string title = std::string(APP_NAME) + " - " + Project::name();
    window()->label(strdup(title.c_str()));
}

void
Mixer::send_feedback_cb ( void *v )
{
    Mixer *m = (Mixer*)v;
    
    m->send_feedback(false);

    /* just to it once at the start... */
    Fl::repeat_timeout( FEEDBACK_UPDATE_FREQ, send_feedback_cb, v );
}

void
Mixer::send_feedback ( bool force )
{
    for ( int i = 0; i < mixer_strips->children(); i++ )
        ((Mixer_Strip*)mixer_strips->child(i))->send_feedback(force);
}

void
Mixer::schedule_feedback ( void )
{
    for ( int i = 0; i < mixer_strips->children(); i++ )
        ((Mixer_Strip*)mixer_strips->child(i))->schedule_feedback();
}


int
Mixer::handle ( int m )
{
    /* if user presses certain keys when project is loading it can cause a crash. Don't respond to input. */
    if ( Project::is_opening_closing() )
	return 0;
    
    if ( Fl_Group::handle( m ) )
        return 1;

    switch ( m )
    {
        case FL_PASTE:
        {
            if ( ! Fl::event_inside( this ) )
                return 0;

            /* Ignore this paste if previous one is not completed */
            if ( _is_pasting )
            {
                WARNING("Previous paste not completed. SLOW DOWN!!!");
                return 0;
            }

	    DMESSAGE( "Got paste into mixer, expecting strip file..." );

            const char *text = Fl::event_text();

            char *file;

            if ( ! sscanf( text, "file://%m[^\r\n]\n", &file ) )
            {
                WARNING( "invalid drop \"%s\"\n", text );
                return 0;
            }

            /* In the case of a paste without previous copy, there may be garbage in the event_text
               buffer. In this case, the file would sometimes cause the unescape_url() function to crash.
               So if sscanf above succeeds, we check if the file string contains the substring
               "clipboard" which is the folder where copied strips are stored. No "clipboard",
                means not a valid paste path.
             */
            std::string svalid = file;
            
            if (svalid.find("clipboard") != std::string::npos)
            {
                MESSAGE( "Found clipboard!");
            }
            else
            {
                MESSAGE( "Invalid paste path, 'clipboard' not found: %s", file);
                return 0;
            }

            unescape_url( file );

            if(file)
                export_import_strip = file;

            MESSAGE( "Pasted file \"%s\"\n", export_import_strip.c_str() );

            if (! Mixer_Strip::import_strip( export_import_strip.c_str() ) )
                fl_alert( "%s", "Failed to import strip!" );

            export_import_strip = "";
            return 1;
        }
    }
    
    return 0;
}

#include <algorithm>

std::list <std::string>
Mixer::get_auto_connect_targets ( void )
{
    std::list <std::string> sl;
    std::list <std::string> rl;

    for ( int i = mixer_strips->children(); i--; )
    {
        ((Mixer_Strip*)mixer_strips->child(i))->get_output_ports(sl);
    }

    for ( std::list<std::string>::iterator i = sl.begin(); i != sl.end(); ++i )
    {
        char *s = strdup( i->c_str() );

        *rindex( s, '/' ) = 0;

        if ( !index( s, '/' ) )
        {
            char *o;
            asprintf( &o, "%s/mains", s );
            free(s);
            s = o;
        }

        if ( std::find( rl.begin(), rl.end(), s ) == rl.end() )
        {
            rl.push_back( s );
        }

        free(s);
    }

    return rl;
}

void
Mixer::auto_connect ( void )
{
    if ( Project::is_opening_closing() )
        /* it's more efficient to do this once at the end rather than as we go. */
        return;
    
    DMESSAGE("Full auto-connect cycle" );
    
    /* give strips with group affinity the first shot */
    for ( int i = 0; i < mixer_strips->children(); i++ )
    {
        Mixer_Strip *s = ((Mixer_Strip*)mixer_strips->child(i));
        
        if ( s->has_group_affinity() )
            s->auto_connect_outputs();
    }

    /* now do that catch-alls, first one wins! */
    for ( int i = 0; i < mixer_strips->children(); i++ )
    {
        Mixer_Strip *s = ((Mixer_Strip*)mixer_strips->child(i));
        
        if ( ! s->has_group_affinity() )
            s->auto_connect_outputs();
    }
}

void
Mixer::maybe_auto_connect_output ( Module::Port *p )
{
    if ( Project::is_opening_closing() )
        /* it's more efficient to do this once at the end rather than as we go. */
        return;

//    DMESSAGE( "Single output auto connect cycle" );
    
    /* give strips with group affinity the first shot */
    for ( int i = 0; i < mixer_strips->children(); i++ )
    {
        Mixer_Strip *s = ((Mixer_Strip*)mixer_strips->child(i));
        
        if ( s->has_group_affinity() )
            if ( s->maybe_auto_connect_output( p ) )
                return;
    }

    /* now do the catch-alls, first one wins! */
    for ( int i = 0; i < mixer_strips->children(); i++ )
    {
        Mixer_Strip *s = ((Mixer_Strip*)mixer_strips->child(i));
        
        if ( ! s->has_group_affinity() )
            if ( s->maybe_auto_connect_output( p ) )
                return;
    }
}
/************/
/* Commands */
/************/

void
Mixer::command_toggle_fader_view ( void )
{
   for ( int i = 0; i < mixer_strips->children(); i++ )
    {
        Mixer_Strip *s = ((Mixer_Strip*)mixer_strips->child(i));
        s->command_toggle_fader_view();
    }
}
                                 
void
Mixer::command_clear_mappings ( void )
{
    osc_endpoint->clear_translations();
}

bool
Mixer::command_save ( void )
{
    if ( ! Project::open() )
    {
        command_new();
        update_menu();
        mixer->redraw();    // To update the project label because this is a new project
    }    

    save_project_settings();

    return Project::save();
}

/** This is where we get the load file name - and tell state restore where to find */
bool
Mixer::command_load ( const char *path, const char *display_name )
{
    if(path)
        project_directory = path;

    DMESSAGE("project_directory = %s", project_directory.c_str());
    mixer->deactivate();

    Project::close();
    
    char *pwd = (char*)malloc( PATH_MAX + 1 );
    getcwd( pwd, PATH_MAX );
    chdir( path );
    load_project_settings();
    chdir( pwd );
    free( pwd );

    if ( Project::open( path ) )
    {
        project_directory = "";
        // fl_alert( "Error opening project specified on commandline: %s", Project::errstr( err ) );
        return false;
    }

    if ( display_name )
        Project::name( display_name );
    
    load_translations();

    update_menu();

    update_window_title();

    auto_connect();

    mixer->activate();

    /* Fl::remove_timeout( send_feedback_cb, this ); */
    Fl::add_timeout( FEEDBACK_UPDATE_FREQ, send_feedback_cb, this );

    return true;
}

bool
Mixer::command_new ( const char *path, const char *display_name )
{
    if ( ! Project::create( path, "" ) )
        return false;

    if(path)
        project_directory = path;

    if ( display_name )
        Project::name( display_name );

    load_project_settings();

    update_menu();

    return true;
//        fl_alert( "Error creating project!" );
}

void
Mixer::command_quit ( void )
{
    if ( !nsm->is_active() )
    {
        if ( Loggable::dirty() )
        {
            int i = fl_choice( "There have been changes since the last save."
            " Quitting now will discard them", "Discard", "Cancel", NULL );

            if ( i != 0 )
                return;
        }
    }
    quit();
}

/*  */

void
Mixer::command_add_strip ( void )
{
    new_strip();
}

void
Mixer::command_hide_gui( void )
{
    while ( Fl::first_window() )
    {
        Fl::first_window()->hide();
    }
}

void
Mixer::command_show_gui( void )
{
    window()->show();
}

