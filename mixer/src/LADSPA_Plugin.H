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
 * File:   LADSPA_Plugin.H
 * Author: sspresto
 *
 * Created on November 24, 2022, 4:32 PM
 */

#pragma once

#include "Plugin_Module.H"


class LADSPA_Plugin : public Plugin_Module {

public:

    ImplementationData *_idata;

    bool load_plugin ( unsigned long id );

    virtual bool get_impulse_response ( sample_t *buf, nframes_t nframes );
    
    bool configure_inputs ( int );
    void handle_port_connection_change ( void );
    void resize_buffers ( nframes_t buffer_size );

    virtual bool bypass ( void ) const { return _bypass; }  // FIXME parent
    virtual void bypass ( bool v );

private:

    void init ( void );
    bool apply ( sample_t *buf, nframes_t nframes );
    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    void activate ( void );
    void deactivate ( void );
    bool loaded ( void ) const;

public:

    LADSPA_Plugin ( );
    virtual ~LADSPA_Plugin();

//    virtual nframes_t get_module_latency ( void ) const;
    virtual void process ( nframes_t );

protected:

    bool plugin_instances ( unsigned int );
    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};
