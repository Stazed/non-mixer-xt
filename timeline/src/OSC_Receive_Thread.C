
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

#include "OSC_Receive_Thread.H"

#include "Timeline.H"

#include <stdlib.h>
#include <unistd.h>

#include "debug.h"

#include "OSC/Endpoint.H"

extern Timeline *timeline;

OSC_Receive_Thread::OSC_Receive_Thread ( )
{
    //   _thread.init();
    _shutdown = false;
}

OSC_Receive_Thread::~OSC_Receive_Thread ( )
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
OSC_Receive_Thread::start ( )
{
    _thread.clone( &OSC_Receive_Thread::process, this );
}

void
OSC_Receive_Thread::join ( )
{
    _thread.join();
}

void
OSC_Receive_Thread::process ( void )
{
    _thread.name( "OSC_Receive" );

    DMESSAGE( "OSC Thread starting" );

    while ( !_shutdown )
    {
	timeline->osc->wait(20);        
    }

    DMESSAGE( "OSC Thread stopping." );
}

void *
OSC_Receive_Thread::process ( void *v )
{
    OSC_Receive_Thread *t = (OSC_Receive_Thread*)v;
    
    t->process();

    return NULL;
}

