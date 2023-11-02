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
 * File:   CLAP_Plugin.C
 * Author: sspresto
 * 
 * Created on November 1, 2023, 7:10 PM
 */

#include "CLAP_Plugin.H"

CLAP_Plugin::CLAP_Plugin()
{
}


CLAP_Plugin::~CLAP_Plugin()
{
}

bool
CLAP_Plugin::load_plugin ( Module::Picked picked )
{
    return false;
}

bool
CLAP_Plugin::configure_inputs ( int )
{
    
}


void
CLAP_Plugin::handle_port_connection_change ( void )
{
    
}

void
CLAP_Plugin::handle_chain_name_changed ( void )
{
    
}

void
CLAP_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    
}

void
CLAP_Plugin::resize_buffers ( nframes_t buffer_size )
{
    
}

void
CLAP_Plugin::bypass ( bool v )
{
    
}

void
CLAP_Plugin::freeze_ports ( void )
{
    
}

void 
CLAP_Plugin::thaw_ports ( void )
{
    
}

void
CLAP_Plugin::configure_midi_inputs ()
{
    
}

void
CLAP_Plugin::configure_midi_outputs ()
{
    
}

nframes_t
CLAP_Plugin::get_module_latency ( void ) const
{
    
}

void
CLAP_Plugin::process ( nframes_t )
{
    
}

void
CLAP_Plugin::activate ( void )
{
    
}

void
CLAP_Plugin::deactivate ( void )
{
    
}

void
CLAP_Plugin::add_port ( const Port &p )
{
    
}

void
CLAP_Plugin::init ( void )
{
    
}

void
CLAP_Plugin::get ( Log_Entry &e ) const
{
    
}

void
CLAP_Plugin::set ( Log_Entry &e )
{
    
}