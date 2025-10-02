/*******************************************************************************/
/* Copyright (C) 2025- Stazed                                                  */
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
 * File:   NMXT_Pack.H
 * Author: sspresto
 *
 * Created on October 1, 2025, 8:46 PM
 */

/* The entire purpose of this class is to override the resize function and set to the previous
 * functionality. The resize was added with FLTK 1.4 function which causes focus
 * and missing mouse button events when used with the module and control packs.
 */

#ifndef NMXT_Pack_H
#define NMXT_Pack_H

#include <FL/Fl_Pack.H>

class NMXT_Pack : public Fl_Pack
{
public:
  NMXT_Pack(int x,int y,int w ,int h,const char *l = 0) :
    Fl_Pack( x, y, w, h, l ) {}
  
  //  The resize override introduced with FLTK 1.4 causes problems when scrolling,
  //  i.e. the modules cannot focus and we lose mouse button events.
  //  So this class is derived to eliminate the Fl_Pack::resize() here and
  //  revert to previous behavior prior to FLTK 1.4 of using default Fl_Group::resize().
  void resize(int X, int Y, int W, int H)
  {
      Fl_Group::resize(X, Y, W, H);
  }
};

#endif
