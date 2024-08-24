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
 * File:   nmxt-plugin-scan.C
 * Author: sspresto
 *
 * Created on July 16, 2024, 5:40 PM
 */

#include <stdio.h>
#include <stdlib.h>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>  // fl_alert()

#include "Plugin_Scan.H"
#include "../../nonlib/debug.h"


#define USER_CONFIG_DIR NMXT_CONFIG_DIRECTORY

char *user_config_dir;

static int
ensure_dirs( void )
{
    asprintf ( &user_config_dir, "%s/.config/%s", getenv ( "HOME" ), USER_CONFIG_DIR );

    int r = mkdir ( user_config_dir, 0777 );

    return r == 0 || errno == EEXIST;
}

int
main( int /*argc*/, char** argv )
{
    if ( !ensure_dirs ( ) )
    {
        fl_alert ( "Warning! Cannot create/open user config directory! Scanning aborted..." );
        return (EXIT_SUCCESS );
    }

    std::string s_name = "";
    std::string s_type = "";
    std::string s_path = "";

    int count = 0;

    while ( *argv )
    {
        if ( count == 0 )
        {
            count++;
            s_name = *argv++;
            continue;
        }
        else if ( count == 1 )
        {
            count++;
            s_type = *argv++;
            continue;
        }
        else if ( count == 2 )
        {
            s_path = *argv++;
            continue;
        }
    }

    DMESSAGE ( "TYPE = %s: PATH = %s", s_type.c_str ( ), s_path.c_str ( ) );

    Plugin_Scan scanner;
    scanner.get_all_plugins ( s_type, s_path );

    return (EXIT_SUCCESS );
}

