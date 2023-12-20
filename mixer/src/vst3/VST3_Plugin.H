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
 * File:   VST3_Plugin.H
 * Author: sspresto
 *
 * Created on December 20, 2023, 9:24 AM
 */

#pragma once

#ifdef VST3_SUPPORT

#include "../Mixer_Strip.H"
#include "../Plugin_Module.H"
#include "../x11/X11PluginUI.H"

class VST3_Plugin : public Plugin_Module,
        private X11PluginUI::Callback
{
public:

    virtual bool load_plugin ( Module::Picked picked ) override;

    bool configure_inputs ( int ) override;
    void handle_port_connection_change ( void ) override;
    void handle_chain_name_changed ( void ) override;
    void handle_sample_rate_change ( nframes_t sample_rate ) override;
    void resize_buffers ( nframes_t buffer_size ) override;

    virtual bool bypass ( void ) const override { return *_bypass == 1.0f; }
    virtual void bypass ( bool v ) override;

    void freeze_ports ( void ) override;
    void thaw_ports ( void ) override;
    
    void configure_midi_inputs () override;
    void configure_midi_outputs () override;

    nframes_t get_module_latency ( void ) const override;
    void process ( nframes_t ) override;

    LOG_CREATE_FUNC( VST3_Plugin );
    MODULE_CLONE_FUNC( VST3_Plugin );

    VST3_Plugin();
    virtual ~VST3_Plugin();
private:
    
    
protected:
    void handlePluginUIClosed() override;
    void handlePluginUIResized(const uint width, const uint height) override;

    void get ( Log_Entry &e ) const override;
    void set ( Log_Entry &e ) override;

};

#endif // VST3_SUPPORT
