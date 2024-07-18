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
 * File:   Scanner_Window.H
 * Author: sspresto
 *
 * Created on July 17, 2024, 10:43 PM
 */

#pragma once

#include <list>
#include <FL/Fl_Box.H>

#include "Plugin_Info.H"

class Scanner_Window
{
public:
    Scanner_Window();
    virtual ~Scanner_Window();

    void get_all_plugins ();
    void close_scanner_window();
private:

    Fl_Box *_box;
    void show_scanner_window();

};


