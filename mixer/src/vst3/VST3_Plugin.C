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
 * File:   VST3_Plugin.C
 * Author: sspresto
 * 
 * Created on December 20, 2023, 9:24 AM
 */

#ifdef VST3_SUPPORT

#include "VST3_Plugin.H"

VST3_Plugin::VST3_Plugin() :
    Plugin_Module()
{
    _plug_type = VST3;

    log_create();
}

VST3_Plugin::~VST3_Plugin()
{
    log_destroy();
}

bool
VST3_Plugin::load_plugin ( Module::Picked picked )
{
    return false;
}

bool
VST3_Plugin::configure_inputs ( int )
{
    return false;
}

void
VST3_Plugin::handle_port_connection_change ( void )
{
    
}

void
VST3_Plugin::handle_chain_name_changed ( void )
{
    
}

void
VST3_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    
}

void
VST3_Plugin::resize_buffers ( nframes_t buffer_size )
{
    
}

void
VST3_Plugin::bypass ( bool v )
{
    
}

void
VST3_Plugin::freeze_ports ( void )
{
    
}

void
VST3_Plugin::thaw_ports ( void )
{
    
}

void
VST3_Plugin::configure_midi_inputs ()
{
    
}

void
VST3_Plugin::configure_midi_outputs ()
{
    
}

nframes_t
VST3_Plugin::get_module_latency ( void ) const
{
    return 0;
}

void
VST3_Plugin::process ( nframes_t )
{
    
}

void
VST3_Plugin::handlePluginUIClosed()
{

}

void
VST3_Plugin::handlePluginUIResized(const uint width, const uint height)
{
    
}

void
VST3_Plugin::get ( Log_Entry &e ) const
{
    
}

void
VST3_Plugin::set ( Log_Entry &e )
{
    
}

#endif  // VST3_SUPPORT