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
 * File:   Timer.C
 * Author: sspresto
 * 
 * Created on December 30, 2023, 7:14 AM
 */

#ifdef VST3_SUPPORT

#include "Timer.H"
#include "VST3_Plugin.H"

extern float f_miliseconds;

void
Timer::start (int msecs)
{
    f_miliseconds = float(msecs) *.001;

    m_plugin->add_ntk_timer();
}

void
Timer::stop()
{
    m_plugin->remove_ntk_timer();
}

int
Timer::interval()
{
    return int(f_miliseconds * 1000);
}

#endif  // VST3_SUPPORT