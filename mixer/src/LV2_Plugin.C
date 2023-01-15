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
 * File:   LV2_Plugin.C
 * Author: sspresto
 * 
 * Created on November 24, 2022, 1:36 PM
 */

#include "LV2_Plugin.H"


LV2_Plugin::LV2_Plugin ( ) : Plugin_Module( )
{
    // TODO
}

LV2_Plugin::~LV2_Plugin ( )
{
    // TODO
}

bool
LV2_Plugin::load_plugin ( const char* uri )
{
    _is_lv2 = true;
    // TODO
}

void
LV2_Plugin::init ( void )
{
    // TODO
}

void
LV2_Plugin::process ( nframes_t nframes )
{
    // TODO
}

void
LV2_Plugin::get ( Log_Entry &e ) const
{
    // TODO
}

void
LV2_Plugin::set ( Log_Entry &e )
{
    // TODO
}