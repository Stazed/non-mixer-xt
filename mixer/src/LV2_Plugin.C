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
 * File:   LV2_Plugin.C
 * Author: sspresto
 * 
 * Created on November 24, 2022, 1:36 PM
 */

#include "LV2_Plugin.H"
#include <dsp.h>


#ifdef PRESET_SUPPORT
// LV2 Presets: port value setter.
static void mixer_lv2_set_port_value ( const char *port_symbol,
	void *user_data, const void *value, uint32_t size, uint32_t type )
{
    Plugin_Module *pLv2Plugin = static_cast<Plugin_Module *> (user_data);
    if (pLv2Plugin == NULL)
            return;

    const LilvPlugin *plugin = pLv2Plugin->get_slv2_plugin();
    
    if (plugin == NULL)
            return;

    if (size != sizeof(float))
            return;

    LilvWorld* world = pLv2Plugin->get_lilv_world();

    LilvNode *symbol = lilv_new_string(world, port_symbol);

    const LilvPort *port = lilv_plugin_get_port_by_symbol(plugin, symbol);

    if (port)
    {
        float paramValue = 0.0;

        switch (type)
        {
            case Plugin_Module_URI_Atom_Bool:
            CARLA_SAFE_ASSERT_RETURN(size == sizeof(int32_t),);
            paramValue = *(const int32_t*)value != 0 ? 1.0f : 0.0f;
            break;
            case Plugin_Module_URI_Atom_Double:
            CARLA_SAFE_ASSERT_RETURN(size == sizeof(double),);
            paramValue = static_cast<float>((*(const double*)value));
            break;
            case Plugin_Module_URI_Atom_Int:
            CARLA_SAFE_ASSERT_RETURN(size == sizeof(int32_t),);
            paramValue = static_cast<float>(*(const int32_t*)value);
            break;
            case Plugin_Module_URI_Atom_Float:
            CARLA_SAFE_ASSERT_RETURN(size == sizeof(float),);
            paramValue = *(const float*)value;
            break;
            case Plugin_Module_URI_Atom_Long:
            CARLA_SAFE_ASSERT_RETURN(size == sizeof(int64_t),);
            paramValue = static_cast<float>(*(const int64_t*)value);
            break;
            default:
            WARNING("(\"%s\", %p, %i, %i:\"%s\") - unknown port type",
                         port_symbol, value, size, type, pLv2Plugin->_idata->_lv2_urid_unmap(pLv2Plugin->_idata, type));
            return;
        }

        const unsigned long port_index = lilv_port_get_index(plugin, port);

       // DMESSAGE("PORT INDEX = %lu: paramValue = %f: VALUE = %p", port_index, paramValue, value);

        pLv2Plugin->set_control_value(port_index, paramValue);
    }
    
    lilv_node_free(symbol);
}
#endif


#ifdef LV2_WORKER_SUPPORT

static LV2_Worker_Status
worker_write_packet(ZixRing* const    target,
                         const uint32_t    size,
                         const void* const data)
{
    ZixRingTransaction tx = zix_ring_begin_write(target);
    if (zix_ring_amend_write(target, &tx, &size, sizeof(size)) ||
        zix_ring_amend_write(target, &tx, data, size))
    {
        return LV2_WORKER_ERR_NO_SPACE;
    }

    DMESSAGE("worker_write_packet");

    zix_ring_commit_write(target, &tx);

    return LV2_WORKER_SUCCESS;
}

static void
update_ui( void *data)
{
    Plugin_Module* plug_ui =  static_cast<Plugin_Module *> (data);
    /* Emit UI events. */
    ControlChange ev;
    const size_t  space = zix_ring_read_space( plug_ui->plugin_to_ui );
    for (size_t i = 0;
         i + sizeof(ev) < space;
         i += sizeof(ev) + ev.size)
    {
     //   DMESSAGE("Reading .plugin_events");
        /* Read event header to get the size */
        zix_ring_read( plug_ui->plugin_to_ui, (char*)&ev, sizeof(ev));

        /* Resize read buffer if necessary */
        plug_ui->ui_event_buf = realloc(plug_ui->ui_event_buf, ev.size);
        void* const buf = plug_ui->ui_event_buf;

        /* Read event body */
        zix_ring_read( plug_ui->plugin_to_ui, (char*)buf, ev.size);

        if ( plug_ui->m_ui_instance )   // Custom UI
        {
            suil_instance_port_event(plug_ui->m_ui_instance, ev.index, ev.size, ev.protocol, buf);
        }
        else    // Generic UI
        {
            plug_ui->ui_port_event( ev.index, ev.size, ev.protocol, buf );
        }
    }
}

static LV2_Worker_Status
non_worker_respond(LV2_Worker_Respond_Handle handle,
                    uint32_t                  size,
                    const void*               data)
{
    Plugin_Module* worker = static_cast<Plugin_Module *> (handle);

    DMESSAGE("non_worker_respond");
    return worker_write_packet(worker->responses, size, data);
}

static void*
worker_func(void* data)
{
    Plugin_Module* worker = static_cast<Plugin_Module *> (data);
    void*       buf    = NULL;
    while (true)
    {
        zix_sem_wait( &worker->sem );
        if ( worker->m_exit )
        {
            DMESSAGE ("EXIT");
            break;
        }

        uint32_t size = 0;
        zix_ring_read(worker->requests, (char*)&size, sizeof(size));

        // Reallocate buffer to accommodate request if necessary
        void* const new_buf = realloc(buf, size);

        if (new_buf) 
        {
            DMESSAGE("Read request into buffer");
            // Read request into buffer
            buf = new_buf;
            zix_ring_read(worker->requests, buf, size);

            // Lock and dispatch request to plugin's work handler
            zix_sem_wait(&worker->work_lock);
            
            worker->_idata->lv2.ext.worker->work(
                worker->m_instance->lv2_handle, non_worker_respond, worker, size, buf);

            zix_sem_post(&worker->work_lock);
        }
        else
        {
            // Reallocation failed, skip request to avoid corrupting ring
            zix_ring_skip(worker->requests, size);
        }
    }

    free(buf);
    return NULL;
}

LV2_Worker_Status
lv2_non_worker_schedule(LV2_Worker_Schedule_Handle handle,
                     uint32_t                   size,
                     const void*                data)
{
    Plugin_Module* worker = static_cast<Plugin_Module *> (handle);

    LV2_Worker_Status st = LV2_WORKER_SUCCESS;

    if (!worker || !size)
    {
        return LV2_WORKER_ERR_UNKNOWN;
    }

    if (worker->threaded)
    {
        DMESSAGE("worker->threaded");

        // Schedule a request to be executed by the worker thread
        if (!(st = worker_write_packet(worker->requests, size, data)))
        {
            zix_sem_post(&worker->sem);
        }
    }
    else
    {
        // Execute work immediately in this thread
        DMESSAGE("NOT threaded");
        zix_sem_wait(&worker->work_lock);

        st = worker->_idata->lv2.ext.worker->work(
        worker->m_instance->lv2_handle, non_worker_respond, worker, size, data);

        zix_sem_post(&worker->work_lock);
    }

    return st;
}

static int
patch_set_get(Plugin_Module* plugin,
              const LV2_Atom_Object* obj,
              const LV2_Atom_URID**  property,
              const LV2_Atom**       value)
{
    lv2_atom_object_get(obj, Plugin_Module_URI_patch_Property,
                        (const LV2_Atom*)property,
                        Plugin_Module_URI_patch_Value,
                        value,
                        0);
    if (!*property)
    {
        WARNING( "patch:Set message with no property" );
        return 1;
    }
    else if ((*property)->atom.type != plugin->m_forge.URID)
    {
        WARNING( "patch:Set property is not a URID" );
        return 1;
    }

    return 0;
}

static int
patch_put_get(Plugin_Module*  plugin,
              const LV2_Atom_Object*  obj,
              const LV2_Atom_Object** body)
{
    lv2_atom_object_get(obj, Plugin_Module_URI_patch_Body,
                        (const LV2_Atom*)body,
                        0);
    if (!*body)
    {
        WARNING( "patch:Put message with no body" );
        return 1;
    }
    else if (!lv2_atom_forge_is_object_type(&plugin->m_forge, (*body)->atom.type))
    {
        WARNING( "patch:Put body is not an object" );
        return 1;
    }

    return 0;
}

#endif  // LV2_WORKER_SUPPORT

#ifdef USE_SUIL
static int
x_resize(LV2UI_Feature_Handle handle, int width, int height)
{
    Plugin_Module *pLv2Plugin = static_cast<Plugin_Module *> (handle);
    if (pLv2Plugin == NULL)
        return 1;

#ifdef USE_CARLA

    XResizeWindow(pLv2Plugin->fDisplay, pLv2Plugin->fHostWindow, width, height );
    XSync(pLv2Plugin->fDisplay, False);

#endif

    DMESSAGE("X-width = %d: X-height = %d", width, height);

    return 0;
}

#ifdef LV2_EXTERNAL_UI
static void mixer_lv2_ui_closed ( LV2UI_Controller ui_controller )
{
    Plugin_Module *pLv2Plugin
            = static_cast<Plugin_Module *> (ui_controller);
    if (pLv2Plugin == nullptr)
            return;

    DMESSAGE("Closing External UI");

    // Just flag up the closure...
    pLv2Plugin->fIsVisible = false;
}
#endif // LV2_EXTERNAL_UI
#endif // USE_SUIL

static LV2_Lib_Manager lv2_lib_manager;

LV2_Plugin::LV2_Plugin ( ) : Plugin_Module( )
{
    // TODO
}

LV2_Plugin::~LV2_Plugin ( )
{
    /* This is the case when the user manually removes a Plugin. We set the
     _is_removed = true, and add any custom data directory to the remove directories
     vector. If the user saves the project then we remove any items in the vector.
     We also clear the vector. If the user abandons any changes on exit, then any
     items added to the vector since the last save will not be removed */
    if(_is_removed)
    {
        if(!m_project_directory.empty())
        {
            remove_custom_data_directories.push_back(m_project_directory);
        }
    }

#ifdef USE_SUIL
    /* In case the user left the custom ui up */
    m_exit = true;

    if (fIsVisible)
    {
        if(m_use_externalUI)
        {
            Fl::remove_timeout(&Plugin_Module::custom_update_ui, this);
            if (m_lv2_ui_widget)
                LV2_EXTERNAL_UI_HIDE((LV2_External_UI_Widget *) m_lv2_ui_widget);
        }
        else
            close_custom_ui();
    }

    if( m_use_X11_interface )
    {
        if(m_ui_instance)
        {
            suil_instance_free(m_ui_instance);
            m_ui_instance = NULL;
        }
        if(m_ui_host)
        {
            suil_host_free(m_ui_host);
            m_ui_host = NULL;
        }
    }
#endif

#ifdef LV2_WORKER_SUPPORT
    if ( _idata->lv2.ext.worker )
    {
        m_exit = true;
        non_worker_finish();
        non_worker_destroy();
    }

    zix_ring_free(plugin_to_ui);
    zix_ring_free(ui_to_plugin);
    free(ui_event_buf);
#endif

    // FIXME check this
 //   log_destroy();
 //   plugin_instances( 0 );
    
#ifdef PRESET_SUPPORT
    lilv_world_free(m_lilvWorld);
#endif
}

bool
LV2_Plugin::load_plugin ( const char* uri )
{
    _is_lv2 = true;
    _idata->lv2.rdf_data = lv2_rdf_new( uri, true );

#ifdef PRESET_SUPPORT
    PresetList = _idata->lv2.rdf_data->PresetListStructs;
    _uridMapFt =  (LV2_URID_Map*) _idata->lv2.features[Plugin_Feature_URID_Map]->data;
    _uridUnmapFt = (LV2_URID_Unmap*) _idata->lv2.features[Plugin_Feature_URID_Unmap]->data;
    LilvNode* plugin_uri = lilv_new_uri(get_lilv_world(), uri);
    m_plugin = lilv_plugins_get_by_uri(get_lilv_plugins(), plugin_uri);
    lilv_node_free(plugin_uri);
#endif

    _plugin_ins = _plugin_outs = 0;
#ifdef LV2_WORKER_SUPPORT
    _atom_ins = _atom_outs = 0;
#endif
#ifdef LV2_MIDI_SUPPORT
    _midi_ins = _midi_outs = 0;
    _position = 0;
    _bpm = 120.0f;
    _rolling = false;
    _is_instrument = false;
#endif

    /* We use custom data for instrument plugins */
   if( _idata->lv2.rdf_data->Type[1] == LV2_PLUGIN_INSTRUMENT)
   {
       _is_instrument = true;
   }

    if ( ! _idata->lv2.rdf_data )
    {
        /* unknown plugin URI */
        WARNING( "Unknown plugin URI: %s", uri );
	char s[25];

	snprintf( s, 24, "! %s", uri );
	
        base_label( s );
        return false;
    }

    _idata->lv2.descriptor = lv2_lib_manager.get_descriptor_for_uri( _idata->lv2.rdf_data->Binary, uri );

    base_label( _idata->lv2.rdf_data->Name );

    if ( _idata->lv2.descriptor )
    {
        MESSAGE( "Name: %s", _idata->lv2.rdf_data->Name );

        if ( _idata->lv2.descriptor->extension_data )
        {
            bool hasOptions = false;
            bool hasState   = false;
            bool hasWorker  = false;

            for (uint32_t i=0, count=_idata->lv2.rdf_data->ExtensionCount; i<count; ++i)
            {
                const char* const extension(_idata->lv2.rdf_data->Extensions[i]);

                /**/ if ( ! extension)
                    continue;
                else if ( ::strcmp(extension, LV2_OPTIONS__interface) == 0 )
                    hasOptions = true;
                else if ( ::strcmp(extension, LV2_STATE__interface) == 0   )
                    hasState = true;
                else if ( ::strcmp(extension, LV2_WORKER__interface) == 0  )
                    hasWorker = true;
            }

            if (hasOptions)
                _idata->lv2.ext.options = (const LV2_Options_Interface*)_idata->lv2.descriptor->extension_data(LV2_OPTIONS__interface);

            if (hasState)
                _idata->lv2.ext.state = (const LV2_State_Interface*)_idata->lv2.descriptor->extension_data(LV2_STATE__interface);

            if (hasWorker)
                _idata->lv2.ext.worker = (const LV2_Worker_Interface*)_idata->lv2.descriptor->extension_data(LV2_WORKER__interface);

            // check if invalid
            if ( _idata->lv2.ext.options != NULL && _idata->lv2.ext.options->get == NULL && _idata->lv2.ext.options->set == NULL )
                _idata->lv2.ext.options = NULL;

            if ( _idata->lv2.ext.state != NULL && ( _idata->lv2.ext.state->save == NULL || _idata->lv2.ext.state->restore == NULL ))
            {
                _idata->lv2.ext.state = NULL;
            }
#ifdef LV2_WORKER_SUPPORT
            else
            {
                m_safe_restore = true;
            }
#endif
            if ( _idata->lv2.ext.worker != NULL && _idata->lv2.ext.worker->work == NULL )
            {
                _idata->lv2.ext.worker = NULL;
            }
#ifdef LV2_WORKER_SUPPORT
            else
            {
                DMESSAGE("Setting worker initialization");

                lv2_atom_forge_init(&m_forge, _uridMapFt);
                non_worker_init(this,  _idata->lv2.ext.worker, true);

		if (m_safe_restore)   // FIXME
                {
                    MESSAGE( "Plugin Has safe_restore - TODO" );
                   // non_worker_init(this, _idata->lv2.ext.state, false);
		}
            }
#endif
        }
        else    // Extension data
        {
            _idata->lv2.ext.options = NULL;
            _idata->lv2.ext.state   = NULL;
            _idata->lv2.ext.worker  = NULL;
        }

        for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
        {
            if ( LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[i].Types ) )
            {
                if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[i].Types ) )
                {
                    add_port( Port( this, Port::INPUT, Port::AUDIO, _idata->lv2.rdf_data->Ports[i].Name ) );
                    control_input[_plugin_ins].hints.plug_port_index = i;
                    _plugin_ins++;
                }
                else if (LV2_IS_PORT_OUTPUT(_idata->lv2.rdf_data->Ports[i].Types))
                {
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO, _idata->lv2.rdf_data->Ports[i].Name ) );
                    control_output[_plugin_outs].hints.plug_port_index = i;
                    _plugin_outs++;
                }
            }
#ifdef LV2_WORKER_SUPPORT
            else if (LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[i].Types ))
            {
                if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[i].Types ) )
                {
#ifdef LV2_MIDI_SUPPORT
                    if(LV2_PORT_SUPPORTS_MIDI_EVENT(_idata->lv2.rdf_data->Ports[i].Types))
                    {
                        add_port( Port( this, Port::INPUT, Port::MIDI, _idata->lv2.rdf_data->Ports[i].Name ) );

                        _midi_ins++;

                        DMESSAGE("LV2_PORT_SUPPORTS_MIDI_EVENT = %s", _idata->lv2.rdf_data->Ports[i].Name);
                    }
                    else
                    {
#endif
                        add_port( Port( this, Port::INPUT, Port::ATOM, _idata->lv2.rdf_data->Ports[i].Name ) );

                        if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->lv2.rdf_data->Ports[i].Types ))
                        {
                            DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - INPUT ");
                            atom_input[_atom_ins].hints.type = Port::Hints::PATCH_MESSAGE;
                        }

                        DMESSAGE("GOT ATOM SEQUENCE PORT IN = %s", _idata->lv2.rdf_data->Ports[i].Name);
#ifdef LV2_MIDI_SUPPORT
                    }
#endif
                    atom_input[_atom_ins].hints.plug_port_index = i;
                    _atom_ins++;
                }
                else if (LV2_IS_PORT_OUTPUT(_idata->lv2.rdf_data->Ports[i].Types))
                {
#ifdef LV2_MIDI_SUPPORT
                    if(LV2_PORT_SUPPORTS_MIDI_EVENT(_idata->lv2.rdf_data->Ports[i].Types))
                    {
                        add_port( Port( this, Port::OUTPUT, Port::MIDI, _idata->lv2.rdf_data->Ports[i].Name ) );

                        _midi_outs++;

                        DMESSAGE("LV2_PORT_SUPPORTS_MIDI_EVENT = %s", _idata->lv2.rdf_data->Ports[i].Name);
                    }
                    else
                    {
#endif
                        add_port( Port( this, Port::OUTPUT, Port::ATOM, _idata->lv2.rdf_data->Ports[i].Name ) );

                        if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->lv2.rdf_data->Ports[i].Types ))
                        {
                            DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - OUTPUT ");
                            atom_output[_atom_outs].hints.type = Port::Hints::PATCH_MESSAGE;
                        }

                        DMESSAGE("GOT ATOM SEQUENCE PORT OUT = %s", _idata->lv2.rdf_data->Ports[i].Name);
#ifdef LV2_MIDI_SUPPORT
                    }
#endif
                    atom_output[_atom_outs].hints.plug_port_index = i;
                    _atom_outs++;
                }
            }
#endif
        }

        MESSAGE( "Plugin has %i AUDIO inputs and %i AUDIO outputs", _plugin_ins, _plugin_outs);
#ifdef LV2_WORKER_SUPPORT
        MESSAGE( "Plugin has %i ATOM inputs and %i ATOM outputs", _atom_ins, _atom_outs);
#endif
#ifdef LV2_MIDI_SUPPORT
        MESSAGE( "Plugin has %i MIDI in ports and %i MIDI out ports", _midi_ins, _midi_outs);
#endif

        if(!_plugin_ins)
            is_zero_input_synth(true);

        for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
        {
            if ( LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[i].Types ) )
            {
                const LV2_RDF_Port& rdfport ( _idata->lv2.rdf_data->Ports[i] );

                Port::Direction d = Port::INPUT;

                if ( LV2_IS_PORT_INPUT( rdfport.Types ) )
                {
                    d = Port::INPUT;
                }
                else if ( LV2_IS_PORT_OUTPUT( rdfport.Types ) )
                {
                    d = Port::OUTPUT;
                }

                Port p( this, d, Port::CONTROL, rdfport.Name );
                
                if ( LV2_HAVE_MINIMUM_PORT_POINT( rdfport.Points.Hints ) )
                {
                    p.hints.ranged = true;
                    p.hints.minimum = rdfport.Points.Minimum;
                }
                else
                {
                    p.hints.minimum = 0.0f;
                }

                if ( LV2_HAVE_MAXIMUM_PORT_POINT( rdfport.Points.Hints ) )
                {
                    p.hints.ranged = true;
                    p.hints.maximum = rdfport.Points.Maximum;
                }
                else
                {
                    // just in case
                    p.hints.maximum = p.hints.minimum + 0.1f;
                }

                if ( LV2_HAVE_DEFAULT_PORT_POINT( rdfport.Points.Hints ) )
                {
                    p.hints.default_value = rdfport.Points.Default;
                }
                else
                {
                    // just in case
                    p.hints.default_value = p.hints.minimum;
                }

                if ( LV2_IS_PORT_SAMPLE_RATE( rdfport.Properties ) )
                {
                    p.hints.minimum *= sample_rate();
                    p.hints.maximum *= sample_rate();
                    p.hints.default_value *= sample_rate();
                }

                if ( LV2_IS_PORT_INTEGER( rdfport.Properties ) )
                {
                    p.hints.type = Port::Hints::LV2_INTEGER;
                    
                    if( LV2_IS_PORT_ENUMERATION(rdfport.Properties) )
                    {
                        p.hints.type = Port::Hints::LV2_INTEGER_ENUMERATION;

                        if( rdfport.ScalePointCount )
                        {
                            for( unsigned i = 0; i < rdfport.ScalePointCount; ++i )
                            {
                                EnumeratorScalePoints item;
                                item.Label = std::to_string( (int) rdfport.ScalePoints[i].Value);
                                item.Label += " - ";

                                std::string temp = rdfport.ScalePoints[i].Label;

                                /* FLTK assumes '/' to be sub-menu, so we have to search the Label and escape it */
                                for (unsigned ii = 0; ii < temp.size(); ++ii)
                                {
                                    if ( temp[ii] == '/' )
                                    {
                                        temp.insert(ii, "\\");
                                        ++ii;
                                        continue;
                                    }
                                }

                                item.Label += temp;
                                item.Value = rdfport.ScalePoints[i].Value;
                                p.hints.ScalePoints.push_back(item);
                               // DMESSAGE("Label = %s: Value = %f", rdfport.ScalePoints[i].Label, rdfport.ScalePoints[i].Value);
                            }

                            std::sort( p.hints.ScalePoints.begin(), p.hints.ScalePoints.end(), EnumeratorScalePoints::before );

                            p.hints.minimum = p.hints.ScalePoints[0].Value;
                            p.hints.maximum = p.hints.ScalePoints[ p.hints.ScalePoints.size() - 1 ].Value;
                        }
                        else    // Integer enumerator with no scale points
                        {
                            p.hints.minimum = rdfport.Points.Minimum;
                            p.hints.maximum = rdfport.Points.Maximum;
                            
                            if ( p.hints.ranged &&
                                 0 == (int)p.hints.minimum &&
                                 1 == (int)p.hints.maximum )
                                p.hints.type = Port::Hints::BOOLEAN;
                            else
                                p.hints.type = Port::Hints::INTEGER;
                        }
                    }
                }
                /* Should always check toggled after integer since some LV2s will have both */
                if ( LV2_IS_PORT_TOGGLED( rdfport.Properties ) )
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }
                if ( LV2_IS_PORT_LOGARITHMIC( rdfport.Properties ) )
                {
                    p.hints.type = Port::Hints::LOGARITHMIC;
                }

                if ( LV2_IS_PORT_DESIGNATION_FREEWHEELING (rdfport.Designation) ||
                     LV2_IS_PORT_DESIGNATION_SAMPLE_RATE (rdfport.Designation) ||
                     LV2_IS_PORT_DESIGNATION_LATENCY (rdfport.Designation) ||
                     LV2_IS_PORT_DESIGNATION_TIME (rdfport.Designation) ||
                     LV2_IS_PORT_NOT_ON_GUI( rdfport.Properties ) )
                {
                    p.hints.visible = false;

                    if ( LV2_IS_PORT_DESIGNATION_SAMPLE_RATE (rdfport.Designation) )
                        p.hints.default_value = sample_rate();
                }
                
                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );
                
                p.hints.plug_port_index = i;

                add_port( p );

                DMESSAGE( "Plugin has control port \"%s\" (default: %f)", rdfport.Name, p.hints.default_value );
            }
        }
    }
    else
    {
        WARNING( "Failed to load plugin" );
        /* We don't need to delete anything here because the plugin module gets deleted along with
           everything else. */
        return false;
    }

    int instances = plugin_instances( 1 );

    if ( instances )
    {
        bypass( false );
    }

#ifdef LV2_WORKER_SUPPORT
    for (unsigned int i = 0; i < atom_input.size(); ++i)
    {
        set_lv2_port_properties( &atom_input[i], true );
    }
    
    for (unsigned int i = 0; i < atom_output.size(); ++i)
    {
        set_lv2_port_properties( &atom_output[i], false );
    }
    
    if ( _atom_ins || _atom_outs )
    {
        /* Not restoring state, load the plugin as a preset to get default files if any */
        if ( ! _loading_from_file  )
        {
            _loading_from_file = false;

            const LV2_URID_Map* const uridMap = (const LV2_URID_Map*)_idata->lv2.features[Plugin_Feature_URID_Map]->data;
            LilvState* const state = Lv2WorldClass::getInstance().getStateFromURI(uri, (LV2_URID_Map*) uridMap);

            /* Set any files for the plugin - no need to update control parameters since they are already set */
            lilv_state_restore(state, m_instance,  mixer_lv2_set_port_value, this, 0, _idata->lv2.features);

            lilv_state_free(state);
        }
    }
    else
        _loading_from_file = false;
#endif  // LV2_WORKER_SUPPORT

    /* We are setting the initial buffer size here because some plugins seem to need it upon instantiation -- Distrho
     The reset update is called too late so we get a crash upon the first call to run. */
    if ( _idata->lv2.ext.options && _idata->lv2.ext.options->set  )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->lv2.ext.options->set( _idata->handle[i], &(_idata->lv2.options.opts[Plugin_Module_Options::MaxBlockLenth]) );
            _idata->lv2.ext.options->set( _idata->handle[i], &(_idata->lv2.options.opts[Plugin_Module_Options::MinBlockLenth]) );
        }
    }

    if ( control_input.size() > 100  )  // FIXME find out how to determine if plugin has custom data
        _use_custom_data = true;
    else if (audio_input.empty())
        _use_custom_data = true;
    else if ( _is_instrument )
        _use_custom_data = true;

    return instances;
}

void
LV2_Plugin::init ( void )
{
    Plugin_Module::init();
  //  _latency = 0;
  //  _last_latency = 0;
    _idata = new ImplementationData();
    /* module will be bypassed until plugin is loaded */
 //   _bypass = true;
 //   _crosswire = false;
  //  _is_lv2 = false;
    m_project_directory = "";

 //   align( (Fl_Align)FL_ALIGN_CENTER | FL_ALIGN_INSIDE );
//     color( (Fl_Color)fl_color_average( FL_MAGENTA, FL_WHITE, 0.5f ) );

 //   int tw, th, tx, ty;

 //   bbox( tx, ty, tw, th );

    // init lv2 stuff
    _idata->lv2.options.maxBufferSize = buffer_size();
    _idata->lv2.options.minBufferSize = buffer_size();
    _idata->lv2.options.sampleRate    = sample_rate();
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
    LV2_URI_Map_Feature* const uriMapFt = new LV2_URI_Map_Feature;
_Pragma("GCC diagnostic pop")
    uriMapFt->callback_data             = _idata;
    uriMapFt->uri_to_id                 = ImplementationData::_lv2_uri_to_id;

    LV2_URID_Map* const uridMapFt       = new LV2_URID_Map;
    uridMapFt->handle                   = _idata;
    uridMapFt->map                      = ImplementationData::_lv2_urid_map;

    LV2_URID_Unmap* const uridUnmapFt   = new LV2_URID_Unmap;
    uridUnmapFt->handle                 = _idata;
    uridUnmapFt->unmap                  = ImplementationData::_lv2_urid_unmap;
    
#ifdef LV2_WORKER_SUPPORT
    LV2_Worker_Schedule* const m_lv2_schedule  = new LV2_Worker_Schedule;
    m_lv2_schedule->handle              = this;
    m_lv2_schedule->schedule_work       = lv2_non_worker_schedule;

    _loading_from_file = false;
    ui_event_buf     = malloc(ATOM_BUFFER_SIZE);
    zix_sem_init(&sem, 0);
    threaded = false;
    zix_sem_init(&work_lock, 1);
    m_exit = false;
    m_safe_restore = false;
#endif

#ifdef USE_SUIL
    LV2UI_Resize* const uiResizeFt = new LV2UI_Resize;
    uiResizeFt->handle             = this;
    uiResizeFt->ui_resize          = x_resize;
    m_uis                          = NULL;
    m_ui                           = NULL;
    m_ui_type                      = NULL;
#endif // USE_SUIL

    _idata->lv2.features[Plugin_Feature_BufSize_Bounded]->URI  = LV2_BUF_SIZE__boundedBlockLength;
    _idata->lv2.features[Plugin_Feature_BufSize_Bounded]->data = NULL;

    _idata->lv2.features[Plugin_Feature_BufSize_Fixed]->URI    = LV2_BUF_SIZE__fixedBlockLength;
    _idata->lv2.features[Plugin_Feature_BufSize_Fixed]->data   = NULL;

    _idata->lv2.features[Plugin_Feature_Options]->URI     = LV2_OPTIONS__options;
    _idata->lv2.features[Plugin_Feature_Options]->data    = _idata->lv2.options.opts;

    _idata->lv2.features[Plugin_Feature_URI_Map]->URI     = LV2_URI_MAP_URI;
    _idata->lv2.features[Plugin_Feature_URI_Map]->data    = uriMapFt;

    _idata->lv2.features[Plugin_Feature_URID_Map]->URI    = LV2_URID__map;
    _idata->lv2.features[Plugin_Feature_URID_Map]->data   = uridMapFt;

    _idata->lv2.features[Plugin_Feature_URID_Unmap]->URI  = LV2_URID__unmap;
    _idata->lv2.features[Plugin_Feature_URID_Unmap]->data = uridUnmapFt;
#ifdef LV2_WORKER_SUPPORT
    _idata->lv2.features[Plugin_Feature_Worker_Schedule]->URI  = LV2_WORKER__schedule;
    _idata->lv2.features[Plugin_Feature_Worker_Schedule]->data = m_lv2_schedule;

    /* Create Plugin <=> UI communication buffers */
    ui_to_plugin = zix_ring_new(ATOM_BUFFER_SIZE);
    plugin_to_ui = zix_ring_new(ATOM_BUFFER_SIZE);
    
    zix_ring_mlock(ui_to_plugin);
    zix_ring_mlock(plugin_to_ui);
#endif
#ifdef USE_SUIL
    _idata->lv2.features[Plugin_Feature_Resize]->URI  = LV2_UI__resize;
    _idata->lv2.features[Plugin_Feature_Resize]->data = uiResizeFt;
#endif
    
#ifdef PRESET_SUPPORT
    m_lilvWorld = lilv_world_new();
    lilv_world_load_all(m_lilvWorld);
    m_lilvPlugins = lilv_world_get_all_plugins(m_lilvWorld);
#endif
    
#ifdef USE_SUIL
    m_ui_host = NULL;
    m_ui_instance = NULL;
    m_use_showInterface = false;
    m_use_X11_interface = false;
#ifdef LV2_EXTERNAL_UI
    m_use_externalUI = false;
#endif
#ifdef USE_CARLA
    fDisplay = nullptr;
    fHostWindow = 0;
    fChildWindow = 0;
    fChildWindowConfigured = false;
    fChildWindowMonitoring = false; // fChildWindowMonitoring(isResizable || canMonitorChildren) // FIXME
    fIsVisible = false;
    fFirstShow = true;
    fSetSizeCalledAtLeastOnce = false;
    fIsIdling = false;
    fIsResizable = false;
    fEventProc = nullptr;
#endif  // USE_CARLA
#endif  // USE_SUIL
}

void
LV2_Plugin::process ( nframes_t nframes )
{
    handle_port_connection_change();

    if ( unlikely( bypass() ) )
    {
        /* If this is a mono to stereo plugin, then duplicate the input channel... */
        /* There's not much we can do to automatically support other configurations. */
        if ( ninputs() == 1 && noutputs() == 2 )
        {
            buffer_copy( (sample_t*)audio_output[1].buffer(), (sample_t*)audio_input[0].buffer(), nframes );
        }

        _latency = 0;
    }
    else
    {
 
#ifdef LV2_WORKER_SUPPORT

        /* Pulls any atom output from the plugin and writes to the zix buffer - to send to the UI*/
        get_atom_output_events();

        for( unsigned int i = 0; i < atom_input.size(); ++i )
        {
            if ( atom_input[i]._clear_input_buffer )
            {
               // DMESSAGE("GOT atom input clear buffer");
                atom_input[i]._clear_input_buffer = false;
                lv2_evbuf_reset(atom_input[i].event_buffer(), true);
            }

            apply_ui_events(  nframes, i );
#ifdef LV2_MIDI_SUPPORT
            /* JACK MIDI in to plugin MIDI in */
            process_midi_in_events( nframes, i );
#endif
        }
#endif

        // Run the plugin for LV2
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->lv2.descriptor->run( _idata->handle[i], nframes );
        }
            
#ifdef LV2_MIDI_SUPPORT
        /* plugin MIDI out to JACK MIDI out */
        for( unsigned int i = 0; i < atom_output.size(); ++i )
        {
            process_midi_out_events( nframes, i );
        }
#endif  // LV2_MIDI_SUPPORT

#ifdef LV2_WORKER_SUPPORT
        /* Process any worker replies. */
        if ( _idata->lv2.ext.worker)
        {
            // FIXME
            // jalv_worker_emit_responses(&jalv->state_worker, jalv->instance);
            // non_worker_emit_responses(&jalv->state_worker, jalv->instance);
            non_worker_emit_responses(m_instance);
            if ( _idata->lv2.ext.worker && _idata->lv2.ext.worker->end_run)
            {
                _idata->lv2.ext.worker->end_run(m_instance->lv2_handle);
            }
        }

        /* Read the zix buffer sent from the plugin and sends to the UI */
        update_ui( this );
#endif

        _latency = get_module_latency();
    }
}

void
LV2_Plugin::get ( Log_Entry &e ) const
{
    // TODO
}

void
LV2_Plugin::set ( Log_Entry &e )
{
    // TODO
}