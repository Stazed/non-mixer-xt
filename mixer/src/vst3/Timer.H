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
 * File:   Timer.H
 * Author: sspresto
 *
 * Created on December 30, 2023, 7:14 AM
 */

#pragma once

#ifdef VST3_SUPPORT


class VST3_Plugin;


class Timer
{
public:

    // Constructor.
    Timer (VST3_Plugin *plug) : m_plugin(plug) {}

    // Main method.
    void start (int msecs);

    void stop();

    int interval();


private:

    // Instance members.
    VST3_Plugin *m_plugin;
};

#endif  // VST3_SUPPORT