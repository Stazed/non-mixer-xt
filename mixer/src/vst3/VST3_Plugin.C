/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   VST3_Plugin.cpp
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