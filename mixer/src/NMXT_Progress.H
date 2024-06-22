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
 * File:   NMXT_Progress.H
 * Author: sspresto
 *
 * Created on June 22, 2024, 11:44 AM
 */

/* The entire purpose of this class is to add the handle method for Fl_Progress which does
 * not provide one itself. This is needed for the tooltip enter method to show the tooltip.
 * see https://groups.google.com/g/fltkgeneral/c/XtXU92TkDm0
 */

#ifndef NMXT_PROGRESS_H
#define NMXT_PROGRESS_H

#include <FL/Fl_Progress.H>

class NMXT_Progress : public Fl_Progress
{
    public:
    NMXT_Progress(int x, int y, int w, int h, const char *l = 0) :
        Fl_Progress(x, y, w, h, l) {}
    

    int handle(int e)
    {
        if(e == FL_ENTER || e == FL_LEAVE) return 1;
        return 0;
    }

};

#endif /* NMXT_PROGRESS_H */

