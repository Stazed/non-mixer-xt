
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

/* Routings for opening/closing/creation of projects. All the actual
   project state belongs to Timeline and other classes. */

/* Project management routines. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <limits.h> // realpath()

#include "const.h"

#include "../../nonlib/debug.h"
#include "../../nonlib/Loggable.H"
#include "../../nonlib/file.h"

#include "Project.H"

#include <FL/filename.H>

#include "Mixer.H"

const int PROJECT_VERSION = 1;

const char *Project::_errstr[] =
{
    "Not a Non-Mixer-XT project",
    "Locked by another process",
    "Access denied",
    "Incompatible project version"
};

char Project::_name[256];
char Project::_created_on[40];
char Project::_path[512];
bool Project::_is_open = false;
bool Project::_is_opening_closing = false;
int Project::_lockfd = 0;

/***********/
/* Private */

/***********/

namespace
{

static void
copy_cstr( char *dst, size_t dst_size, const char *src )
{
    if ( !dst || dst_size == 0 )
        return;

    if ( !src )
        src = "";

    snprintf( dst, dst_size, "%s", src );
}

class Scoped_Cwd
{
    char _cwd[1024];
    bool _valid;

public:

    Scoped_Cwd ( )
        : _valid( getcwd( _cwd, sizeof( _cwd ) ) != NULL )
    {
    }

    ~Scoped_Cwd ( )
    {
        if ( _valid )
            chdir( _cwd );
    }

private:

    Scoped_Cwd( const Scoped_Cwd & );
    Scoped_Cwd & operator=( const Scoped_Cwd & );
};

static bool
make_absolute_path( const char *path, char *out, size_t out_size )
{
    if ( !path || !out || out_size == 0 )
        return false;

    char *resolved = realpath( path, out );
    if ( !resolved )
        return false;

    out[ out_size - 1 ] = '\0';
    return true;
}

static bool
read_info_pair( FILE *fp, std::string &name, std::string &value )
{
    char key[256];
    char val[1024];

    if ( !fgets( key, sizeof( key ), fp ) )
        return false;

    if ( !fgets( val, sizeof( val ), fp ) )
        return false;

    const size_t key_len = strlen( key );
    if ( key_len > 0 && key[ key_len - 1 ] == '\n' )
        key[ key_len - 1 ] = '\0';

    char *v = val;
    if ( *v == '\t' )
        ++v;

    const size_t val_len = strlen( v );
    if ( val_len > 0 && v[ val_len - 1 ] == '\n' )
        v[ val_len - 1 ] = '\0';

    name = key;
    value = v;
    return true;
}

} /* namespace */

void
Project::set_name( const char *name )
{
    std::string s = name ? name : "";

    while ( !s.empty() && s[ s.size() - 1 ] == '/' )
        s.erase( s.size() - 1 );

    const std::string::size_type pos = s.find_last_of( '/' );
    if ( pos != std::string::npos )
        s.erase( 0, pos + 1 );

    for ( std::string::size_type i = 0; i < s.size(); ++i )
    {
        if ( s[i] == '_' || s[i] == '-' )
            s[i] = ' ';
    }

    copy_cstr( Project::_name, sizeof( Project::_name ), s.c_str() );
}

void
Project::name( const char *name )
{
    copy_cstr( Project::_name, sizeof( Project::_name ), name );
}

bool
Project::write_info( void )
{
    FILE *fp;

    if ( !( fp = fopen ( "info", "w" ) ) )
    {
        WARNING ( "could not open project info file for writing." );
        return false;
    }

    char s[40];

    if ( ! *_created_on )
    {
        const time_t t = time ( NULL );
        ctime_r ( &t, s );
        s[ sizeof( s ) - 1 ] = '\0';
        const size_t len = strlen( s );
        if ( len > 0 && s[ len - 1 ] == '\n' )
            s[ len - 1 ] = '\0';
    }
    else
        copy_cstr( s, sizeof( s ), _created_on );
    
    fprintf ( fp, "created by\n\t%s\ncreated on\n\t%s\nversion\n\t%d\n",
        APP_TITLE " " VERSION,
        s,
        PROJECT_VERSION );

    fclose ( fp );

    return true;
}

bool
Project::read_info( int *version, char **creation_date, char **created_by )
{
    FILE *fp;

    if ( !( fp = fopen ( "info", "r" ) ) )
    {
        WARNING ( "could not open project info file for reading." );
        return false;
    }

    *version = 0;
    *creation_date = 0;
    *created_by = 0;

    std::string name;
    std::string value;

    while ( read_info_pair( fp, name, value ) )
    {
        MESSAGE ( "Info: %s = %s", name.c_str(), value.c_str() );

        if ( name == "version" )
            *version = atoi ( value.c_str() );
        else if ( name == "created on" )
        {
            free( *creation_date );
            *creation_date = strdup ( value.c_str() );
        }
        else if ( name == "created by" )
        {
            free( *created_by );
            *created_by = strdup ( value.c_str() );
        }
    }

    fclose ( fp );

    return true;
}

/**********/
/* Public */
/**********/

/** Save out any settings and unjournaled state... */
bool
Project::save( void )
{
    if ( !open ( ) )
        return true;

    //    tle->save_timeline_settings();

    int r = mixer->save ( );

    //    Loggable::clear_dirty();

    return r;
    //    return Loggable::save_unjournaled_state();
}

/** Close the project (reclaiming all memory) */
bool
Project::close( void )
{
    if ( !open ( ) )
        return true;

    if ( !save ( ) )
        return false;

    Project::_is_opening_closing = true;

    Loggable::close ( );
    /* //    write_info(); */

    _is_open = false;

    *Project::_name = '\0';
    *Project::_created_on = '\0';
    *Project::_path = '\0';

    release_lock ( &_lockfd, ".lock" );

    Project::_is_opening_closing = false;

    return true;
}

/** Ensure a project is valid before opening it... */
bool
Project::validate( const char *name )
{
    Scoped_Cwd restore_cwd;
    char abs_path[1024];

    if ( !make_absolute_path( name, abs_path, sizeof( abs_path ) ) )
    {
        WARNING ( "Cannot resolve project path: \"%s\": %s",
                  name ? name : "(null)", strerror( errno ) );
        return false;
    }

    if ( chdir ( abs_path ) )
    {
        WARNING ( "Cannot change to project dir \"%s\"", abs_path );
        return false;
    }

    bool r = true;

    if ( !exists ( "info" ) ||
        !exists ( "snapshot" ) )
    {
        WARNING ( "Not a Non-Mixer-XT project: \"%s\"", abs_path );
        r = false;
    }

    return r;
}

/** Try to open project /name/. Returns 0 if successful, an error code
 * otherwise */
int
Project::open( const char *name )
{
    char abs_path[1024];

    if ( !make_absolute_path( name, abs_path, sizeof( abs_path ) ) )
    {
        WARNING ( "Cannot resolve project path: \"%s\": %s",
                  name ? name : "(null)", strerror( errno ) );
        return E_INVALID;
    }

    if ( !validate ( abs_path ) )
    {
        WARNING ( "Cannot validate abs_path: \"%s\"", abs_path );
        return E_INVALID;
    }

    close ( );

    if ( chdir ( abs_path ) )
    {
        WARNING ( "Cannot chdir abs_path: \"%s\"", abs_path );
        return E_INVALID;
    }

    if ( !acquire_lock ( &_lockfd, ".lock" ) )
        return E_LOCKED;

    int version = 0;
    char *creation_date = NULL;
    char *created_by = NULL;

    if ( !read_info ( &version, &creation_date, &created_by ) )
    {
        WARNING ( "Cannot read_info: \"%s\"", abs_path );
        release_lock( &_lockfd, ".lock" );
        return E_INVALID;
    }

    if ( !created_by || strncmp ( created_by, APP_TITLE, strlen ( APP_TITLE ) ) )
    {
        free( creation_date );
        free( created_by );
        release_lock( &_lockfd, ".lock" );
        return E_INVALID;
    }

    if ( version != PROJECT_VERSION )
    {
        free( creation_date );
        free( created_by );
        release_lock( &_lockfd, ".lock" );
        return E_VERSION;
    }

    _is_opening_closing = true;

    if ( !Loggable::replay ( "snapshot" ) )
    {
        _is_opening_closing = false;
        free( creation_date );
        free( created_by );
        release_lock( &_lockfd, ".lock" );
        return E_INVALID;
    }

    if ( creation_date )
    {
        copy_cstr ( _created_on, sizeof( _created_on ), creation_date );
        free ( creation_date );
    }
    else
        *_created_on = 0;

    if ( !getcwd ( _path, sizeof ( _path ) ) )
    {
        copy_cstr( _path, sizeof( _path ), abs_path );
    }

    set_name ( _path );

    _is_open = true;

    _is_opening_closing = false;
    free( created_by );

    MESSAGE ( "Loaded project \"%s\"", name );

    return 0;
}

/** Create a new project /name/ from existing template
 * /template_name/ */
bool
Project::create( const char *name, const char *template_name )
{
    if ( exists ( name ) )
    {
        (void)template_name;
        WARNING ( "Project already exists" );
        return false;
    }

    close ( );

    if ( mkdir ( name, 0777 ) )
    {
        WARNING ( "Cannot create project directory: %s", name );
        return false;
    }

    char abs_path[1024];
    if ( !make_absolute_path( name, abs_path, sizeof( abs_path ) ) )
    {
        WARNING ( "Cannot resolve project path: \"%s\": %s",
                  name ? name : "(null)", strerror( errno ) );
        return false;
    }

    if ( chdir ( name ) )
    {
        FATAL ( "WTF? Cannot change to new project directory" );
        return false;
    }

    int ret = creat ( "snapshot", 0666 );
    if ( ret < 0 )
    {
        WARNING ( "Cannot create snapshot file: %s", strerror( errno ));
        return false;
    }

    ::close( ret );

    /* TODO: copy template */

    if ( !write_info ( ) )
        return false;

    /*
     * We are already inside the newly-created directory here.
     * Re-opening via the original relative name is incorrect.
     */
    if ( open ( abs_path ) == 0 )
    {
        //        /* add the bare essentials */
        //        timeline->beats_per_minute( 0, 120 );
        //        timeline->time( 0, 4, 4 );

        MESSAGE ( "Created project \"%s\" from template \"%s\"", name, template_name );
        return true;
    }
    else
    {
        WARNING ( "Failed to open newly created project" );
        return false;
    }
}

/** Replace the journal with a snapshot of the current state */
void
Project::compact( void )
{
    Loggable::compact ( );
}
