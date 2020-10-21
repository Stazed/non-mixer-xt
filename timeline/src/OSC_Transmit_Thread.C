
/*******************************************************************************/
/* Copyright (C) 2012 Jonathan Moore Liles                                     */
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

#include "OSC_Transmit_Thread.H"

#include "Timeline.H"

#include <stdlib.h>
#include <unistd.h>

#include "debug.h"

#include "OSC/Endpoint.H"

extern Timeline *timeline;

OSC_Transmit_Thread::OSC_Transmit_Thread ( )
{
    //   _thread.init();
    _shutdown = false;
}

OSC_Transmit_Thread::~OSC_Transmit_Thread ( )
{
    lock();
    if ( _shutdown == false )
    {
        _shutdown = true;
        _thread.join();
    }
    unlock();
}   
        

void
OSC_Transmit_Thread::start ( )
{
    _thread.clone( &OSC_Transmit_Thread::process, this );
}

void
OSC_Transmit_Thread::join ( )
{
    _thread.join();
}

void
OSC_Transmit_Thread::process ( void )
{
    _thread.name( "OSC_Transmit" );

    DMESSAGE( "OSC Thread starting" );

    while ( !_shutdown )
    {
	
        if ( trylock() )
        {
            timeline->process_osc();
            unlock();
        }

        usleep( 50 * 1000 );
    }

    DMESSAGE( "OSC Thread stopping." );
}

void *
OSC_Transmit_Thread::process ( void *v )
{
    OSC_Transmit_Thread *t = (OSC_Transmit_Thread*)v;
    
    t->process();

    return NULL;
}

