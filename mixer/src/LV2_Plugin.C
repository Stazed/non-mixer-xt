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
#include "Module_Parameter_Editor.H"
#include <lv2/instance-access/instance-access.h>
#include <X11/Xatom.h>
#include <unistd.h>    // usleep()
#include "NonMixerPluginUI_X11Icon.h"

#include "../../nonlib/dsp.h"

#include "Chain.H"

class Chain;    // forward declaration

#define MSG_BUFFER_SIZE 1024

#ifdef USE_SUIL
const std::vector<std::string> v_ui_types
{
    LV2_UI__X11UI,
    LV2_UI__GtkUI,
    LV2_UI__Gtk3UI,
    LV2_UI__Qt4UI,
    LV2_UI__Qt5UI,
    LV2_UI__UI      // This s/b last or all match and crash - ??? FIXME check
};
#endif

#ifdef PRESET_SUPPORT
// LV2 Presets: port value setter.
static void mixer_lv2_set_port_value ( const char *port_symbol,
	void *user_data, const void *value, uint32_t size, uint32_t type )
{
    LV2_Plugin *pLv2Plugin = static_cast<LV2_Plugin *> (user_data);
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
            NON_SAFE_ASSERT_RETURN(size == sizeof(int32_t),);
            paramValue = *(const int32_t*)value != 0 ? 1.0f : 0.0f;
            break;
            case Plugin_Module_URI_Atom_Double:
            NON_SAFE_ASSERT_RETURN(size == sizeof(double),);
            paramValue = static_cast<float>((*(const double*)value));
            break;
            case Plugin_Module_URI_Atom_Int:
            NON_SAFE_ASSERT_RETURN(size == sizeof(int32_t),);
            paramValue = static_cast<float>(*(const int32_t*)value);
            break;
            case Plugin_Module_URI_Atom_Float:
            NON_SAFE_ASSERT_RETURN(size == sizeof(float),);
            paramValue = *(const float*)value;
            break;
            case Plugin_Module_URI_Atom_Long:
            NON_SAFE_ASSERT_RETURN(size == sizeof(int64_t),);
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

void
LV2_Plugin::set_control_value(unsigned long port_index, float value)
{
    // FIXME - use unordered_map like clap to look up, and set dirty flag here
    for ( unsigned int i = 0; i < control_input.size(); ++i )
    {
        if ( port_index == control_input[i].hints.plug_port_index )
        {
            control_input[i].control_value(value);
          //  DMESSAGE("Port Index = %d: Value = %f", port_index, value);
            break;
        }
    }
}

void 
LV2_Plugin::update_control_parameters(int choice)
{
    const Lv2WorldClass& lv2World = Lv2WorldClass::getInstance();

    DMESSAGE("PresetList[%d].URI = %s", choice, _PresetList[choice].URI);

    LilvState *state = lv2World.getStateFromURI(_PresetList[choice].URI, _uridMapFt);
    lilv_state_restore(state, _lilv_instance,  mixer_lv2_set_port_value, this, 0, NULL);

    lilv_state_free(state);
}
#endif  // PRESET_SUPPORT

#ifdef LV2_STATE_SAVE
static const void*
get_port_value(const char* port_symbol,
               void*       user_data,
               uint32_t*   size,
               uint32_t*   type)
{
    LV2_Plugin* pm =  static_cast<LV2_Plugin *> (user_data);

    const LilvPlugin *plugin = pm->get_slv2_plugin();

    if (plugin == NULL)
    {
        *size = *type = 0;
        return NULL;
    }

    LilvWorld* world = pm->get_lilv_world();

    LilvNode *symbol = lilv_new_string(world, port_symbol);

    const LilvPort *port = lilv_plugin_get_port_by_symbol(plugin, symbol);
    free(symbol);

    if (port)
    {
        const unsigned long port_index = lilv_port_get_index(plugin, port);

        for (unsigned int i = 0; i < pm->control_input.size(); ++i)
        {
            if(port_index == pm->control_input[i].hints.plug_port_index)
            {
                *size = sizeof(float);
                *type = Plugin_Module_URI_Atom_Float;
                pm->control_input[i].hints.current_value = pm->control_input[i].control_value();
                return &pm->control_input[i].hints.current_value;
            }
        }
    }

    *size = *type = 0;
    return NULL;
}
#endif  // LV2_STATE_SAVE

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
    LV2_Plugin* plug_ui =  static_cast<LV2_Plugin *> (data);
    /* Emit UI events. */
    ControlChange ev;
    const size_t  space = zix_ring_read_space( plug_ui->_plugin_to_ui );
    for (size_t i = 0;
         i + sizeof(ev) < space;
         i += sizeof(ev) + ev.size)
    {
     //   DMESSAGE("Reading .plugin_events");
        /* Read event header to get the size */
        zix_ring_read( plug_ui->_plugin_to_ui, (char*)&ev, sizeof(ev));

        /* Resize read buffer if necessary */
        plug_ui->_ui_event_buf = realloc(plug_ui->_ui_event_buf, ev.size);
        void* const buf = plug_ui->_ui_event_buf;

        /* Read event body */
        zix_ring_read( plug_ui->_plugin_to_ui, (char*)buf, ev.size);

        if ( plug_ui->_ui_instance )   // Custom UI
        {
            //DMESSAGE("SUIL INSTANCE - index = %d",ev.index);
            suil_instance_port_event(plug_ui->_ui_instance, ev.index, ev.size, ev.protocol, buf);
        }

        if( plug_ui->_editor && plug_ui->_editor->visible() )
        {
            plug_ui->ui_port_event( ev.index, ev.size, ev.protocol, buf );
        }
    }

    Fl::repeat_timeout( 0.03f, &update_ui, data );
}

static LV2_Worker_Status
non_worker_respond(LV2_Worker_Respond_Handle handle,
                    uint32_t                  size,
                    const void*               data)
{
    LV2_Plugin* worker = static_cast<LV2_Plugin *> (handle);

    DMESSAGE("non_worker_respond");
    return worker_write_packet(worker->_zix_responses, size, data);
}

static void*
worker_func(void* data)
{
    LV2_Plugin* worker = static_cast<LV2_Plugin *> (data);
    void*       buf    = NULL;
    while (true)
    {
        zix_sem_wait( &worker->_zix_sem );
        if ( worker->_exit_process )
        {
            DMESSAGE ("EXIT");
            break;
        }

        uint32_t size = 0;
        zix_ring_read(worker->_zix_requests, (char*)&size, sizeof(size));

        // Reallocate buffer to accommodate request if necessary
        void* const new_buf = realloc(buf, size);

        if (new_buf) 
        {
            DMESSAGE("Read request into buffer");
            // Read request into buffer
            buf = new_buf;
            zix_ring_read(worker->_zix_requests, buf, size);

            // Lock and dispatch request to plugin's work handler
            zix_sem_wait(&worker->_work_lock);
            
            worker->_idata->ext.worker->work(
                worker->_lilv_instance->lv2_handle, non_worker_respond, worker, size, buf);

            zix_sem_post(&worker->_work_lock);
        }
        else
        {
            // Reallocation failed, skip request to avoid corrupting ring
            zix_ring_skip(worker->_zix_requests, size);
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
    LV2_Plugin* worker = static_cast<LV2_Plugin *> (handle);

    LV2_Worker_Status st = LV2_WORKER_SUCCESS;

    if (!worker || !size)
    {
        return LV2_WORKER_ERR_UNKNOWN;
    }

    if (worker->_b_threaded)
    {
        DMESSAGE("worker->threaded");

        // Schedule a request to be executed by the worker thread
        if (!(st = worker_write_packet(worker->_zix_requests, size, data)))
        {
            zix_sem_post(&worker->_zix_sem);
        }
    }
    else
    {
        // Execute work immediately in this thread
        DMESSAGE("NOT threaded");
        zix_sem_wait(&worker->_work_lock);

        st = worker->_idata->ext.worker->work(
        worker->_lilv_instance->lv2_handle, non_worker_respond, worker, size, data);

        zix_sem_post(&worker->_work_lock);
    }

    return st;
}

char*
lv2_make_path(LV2_State_Make_Path_Handle handle, const char* path)
{
    LV2_Plugin* pm = static_cast<LV2_Plugin *> (handle);

    char *user_dir;
    if(project_directory.empty())
    {
        asprintf( &user_dir, "%s/%s/", getenv( "HOME" ), path );

        unsigned int destlen = strlen(user_dir);

        char * dst = (char *) malloc(sizeof(char) * ( destlen + 1 ) );

        fl_utf8froma(dst, destlen, user_dir, strlen(user_dir) );

        return dst;
    }
    else
    {
        std::string file = project_directory;

        if(!pm->_project_directory.empty())
            file = pm->_project_directory;

        file += "/";
        file += path;
        file += "/";

        unsigned int destlen = file.size();

        char * dst = (char *) malloc(sizeof(char) * ( destlen + 1 ) );

        fl_utf8froma(dst, destlen, file.c_str(), file.size());

        return dst;
    }
}

static int
patch_set_get(LV2_Plugin* plugin,
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
    else if ((*property)->atom.type != plugin->_atom_forge.URID)
    {
        WARNING( "patch:Set property is not a URID" );
        return 1;
    }

    return 0;
}

static int
patch_put_get(LV2_Plugin*  plugin,
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
    else if (!lv2_atom_forge_is_object_type(&plugin->_atom_forge, (*body)->atom.type))
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
    LV2_Plugin *pLv2Plugin = static_cast<LV2_Plugin *> (handle);
    if (pLv2Plugin == NULL)
        return 1;

    pLv2Plugin->_X11_UI->setSize(width, height, true, false );

    DMESSAGE("X-width = %d: X-height = %d", width, height);

    return 0;
}

#ifdef LV2_EXTERNAL_UI
static void mixer_lv2_ui_closed ( LV2UI_Controller ui_controller )
{
    LV2_Plugin *pLv2Plugin
            = static_cast<LV2_Plugin *> (ui_controller);
    if (pLv2Plugin == nullptr)
            return;

    DMESSAGE("Closing External UI");

    // Just flag up the closure...
    pLv2Plugin->_x_is_visible = false;
}
#endif // LV2_EXTERNAL_UI
#endif // USE_SUIL

static LV2_Lib_Manager lv2_lib_manager;

LV2_Plugin::LV2_Plugin ( ) :
    Plugin_Module( ),
    _idata(nullptr),
    _lilv_plugin(nullptr),
    _uridMapFt(nullptr),
    _uridUnmapFt(nullptr),
    _project_directory(),
    _atom_ins(0),
    _atom_outs(0),
    _loading_from_file(false),
    _zix_requests(nullptr),
    _zix_responses(nullptr),
    _plugin_to_ui(nullptr),
    _ui_to_plugin(nullptr),
    _ui_event_buf(nullptr),
    _worker_response(nullptr),
    _b_threaded(false),
    _exit_process(false),
    _safe_restore(false),
    _atom_buffer_size(ATOM_BUFFER_SIZE),
    _ui_host(nullptr),
    _ui_instance(nullptr),
    _use_showInterface(false),
    _use_X11_interface(false),
    _all_uis(nullptr),
    _lilv_user_interface(nullptr),
    _lilv_ui_type(nullptr),
    _use_external_ui(false),
    _lv2_ui_handle(nullptr),
    _X11_UI(nullptr),
    _x_is_resizable(false),
    _x_is_visible(false),
    _x_width(0),
    _x_height(0),
    _midi_ins(0),
    _midi_outs(0),
    _position(0),
    _bpm(120.0f),
    _rolling(false)
{
    init();
    log_create();
}

LV2_Plugin::~LV2_Plugin ( )
{
    log_destroy();

#ifdef LV2_WORKER_SUPPORT
    _exit_process = true;
    if ( _idata->ext.worker )
    {
        non_worker_finish();
        non_worker_destroy();
    }

    Fl::remove_timeout( &update_ui, this );
#endif
    /* This is the case when the user manually removes a Plugin. We set the
     _is_removed = true, and add any custom data directory to the remove directories
     vector. If the user saves the project then we remove any items in the vector.
     We also clear the vector. If the user abandons any changes on exit, then any
     items added to the vector since the last save will not be removed */
    if(_is_removed)
    {
        if(!_project_directory.empty())
        {
            remove_custom_data_directories.push_back(_project_directory);
        }
    }

#ifdef USE_SUIL
    if (_x_is_visible)
    {
        if(_use_external_ui)
        {
            Fl::remove_timeout(&LV2_Plugin::custom_update_ui, this);
            if (_lv2_ui_widget)
                LV2_EXTERNAL_UI_HIDE((LV2_External_UI_Widget *) _lv2_ui_widget);
        }
        else
            close_custom_ui();
    }

    if( _use_X11_interface )
    {
        if(_ui_instance)
        {
            suil_instance_free(_ui_instance);
            _ui_instance = NULL;
        }
        if(_ui_host)
        {
            suil_host_free(_ui_host);
            _ui_host = NULL;
        }
        
        if(_X11_UI != nullptr)
        {
            delete _X11_UI;
            _X11_UI = nullptr;
        }
    }
#endif

#ifdef LV2_WORKER_SUPPORT
    zix_ring_free(_plugin_to_ui);
    zix_ring_free(_ui_to_plugin);
    free(_ui_event_buf);
#endif

#ifdef PRESET_SUPPORT
    lilv_world_free(_lilvWorld);
#endif

#ifdef LV2_MIDI_SUPPORT
#ifdef LV2_WORKER_SUPPORT
    for ( unsigned int i = 0; i < atom_input.size(); ++i )
    {
        if(!(atom_input[i].type() == Port::MIDI))
            continue;

        if(atom_input[i].jack_port())
        {
            atom_input[i].disconnect();
            atom_input[i].jack_port()->shutdown();
            delete atom_input[i].jack_port();
        }
    } 
    for ( unsigned int i = 0; i < atom_output.size(); ++i )
    {
        if(!(atom_output[i].type() == Port::MIDI))
            continue;

        if(atom_output[i].jack_port())
        {
            atom_output[i].disconnect();
            atom_output[i].jack_port()->shutdown();
            delete atom_output[i].jack_port();
        }
    }

    atom_output.clear();
    atom_input.clear();
#endif  // LV2_WORKER_SUPPORT
#endif  // LV2_MIDI_SUPPORT

    plugin_instances( 0 );  // This must be last, or after UI destruction
}

bool
LV2_Plugin::load_plugin ( Module::Picked picked )
{
    const std::string uri = picked.s_unique_id;

    _idata->rdf_data = lv2_rdf_new( uri.c_str(), true );

    _plugin_ins = _plugin_outs = 0;

    if ( ! _idata->rdf_data )
    {
        /* unknown plugin URI */
        WARNING( "Unknown plugin URI: %s", uri.c_str() );
	char s[25];

	snprintf( s, 24, "! %s", uri.c_str() );
	
        base_label( s );

        return false;
    }

    _idata->descriptor = lv2_lib_manager.get_descriptor_for_uri( _idata->rdf_data->Binary, uri.c_str() );

    if ( _idata->descriptor == NULL )
    {
        WARNING( "Failed to load plugin" );
        /* We don't need to delete anything here because the plugin module gets deleted along with
           everything else. */
        return false;
    }

    base_label( _idata->rdf_data->Name );
    MESSAGE( "Name: %s", _idata->rdf_data->Name );

    initialize_presets(uri);    // Must initialize before control ports
    get_plugin_extensions();
    create_audio_ports();
    create_control_ports();
    create_atom_ports();
    
    MESSAGE( "Plugin has %i AUDIO inputs and %i AUDIO outputs", _plugin_ins, _plugin_outs);
#ifdef LV2_WORKER_SUPPORT
    MESSAGE( "Plugin has %i ATOM inputs and %i ATOM outputs", _atom_ins, _atom_outs);
#ifdef LV2_MIDI_SUPPORT
    MESSAGE( "Plugin has %i MIDI in ports and %i MIDI out ports", _midi_ins, _midi_outs);
#endif
#endif  // LV2_WORKER_SUPPORT

    if(!_plugin_ins)
        is_zero_input_synth(true);

    if ( control_input.size() > 50  )
        _use_custom_data = true;

#ifdef LV2_WORKER_SUPPORT
    if ( _atom_ins || _atom_outs )
    {
        _use_custom_data = true;

        /* Not restoring state, load the plugin as a preset to get default files if any */
        if ( ! _loading_from_file )
        {
            const LV2_URID_Map* const uridMap = (const LV2_URID_Map*)_idata->features[Plugin_Feature_URID_Map]->data;
            LilvState* const state = Lv2WorldClass::getInstance().getStateFromURI(uri.c_str(), (LV2_URID_Map*) uridMap);

            /* Set any files for the plugin - no need to update control parameters since they are already set */
            lilv_state_restore(state, _lilv_instance,  mixer_lv2_set_port_value, this, 0, _idata->features);

            lilv_state_free(state);
        }
    }
    else
        _loading_from_file = false;
#endif  // LV2_WORKER_SUPPORT

    int instances = plugin_instances( 1 );

    if ( instances )
    {
        bypass( false );
    }

    /* We are setting the initial buffer size here because some plugins seem to need it upon instantiation -- Distrho
     The reset update is called too late so we get a crash upon the first call to run. Need to set instances
     before this is set */
    if ( _idata->ext.options && _idata->ext.options->set  )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->ext.options->set( _idata->handle[i], &(_idata->options.opts[Plugin_Module_Options::MaxBlockLenth]) );
            _idata->ext.options->set( _idata->handle[i], &(_idata->options.opts[Plugin_Module_Options::MinBlockLenth]) );
        }
    }

    /* Read the zix buffer sent from the plugin and sends to the UI.
       This needs to have a separate timeout from custom ui since it
       can also apply to generic UI events.*/
#ifdef LV2_WORKER_SUPPORT
    Fl::add_timeout( 0.03f, &update_ui, this );
#endif

    return instances;
}

void
LV2_Plugin::get_plugin_extensions()
{
    if ( _idata->descriptor->extension_data )
    {
        bool hasOptions = false;
        bool hasState   = false;
        bool hasWorker  = false;

        for (uint32_t i=0, count=_idata->rdf_data->ExtensionCount; i<count; ++i)
        {
            const char* const extension(_idata->rdf_data->Extensions[i]);

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
            _idata->ext.options = (const LV2_Options_Interface*)_idata->descriptor->extension_data(LV2_OPTIONS__interface);

        if (hasState)
            _idata->ext.state = (const LV2_State_Interface*)_idata->descriptor->extension_data(LV2_STATE__interface);

        if (hasWorker)
            _idata->ext.worker = (const LV2_Worker_Interface*)_idata->descriptor->extension_data(LV2_WORKER__interface);

        // check if invalid
        if ( _idata->ext.options != NULL && _idata->ext.options->get == NULL && _idata->ext.options->set == NULL )
            _idata->ext.options = NULL;

        if ( _idata->ext.state != NULL && ( _idata->ext.state->save == NULL || _idata->ext.state->restore == NULL ))
        {
            _idata->ext.state = NULL;
        }
#ifdef LV2_WORKER_SUPPORT
        else
        {
            _safe_restore = true;
        }
#endif
        if ( _idata->ext.worker != NULL && _idata->ext.worker->work == NULL )
        {
            _idata->ext.worker = NULL;
        }
#ifdef LV2_WORKER_SUPPORT
        else
        {
            DMESSAGE("Setting worker initialization");

            lv2_atom_forge_init(&_atom_forge, _uridMapFt);
            non_worker_init(this,  _idata->ext.worker, true);

            if (_safe_restore)   // FIXME
            {
                DMESSAGE( "Plugin Has safe_restore - TODO" );
               // non_worker_init(this, _idata->ext.state, false);
            }
        }
#endif
    }
    else    // Extension data
    {
        _idata->ext.options = NULL;
        _idata->ext.state   = NULL;
        _idata->ext.worker  = NULL;
    }
}

void
LV2_Plugin::create_audio_ports()
{
    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_AUDIO( _idata->rdf_data->Ports[i].Types ) )
        {
            if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[i].Types ) )
            {
                add_port( Port( this, Port::INPUT, Port::AUDIO, _idata->rdf_data->Ports[i].Name ) );
                audio_input[_plugin_ins].hints.plug_port_index = i;
                _plugin_ins++;
            }
            else if (LV2_IS_PORT_OUTPUT(_idata->rdf_data->Ports[i].Types))
            {
                add_port( Port( this, Port::OUTPUT, Port::AUDIO, _idata->rdf_data->Ports[i].Name ) );
                audio_output[_plugin_outs].hints.plug_port_index = i;
                _plugin_outs++;
            }
        }
    }
}

void
LV2_Plugin::create_control_ports()
{
    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_CONTROL( _idata->rdf_data->Ports[i].Types ) )
        {
            const LV2_RDF_Port& rdfport ( _idata->rdf_data->Ports[i] );

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

            /* Used for OSC path creation unique symbol */
            p.set_symbol(rdfport.Symbol);

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

    if (bypassable()) {
        Port pb( this, Port::INPUT, Port::CONTROL, "dsp/bypass" );
        pb.hints.type = Port::Hints::BOOLEAN;
        pb.hints.ranged = true;
        pb.hints.maximum = 1.0f;
        pb.hints.minimum = 0.0f;
        pb.hints.dimensions = 1;
        pb.hints.visible = false;
        pb.hints.invisible_with_signals = true;
        pb.connect_to( _bypass );
        add_port( pb );
    }
}

void
LV2_Plugin::create_atom_ports()
{
    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
#ifdef LV2_WORKER_SUPPORT
        if (LV2_IS_PORT_ATOM_SEQUENCE ( _idata->rdf_data->Ports[i].Types ))
        {
            if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[i].Types ) )
            {
#ifdef LV2_MIDI_SUPPORT
                if(LV2_PORT_SUPPORTS_MIDI_EVENT(_idata->rdf_data->Ports[i].Types))
                {
                    add_port( Port( this, Port::INPUT, Port::MIDI, _idata->rdf_data->Ports[i].Name ) );

                    _midi_ins++;

                    DMESSAGE("LV2_PORT_SUPPORTS_MIDI_EVENT = %s", _idata->rdf_data->Ports[i].Name);
                }
                else
                {
#endif
                    add_port( Port( this, Port::INPUT, Port::ATOM, _idata->rdf_data->Ports[i].Name ) );

                    if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->rdf_data->Ports[i].Types ))
                    {
                        DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - INPUT ");
                        atom_input[_atom_ins].hints.type = Port::Hints::PATCH_MESSAGE;
                    }

                    DMESSAGE("GOT ATOM SEQUENCE PORT IN = %s", _idata->rdf_data->Ports[i].Name);
#ifdef LV2_MIDI_SUPPORT
                }
#endif
                if(LV2_PORT_SUPPORTS_TIME_POSITION(_idata->rdf_data->Ports[i].Types))
                {
                    atom_input[_atom_ins]._supports_time_position = true;
                    DMESSAGE("LV2_PORT_SUPPORTS_TIME_POSITION: index = %d", i);
                }

                atom_input[_atom_ins].hints.plug_port_index = i;
                _atom_ins++;
            }
            else if (LV2_IS_PORT_OUTPUT(_idata->rdf_data->Ports[i].Types))
            {
#ifdef LV2_MIDI_SUPPORT
                if(LV2_PORT_SUPPORTS_MIDI_EVENT(_idata->rdf_data->Ports[i].Types))
                {
                    add_port( Port( this, Port::OUTPUT, Port::MIDI, _idata->rdf_data->Ports[i].Name ) );

                    _midi_outs++;

                    DMESSAGE("LV2_PORT_SUPPORTS_MIDI_EVENT = %s", _idata->rdf_data->Ports[i].Name);
                }
                else
                {
#endif
                    add_port( Port( this, Port::OUTPUT, Port::ATOM, _idata->rdf_data->Ports[i].Name ) );

                    if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->rdf_data->Ports[i].Types ))
                    {
                        DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - OUTPUT ");
                        atom_output[_atom_outs].hints.type = Port::Hints::PATCH_MESSAGE;
                    }

                    DMESSAGE("GOT ATOM SEQUENCE PORT OUT = %s", _idata->rdf_data->Ports[i].Name);
#ifdef LV2_MIDI_SUPPORT
                }
#endif
                atom_output[_atom_outs].hints.plug_port_index = i;
                _atom_outs++;
            }
        }
#endif
    }

    for (unsigned int i = 0; i < atom_input.size(); ++i)
    {
        set_lv2_port_properties( &atom_input[i], true );
    }
    
    for (unsigned int i = 0; i < atom_output.size(); ++i)
    {
        set_lv2_port_properties( &atom_output[i], false );
    }
}

void
LV2_Plugin::initialize_presets(const std::string &uri)
{
#ifdef PRESET_SUPPORT
    _PresetList = _idata->rdf_data->PresetListStructs;
    _uridMapFt =  (LV2_URID_Map*) _idata->features[Plugin_Feature_URID_Map]->data;
    _uridUnmapFt = (LV2_URID_Unmap*) _idata->features[Plugin_Feature_URID_Unmap]->data;
    LilvNode* plugin_uri = lilv_new_uri(get_lilv_world(), uri.c_str());
    _lilv_plugin = lilv_plugins_get_by_uri(get_lilv_plugins(), plugin_uri);
    lilv_node_free(plugin_uri);
#endif
}

bool
LV2_Plugin::configure_inputs( int n )
{
    unsigned int inst = _idata->handle.size();
    
    /* The synth case - no inputs and JACK module has one */
    if( ninputs() == 0 && n == 1)
    {
        _crosswire = false;
    }
    else if ( ninputs() != n )
    {
        _crosswire = false;

        if ( 1 == n && plugin_ins() > 1 )
        {
            DMESSAGE( "Cross-wiring plugin inputs" );
            _crosswire = true;

            audio_input.clear();

            for ( int i = n; i--; )
                audio_input.push_back( Port( this, Port::INPUT, Port::AUDIO ) );
        }
        else if ( n >= plugin_ins() &&
                  ( plugin_ins() == 1 && plugin_outs() == 1 ) )
        {
            DMESSAGE( "Running multiple instances of plugin" );

            audio_input.clear();
            audio_output.clear();

            for ( int i = n; i--; )
            {
                add_port( Port( this, Port::INPUT, Port::AUDIO ) );
                add_port( Port( this, Port::OUTPUT, Port::AUDIO ) );
            }

            inst = n;
        }
        else if ( n == plugin_ins() )
        {
            DMESSAGE( "Plugin input configuration is a perfect match" );
        }
        else
        {
            DMESSAGE( "Unsupported input configuration" );
            return false;
        }
    }

    if ( loaded() )
    {
        bool b = bypass();
        if ( inst != _idata->handle.size() )
        {
            if ( !b )
                deactivate();
            
            if ( plugin_instances( inst ) )
                instances( inst );
            else
                return false;
            
            if ( !b )
                activate();
        }
    }

    return true;
}

#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
void
LV2_Plugin::configure_midi_inputs ()
{
    if(!atom_input.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < atom_input.size(); ++i )
    {
        if(!(atom_input[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += atom_input[i].name();

        DMESSAGE("CONFIGURE MIDI INPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Input, JACK::Port::MIDI );
        atom_input[i].jack_port(jack_port);

        if( !atom_input[i].jack_port()->activate() )
        {
            delete atom_input[i].jack_port();
            atom_input[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI IN port" );
            return;
        }
    }
}

void
LV2_Plugin::configure_midi_outputs ()
{
    if(!atom_output.size())
        return;

    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for( unsigned int i = 0; i < atom_output.size(); ++i )
    {
        if(!(atom_output[i].type() == Port::MIDI))
            continue;

        std::string port_name = label();

        port_name += " ";
        port_name += atom_output[i].name();

        DMESSAGE("CONFIGURE MIDI OUTPUTS = %s", port_name.c_str());
        JACK::Port *jack_port = new JACK::Port( chain()->client(), trackname, port_name.c_str(), JACK::Port::Output, JACK::Port::MIDI );
        atom_output[i].jack_port(jack_port);

        if( !atom_output[i].jack_port()->activate() )
        {
            delete atom_output[i].jack_port();
            atom_output[i].jack_port(NULL);
            WARNING( "Failed to activate JACK MIDI OUT port" );
            return;
        }
    }
}
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT

void
LV2_Plugin::handle_port_connection_change ( void )
{
//    DMESSAGE( "Connecting audio ports" );

    if ( loaded() )
    {
        if ( _crosswire )
        {
            for ( int i = 0; i < plugin_ins(); ++i )
                set_input_buffer( i, audio_input[0].buffer() );
        }
        else
        {
            for ( unsigned int i = 0; i < audio_input.size(); ++i )
                set_input_buffer( i, audio_input[i].buffer() );
        }

        for ( unsigned int i = 0; i < audio_output.size(); ++i )
            set_output_buffer( i, audio_output[i].buffer() );
    }
}

void
LV2_Plugin::handle_chain_name_changed ( )
{
    Module::handle_chain_name_changed();

    if ( ! chain()->strip()->group()->single() )
    {
#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
        for ( unsigned int i = 0; i < atom_input.size(); i++ )
        {
            if(!(atom_input[i].type() == Port::MIDI))
                continue;

            if(atom_input[i].jack_port())
            {
                atom_input[i].jack_port()->trackname( chain()->name() );
                atom_input[i].jack_port()->rename();
            }
        }
        for ( unsigned int i = 0; i < atom_output.size(); i++ )
        {
            if(!(atom_output[i].type() == Port::MIDI))
                continue;

            if(atom_output[i].jack_port())
            {
                atom_output[i].jack_port()->trackname( chain()->name() );
                atom_output[i].jack_port()->rename();
            }
        }
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT
    }
}

void
LV2_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    if ( ! _idata->rdf_data )
        return;

    _idata->options.sampleRate = sample_rate;

    if ( _idata->ext.options && _idata->ext.options->set )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            _idata->ext.options->set( _idata->handle[i], &(_idata->options.opts[Plugin_Module_Options::SampleRate]) );
    }

    unsigned int nport = 0;

    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[i].Types ) &&
             LV2_IS_PORT_CONTROL( _idata->rdf_data->Ports[i].Types ) )
        {
            if ( LV2_IS_PORT_DESIGNATION_SAMPLE_RATE( _idata->rdf_data->Ports[i].Designation ) )
            {
                control_input[nport].control_value( sample_rate );
                break;
            }
            ++nport;
        }
    }
}

void
LV2_Plugin::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );

    if ( ! _idata->rdf_data )
        return;

    _idata->options.maxBufferSize = buffer_size;
    _idata->options.minBufferSize = buffer_size;

    if ( _idata->ext.options && _idata->ext.options->set )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->ext.options->set( _idata->handle[i], &(_idata->options.opts[Plugin_Module_Options::MaxBlockLenth]) );
            _idata->ext.options->set( _idata->handle[i], &(_idata->options.opts[Plugin_Module_Options::MinBlockLenth]) );
        }
    }
}

void
LV2_Plugin::bypass ( bool v )
{
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

/* freeze/disconnect all jack ports--used when changing groups */
void
LV2_Plugin::freeze_ports ( void )
{
    Module::freeze_ports();
#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
    for ( unsigned int i = 0; i < atom_input.size(); ++i )
    {
        if(!(atom_input[i].type() == Port::MIDI))
            continue;

        if(atom_input[i].jack_port())
        {
            atom_input[i].jack_port()->freeze();
            atom_input[i].jack_port()->shutdown();
        }
    }

    for ( unsigned int i = 0; i < atom_output.size(); ++i )
    {
        if(!(atom_output[i].type() == Port::MIDI))
            continue;

        if(atom_output[i].jack_port())
        {
            atom_output[i].jack_port()->freeze();
            atom_output[i].jack_port()->shutdown();
        }
    }
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT
}

/* rename and thaw all jack ports--used when changing groups */
void
LV2_Plugin::thaw_ports ( void )
{
    Module::thaw_ports();

#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
    const char *trackname = chain()->strip()->group()->single() ? NULL : chain()->name();

    for ( unsigned int i = 0; i < atom_input.size(); ++i )
    {   
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        if(!(atom_input[i].type() == Port::MIDI))
            continue;

        if(atom_input[i].jack_port())
        {
            atom_input[i].jack_port()->client( chain()->client() );
            atom_input[i].jack_port()->trackname( trackname );
            atom_input[i].jack_port()->thaw();
        }
    }

    for ( unsigned int i = 0; i < atom_output.size(); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        if(!(atom_output[i].type() == Port::MIDI))
            continue;

        if(atom_output[i].jack_port())
        {
            atom_output[i].jack_port()->client( chain()->client() );
            atom_output[i].jack_port()->trackname( trackname );
            atom_output[i].jack_port()->thaw();
        }
    }
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT
}

bool
LV2_Plugin::plugin_instances ( unsigned int n )
{
    if ( _idata->handle.size() > n )
    {
        for ( int i = _idata->handle.size() - n; i--; )
        {
            DMESSAGE( "Destroying plugin instance" );

            LV2_Handle h = _idata->handle.back();

            if ( _idata->descriptor->deactivate )
                _idata->descriptor->deactivate( h );
            if ( _idata->descriptor->cleanup )
                _idata->descriptor->cleanup( h );

            _idata->handle.pop_back();
        }
    }
    else if ( _idata->handle.size() < n )
    {
        for ( int i = n - _idata->handle.size(); i--; )
        {
            DMESSAGE( "Instantiating plugin... with sample rate %lu", (unsigned long)sample_rate());

            void* h;

            _lilv_instance = lilv_plugin_instantiate(_lilv_plugin,  sample_rate(), _idata->features);

            if ( ! _lilv_instance )
            {
                WARNING( "Failed to instantiate plugin" );
                return false;
            }
            else
            {
                h = _lilv_instance->lv2_handle;
                _idata->descriptor = _lilv_instance->lv2_descriptor;    // probably not necessary
            }

            DMESSAGE( "Instantiated: %p", h );

            _idata->handle.push_back( h );

            DMESSAGE( "Connecting control ports..." );

            int ij = 0;
            int oj = 0;
#ifdef LV2_WORKER_SUPPORT
            int aji = 0;
            int ajo = 0;
#endif
            for ( unsigned int k = 0; k < _idata->rdf_data->PortCount; ++k )
            {
                if ( LV2_IS_PORT_CONTROL( _idata->rdf_data->Ports[k].Types ) )
                {
                    if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[k].Types ) )
                        _idata->descriptor->connect_port( h, k, (float*)control_input[ij++].buffer() );
                    else if ( LV2_IS_PORT_OUTPUT( _idata->rdf_data->Ports[k].Types ) )
                        _idata->descriptor->connect_port( h, k, (float*)control_output[oj++].buffer() );
                }
                // we need to connect non audio/control ports to NULL
                else if ( ! LV2_IS_PORT_AUDIO( _idata->rdf_data->Ports[k].Types ) &&
                         !LV2_IS_PORT_ATOM_SEQUENCE ( _idata->rdf_data->Ports[k].Types ))
                    _idata->descriptor->connect_port( h, k, NULL );

#ifdef LV2_WORKER_SUPPORT
                if (LV2_IS_PORT_ATOM_SEQUENCE ( _idata->rdf_data->Ports[k].Types ))
                {
                    if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[k].Types ) )
                    {
                        if ( atom_input[aji].event_buffer() )
                            lv2_evbuf_free( atom_input[aji].event_buffer() );

                        const size_t buf_size = get_atom_buffer_size(k);
                        DMESSAGE("Atom IN buffer size = %d", buf_size);

                        atom_input[aji].event_buffer(
                                lv2_evbuf_new(buf_size,
                                Plugin_Module_URI_Atom_Chunk,
                                Plugin_Module_URI_Atom_Sequence)
                        );

                        _idata->descriptor->connect_port( h, k, lv2_evbuf_get_buffer(atom_input[aji].event_buffer()));

                        DMESSAGE("ATOM IN event_buffer = %p", lv2_evbuf_get_buffer(atom_input[aji].event_buffer()));

                        /* This sets the capacity */
                        lv2_evbuf_reset(atom_input[aji].event_buffer(), true);

                        aji++;
                    }
                    else if ( LV2_IS_PORT_OUTPUT( _idata->rdf_data->Ports[k].Types ) )
                    {

                        if ( atom_output[ajo].event_buffer() )
                            lv2_evbuf_free( atom_output[ajo].event_buffer() );

                        const size_t buf_size = get_atom_buffer_size(k);
                        DMESSAGE("Atom OUT buffer size = %d", buf_size);

                        atom_output[ajo].event_buffer(
                                lv2_evbuf_new(buf_size,
                                Plugin_Module_URI_Atom_Chunk,
                                Plugin_Module_URI_Atom_Sequence)
                        );

                        _idata->descriptor->connect_port( h, k, lv2_evbuf_get_buffer( atom_output[ajo].event_buffer() ));

                        /* This sets the capacity */
                        lv2_evbuf_reset(atom_output[ajo].event_buffer(), false);

                        DMESSAGE("ATOM OUT event_buffer = %p", lv2_evbuf_get_buffer( atom_output[ajo].event_buffer() ) );

                        ajo++;
                    }
                }
#endif
            }

            // connect ports to magic bogus value to aid debugging.
            for ( unsigned int k = 0; k < _idata->rdf_data->PortCount; ++k )
                if ( LV2_IS_PORT_AUDIO( _idata->rdf_data->Ports[k].Types ) )
                    _idata->descriptor->connect_port( h, k, (float*)0x42 );
        }
    }

    /* Create Plugin <=> UI communication buffers */
#ifdef LV2_WORKER_SUPPORT
    _ui_event_buf     = malloc(_atom_buffer_size);
    _ui_to_plugin = zix_ring_new(NULL, _atom_buffer_size);
    _plugin_to_ui = zix_ring_new(NULL, _atom_buffer_size);

    zix_ring_mlock(_ui_to_plugin);
    zix_ring_mlock(_plugin_to_ui);
#endif
    return true;
}

void
LV2_Plugin::save_LV2_plugin_state(const std::string &directory)
{
    DMESSAGE("Saving plugin state to %s", directory.c_str());

    LilvState* const state =
        lilv_state_new_from_instance(_lilv_plugin,
                                 _lilv_instance,
                                 _uridMapFt,
                                 NULL,
                                 directory.c_str(),
                                 directory.c_str(),
                                 directory.c_str(),
                                 get_port_value,
                                 this,
                                 LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
                                 NULL);

    lilv_state_save(
        _lilvWorld, _uridMapFt, _uridUnmapFt, state, NULL, directory.c_str(), "state.ttl");

    lilv_state_free(state);
}

void
LV2_Plugin::restore_LV2_plugin_state(const std::string &directory)
{
    std::string path = directory;
    path.append("/state.ttl");

    LilvState* state      = NULL;

    state = lilv_state_new_from_file(_lilvWorld, _uridMapFt, NULL, path.c_str());

    if (!state)
    {
        WARNING("Failed to load state from %s", path.c_str());
        return;
    }

    DMESSAGE("Restoring plugin state from %s", path.c_str());

    lilv_state_restore(state, _lilv_instance,  mixer_lv2_set_port_value, this, 0, _idata->features);

    lilv_state_free(state);
}

void
LV2_Plugin::handlePluginUIClosed()
{
    hide_custom_ui();
}

void
LV2_Plugin::handlePluginUIResized(const uint width, const uint height)
{
    DMESSAGE("Handle Resized W = %d: H = %d", width, height);

    if (_x_width != width || _x_height != height)
    {
        _x_width = width;
        _x_height = height;
        _X11_UI->setSize(width, height, true, false);
    }
}

void
LV2_Plugin::set_input_buffer ( int n, void *buf )
{
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
    {
        h = _idata->handle[0];
    }

    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_INPUT( _idata->rdf_data->Ports[i].Types ) &&
             LV2_IS_PORT_AUDIO( _idata->rdf_data->Ports[i].Types ) )
        {
            if ( n-- == 0 )
                _idata->descriptor->connect_port( h, i, (float*)buf );
        }
    }
}

void
LV2_Plugin::set_output_buffer ( int n, void *buf )
{
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
        h = _idata->handle[0];

    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_OUTPUT( _idata->rdf_data->Ports[i].Types ) &&
            LV2_IS_PORT_AUDIO( _idata->rdf_data->Ports[i].Types ) )
        {
            if ( n-- == 0 )
                _idata->descriptor->connect_port( h, i, (float*)buf );
        }
    }
}

bool
LV2_Plugin::loaded ( void ) const
{
    return _idata->handle.size() > 0 && ( _idata->rdf_data && _idata->descriptor);
}

void
LV2_Plugin::activate ( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );

    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

    if ( _idata->descriptor->activate )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->descriptor->activate( _idata->handle[i] );
        }
    }

    *_bypass = 0.0f;

    if ( chain() )
        chain()->client()->unlock();
}

void
LV2_Plugin::deactivate( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    *_bypass = 1.0f;

    if ( _idata->descriptor->deactivate )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->descriptor->deactivate( _idata->handle[i] );
        }
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
LV2_Plugin::add_port ( const Port &p )
{
    Module::add_port(p);
#ifdef LV2_WORKER_SUPPORT
    if ( p.type() == Port::ATOM && p.direction() == Port::INPUT )
        atom_input.push_back( p );
    else if ( p.type() == Port::ATOM && p.direction() == Port::OUTPUT )
        atom_output.push_back( p );

#ifdef LV2_MIDI_SUPPORT
    else if ( p.type() == Port::MIDI && p.direction() == Port::INPUT )
        atom_input.push_back( p );
    else if ( p.type() == Port::MIDI && p.direction() == Port::OUTPUT )
        atom_output.push_back( p );
#endif  // LV2_MIDI_SUPPORT
#endif  // LV2_WORKER_SUPPORT
}

void
LV2_Plugin::init ( void )
{
    _plug_type = LV2;

    _idata = new ImplementationData();

    _idata->options.maxBufferSize = buffer_size();
    _idata->options.minBufferSize = buffer_size();
    _idata->options.sampleRate    = sample_rate();
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
    LV2_State_Make_Path* const nonMakePath  = new LV2_State_Make_Path;
    nonMakePath->handle                 = this;
    nonMakePath->path                   = lv2_make_path;

    LV2_Worker_Schedule* const m_lv2_schedule  = new LV2_Worker_Schedule;
    m_lv2_schedule->handle              = this;
    m_lv2_schedule->schedule_work       = lv2_non_worker_schedule;

    zix_sem_init(&_zix_sem, 0);
    zix_sem_init(&_work_lock, 1);
#endif

#ifdef USE_SUIL
    LV2UI_Resize* const uiResizeFt = new LV2UI_Resize;
    uiResizeFt->handle             = this;
    uiResizeFt->ui_resize          = x_resize;
#endif // USE_SUIL

    _idata->features[Plugin_Feature_BufSize_Bounded]->URI  = LV2_BUF_SIZE__boundedBlockLength;
    _idata->features[Plugin_Feature_BufSize_Bounded]->data = NULL;

    _idata->features[Plugin_Feature_BufSize_Fixed]->URI    = LV2_BUF_SIZE__fixedBlockLength;
    _idata->features[Plugin_Feature_BufSize_Fixed]->data   = NULL;

    _idata->features[Plugin_Feature_Options]->URI     = LV2_OPTIONS__options;
    _idata->features[Plugin_Feature_Options]->data    = _idata->options.opts;

    _idata->features[Plugin_Feature_URI_Map]->URI     = LV2_URI_MAP_URI;
    _idata->features[Plugin_Feature_URI_Map]->data    = uriMapFt;

    _idata->features[Plugin_Feature_URID_Map]->URI    = LV2_URID__map;
    _idata->features[Plugin_Feature_URID_Map]->data   = uridMapFt;

    _idata->features[Plugin_Feature_URID_Unmap]->URI  = LV2_URID__unmap;
    _idata->features[Plugin_Feature_URID_Unmap]->data = uridUnmapFt;
#ifdef LV2_WORKER_SUPPORT
    _idata->features[Plugin_Feature_Make_path]->URI   = LV2_STATE__makePath;
    _idata->features[Plugin_Feature_Make_path]->data  = nonMakePath;

    _idata->features[Plugin_Feature_Worker_Schedule]->URI  = LV2_WORKER__schedule;
    _idata->features[Plugin_Feature_Worker_Schedule]->data = m_lv2_schedule;
#endif

#ifdef USE_SUIL
    _idata->features[Plugin_Feature_Resize]->URI  = LV2_UI__resize;
    _idata->features[Plugin_Feature_Resize]->data = uiResizeFt;
#endif
    
#ifdef PRESET_SUPPORT
    _lilvWorld = lilv_world_new();
    lilv_world_load_all(_lilvWorld);
    _lilvPlugins = lilv_world_get_all_plugins(_lilvWorld);
#endif
}

#ifdef LV2_WORKER_SUPPORT
void
LV2_Plugin::non_worker_init(LV2_Plugin* plug,
                 const LV2_Worker_Interface* iface,
                 bool                        threaded)
{
    DMESSAGE("Threaded = %d", threaded);
    plug->_idata->ext.worker = iface;
    plug->_b_threaded = threaded;

    if (threaded)
    {
        zix_thread_create(&plug->_zix_thread, ATOM_BUFFER_SIZE, worker_func, plug);
        plug->_zix_requests = zix_ring_new(NULL, ATOM_BUFFER_SIZE);
        zix_ring_mlock(plug->_zix_requests);
    }
    
    plug->_zix_responses = zix_ring_new(NULL, ATOM_BUFFER_SIZE);
    plug->_worker_response = malloc(ATOM_BUFFER_SIZE);
    zix_ring_mlock(plug->_zix_responses);
}

void
LV2_Plugin::non_worker_emit_responses( LilvInstance* instance)
{
    if (_zix_responses)
    {
        static const uint32_t size_size = (uint32_t)sizeof(uint32_t);

        uint32_t size = 0U;
        while (zix_ring_read(_zix_responses, &size, size_size) == size_size)
        {
            if (zix_ring_read(_zix_responses, _worker_response, size) == size)
            {
                DMESSAGE("Got work response");
                _idata->ext.worker->work_response(
                    instance->lv2_handle, size, _worker_response);
            }
        }
    }
}

void
LV2_Plugin::non_worker_finish( void )
{
    if (_b_threaded) 
    {
        zix_sem_post(&_zix_sem);
        zix_thread_join(_zix_thread);
    }
}

void
LV2_Plugin::non_worker_destroy( void )
{
    if (_zix_requests) 
    {
        if (_b_threaded)
        {
            zix_ring_free(_zix_requests);
        }

        zix_ring_free(_zix_responses);
        free(_worker_response);
    }
}

void
LV2_Plugin::ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer )
{
    /* The incoming port_index is the index from the plugin .ttl port order.
       We need the corresponding atom_input index so we have to look up
       the saved port_index in hints.plug_port_index - ai == atom_input index */
    unsigned int ai = 0;    // used by set_file()
    for (unsigned int i = 0; i < atom_input.size(); ++i)
    {
        if ( atom_input[i].hints.plug_port_index == port_index )
        {
            ai = i;
            break;
        }
    }

    const LV2_Atom* atom = (const LV2_Atom*)buffer;
    if (lv2_atom_forge_is_object_type(&_atom_forge, atom->type))
    {
//        updating = true;  // FIXME
        const LV2_Atom_Object* obj = (const LV2_Atom_Object*)buffer;
        if (obj->body.otype ==  Plugin_Module_URI_patch_Set)
        {
            const LV2_Atom_URID* property = NULL;
            const LV2_Atom*      value    = NULL;
            if (!patch_set_get(this, obj, &property, &value))
            {
                DMESSAGE("To set_file(): atom_input_index = %u: Value received = %s",ai , (char *) (value + 1) );
                set_file ((char *) (value + 1), ai, false ); // false means don't update the plugin, since this comes from the plugin
            }
        } 
        else if (obj->body.otype == Plugin_Module_URI_patch_Put)
        {
            const LV2_Atom_Object* body = NULL;
            if (!patch_put_get(this, obj, &body))
            {
                LV2_ATOM_OBJECT_FOREACH(body, prop)
                {
                    //property_changed(jalv, prop->key, &prop->value);  // FIXME
                }
            }
        }
        else
        {
            WARNING("Unknown object type = %d: ID = %d?", obj->body.otype, obj->body.id);
        }
      //  updating = false; // FIXME
    }
}

void
LV2_Plugin::send_atom_to_plugin( uint32_t port_index, uint32_t buffer_size, const void* buffer)
{
    if(_exit_process)
        return;

    const LV2_Atom* const atom = (const LV2_Atom*)buffer;
    if (buffer_size < sizeof(LV2_Atom))
    {
        WARNING("UI wrote impossible atom size");
    }
    else if (sizeof(LV2_Atom) + atom->size != buffer_size)
    {
        WARNING("UI wrote corrupt atom size");
    }
    else
    {
        write_atom_event(_ui_to_plugin, port_index, atom->size, atom->type, atom + 1U);
    }
}

static int
write_control_change(   ZixRing* const    target,
                        const void* const header,
                        const uint32_t    header_size,
                        const void* const body,
                        const uint32_t    body_size)
{
    ZixRingTransaction tx = zix_ring_begin_write(target);
    if (zix_ring_amend_write(target, &tx, header, header_size) ||
        zix_ring_amend_write(target, &tx, body, body_size))
    {
        WARNING( "UI => Plugin or Plugin => UI buffer overflow" );
        return -1;
    }

    zix_ring_commit_write(target, &tx);

    return 0;
}

int
LV2_Plugin::write_atom_event(ZixRing* target, const uint32_t    port_index,
                 const uint32_t    size,
                 const LV2_URID    type,
                 const void* const body)
{
    typedef struct {
    ControlChange change;
    LV2_Atom      atom;
    } Header;

    const Header header = {
    {port_index, Plugin_Module_URI_Atom_eventTransfer, (uint32_t) sizeof(LV2_Atom) + size},
    {size, type}};

   // DMESSAGE("SENDING ATOM TO PLUGIN - %d", header.atom.type);
    return write_control_change(target, &header, sizeof(header), body, size);
}

size_t
LV2_Plugin::get_atom_buffer_size(int port_index)
{
    const LilvPort* lilv_port = lilv_plugin_get_port_by_index(_lilv_plugin, port_index);
    const LilvNode * minimumSize = lilv_new_uri(_lilvWorld, LV2_RESIZE_PORT__minimumSize);

    LilvNode* min_size = lilv_port_get(_lilv_plugin, lilv_port, minimumSize);

    size_t buf_size = ATOM_BUFFER_SIZE;

    if (min_size && lilv_node_is_int(min_size))
    {
        buf_size = lilv_node_as_int(min_size);
        buf_size = buf_size * N_BUFFER_CYCLES;
        
        _atom_buffer_size =  _atom_buffer_size > buf_size ? _atom_buffer_size : buf_size;
    }

    lilv_node_free(min_size);

    return _atom_buffer_size;
}

void
LV2_Plugin::send_file_to_plugin( int port, const std::string &filename )
{
    DMESSAGE("File = %s", filename.c_str());

    /* Set the file for non-mixer-xt here - may be redundant some times */
    atom_input[port]._file = filename;

    uint32_t size = filename.size() + 1;

    // Copy forge since it is used by process thread
    LV2_Atom_Forge       forge =  this->_atom_forge;
    LV2_Atom_Forge_Frame frame;
    uint8_t              buf[1024];
    lv2_atom_forge_set_buffer(&forge, buf, sizeof(buf));

    lv2_atom_forge_object(&forge, &frame, 0, Plugin_Module_URI_patch_Set );
    lv2_atom_forge_key(&forge, Plugin_Module_URI_patch_Property );
    lv2_atom_forge_urid(&forge, atom_input[port]._property_mapped);
    lv2_atom_forge_key(&forge, Plugin_Module_URI_patch_Value);
    lv2_atom_forge_atom(&forge, size, this->_atom_forge.Path);
    lv2_atom_forge_write(&forge, (const void*)filename.c_str(), size);

    const LV2_Atom* atom = lv2_atom_forge_deref(&forge, frame.ref);

    /* Use the .ttl plugin index, not our internal index */
    int index = atom_input[port].hints.plug_port_index;

    write_atom_event(_ui_to_plugin, index, atom->size, atom->type, atom + 1U);
}

void
LV2_Plugin::apply_ui_events( uint32_t nframes )
{
    ControlChange ev  = {0U, 0U, 0U};
    const size_t  space = zix_ring_read_space(_ui_to_plugin);

    for (size_t i = 0; i < space; i += sizeof(ev) + ev.size)
    {
        DMESSAGE("APPLY UI");
        if(zix_ring_read(_ui_to_plugin, (char*)&ev, sizeof(ev)) != sizeof(ev))
        {
            WARNING("Failed to read header from UI ring buffer\n");
            break;
        }

        struct
        {
            union
            {
              LV2_Atom atom;
              float    control;
            } head;
            uint8_t body[MSG_BUFFER_SIZE];
        } buffer;

        if (zix_ring_read(_ui_to_plugin, &buffer, ev.size) != ev.size)
        {
            WARNING("Failed to read from UI ring buffer\n");
            break;
        }

        if (ev.protocol == Plugin_Module_URI_Atom_eventTransfer)
        {
            for(unsigned int port = 0; port < atom_input.size(); ++port)
            {
                /* Check if we are sending this to the correct atom port */
                if(atom_input[port].hints.plug_port_index == ev.index)
                {
                    LV2_Evbuf_Iterator    e    = lv2_evbuf_end( atom_input[port].event_buffer() ); 
                        const LV2_Atom* const atom = &buffer.head.atom;

                    DMESSAGE("LV2 ATOM eventTransfer atom type = %d: port = %d: index = %d", atom->type, port, ev.index);
                    DMESSAGE("atom_input[port].hints.plug_port_index = %d", atom_input[port].hints.plug_port_index);
                        lv2_evbuf_write(&e, nframes, 0, atom->type, atom->size,
                                        (const uint8_t*)LV2_ATOM_BODY_CONST(atom));

                    atom_input[port]._clear_input_buffer = true;
                    break;
                }
            }
        }
        else
        {
            WARNING("Unknown control change protocol %u", ev.protocol);
        }
    }
}

#ifdef LV2_MIDI_SUPPORT
void
LV2_Plugin::process_atom_in_events( uint32_t nframes, unsigned int port )
{
    LV2_Evbuf_Iterator iter = lv2_evbuf_begin(atom_input[port].event_buffer());

    if ( atom_input[port]._supports_time_position )
    {
        // Get Jack transport position
        jack_position_t pos;
        const bool rolling =
            (chain()->client()->transport_query(&pos) == JackTransportRolling);

        // If transport state is not as expected, then something has changed
        const bool has_bbt = (pos.valid & JackPositionBBT);
        const bool xport_changed =
          (rolling != _rolling || pos.frame != _position ||
           (has_bbt && pos.beats_per_minute != _bpm));

        uint8_t   pos_buf[256];
        LV2_Atom* lv2_pos = (LV2_Atom*)pos_buf;
        if (xport_changed)
        {
            // Build an LV2 position object to report change to plugin
            lv2_atom_forge_set_buffer(&_atom_forge, pos_buf, sizeof(pos_buf));
            LV2_Atom_Forge*      forge = &_atom_forge;
            LV2_Atom_Forge_Frame frame;
            lv2_atom_forge_object(forge, &frame, 0, Plugin_Module_URI_time_Position);
            lv2_atom_forge_key(forge, Plugin_Module_URI_time_frame);
            lv2_atom_forge_long(forge, pos.frame);
            lv2_atom_forge_key(forge, Plugin_Module_URI_time_speed);
            lv2_atom_forge_float(forge, rolling ? 1.0 : 0.0);

            if (has_bbt)
            {
                lv2_atom_forge_key(forge, Plugin_Module_URI_time_barBeat);
                lv2_atom_forge_float(forge,
                                     pos.beat - 1 + (pos.tick / pos.ticks_per_beat));
                lv2_atom_forge_key(forge, Plugin_Module_URI_time_bar);
                lv2_atom_forge_long(forge, pos.bar - 1);
                lv2_atom_forge_key(forge, Plugin_Module_URI_time_beatUnit);
                lv2_atom_forge_int(forge, pos.beat_type);
                lv2_atom_forge_key(forge, Plugin_Module_URI_time_beatsPerBar);
                lv2_atom_forge_float(forge, pos.beats_per_bar);
                lv2_atom_forge_key(forge, Plugin_Module_URI_time_beatsPerMinute);
                lv2_atom_forge_float(forge, pos.beats_per_minute);
            }
        }

        // Update transport state to expected values for next cycle
        _position = rolling ? pos.frame + nframes : pos.frame;
        _bpm      = has_bbt ? pos.beats_per_minute : _bpm;
        _rolling  = rolling;

        // Write transport change event if applicable
        if (xport_changed)
        {
          lv2_evbuf_write(
            &iter, 0, 0, lv2_pos->type, lv2_pos->size, LV2_ATOM_BODY(lv2_pos));
        }
        atom_input[port]._clear_input_buffer = true;
    }

    /* Process any MIDI events from jack */
    if ( atom_input[port].jack_port())
    {
        void *buf = atom_input[port].jack_port()->buffer( nframes );

        for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i)
        {
          //  DMESSAGE( "Got midi input!" );
            jack_midi_event_t ev;
            jack_midi_event_get(&ev, buf, i);
            lv2_evbuf_write(
              &iter, ev.time, 0, Plugin_Module_URI_Midi_event, ev.size, ev.buffer);
        }
        atom_input[port]._clear_input_buffer = true;
    }
   // DMESSAGE("ATOM PORT number = %d", port);
   // atom_input[port]._clear_input_buffer = true;
}

void
LV2_Plugin::process_atom_out_events( uint32_t nframes, unsigned int port )
{
    void* buf = NULL;

    if ( atom_output[port].jack_port() )
    {
        buf = atom_output[port].jack_port()->buffer( nframes );
        jack_midi_clear_buffer(buf);
    }

    for (LV2_Evbuf_Iterator i = lv2_evbuf_begin(atom_output[port].event_buffer());
         lv2_evbuf_is_valid(i);
         i = lv2_evbuf_next(i))
    {
        // Get event from LV2 buffer
        uint32_t frames    = 0;
        uint32_t subframes = 0;
        LV2_URID type      = 0;
        uint32_t size      = 0;
        void*    body      = NULL;
        lv2_evbuf_get(i, &frames, &subframes, &type, &size, &body);
        
        if (buf && type == Plugin_Module_URI_Midi_event)
        {
            DMESSAGE("Write MIDI event to Jack output");
            jack_midi_event_write(buf, frames, (jack_midi_data_t*) body, size);
        }

        if( (_ui_instance && _x_is_visible) || (_editor && _editor->visible()) )
        {
            DMESSAGE("SEND to UI index = %d", atom_output[port].hints.plug_port_index);
            write_atom_event(_plugin_to_ui, atom_output[port].hints.plug_port_index, size, type, body);
        }
    }

    lv2_evbuf_reset(atom_output[port].event_buffer(), false);
}

#endif  // LV2_MIDI_SUPPORT

void
LV2_Plugin::set_lv2_port_properties (Port * port, bool writable )
{
    const LilvPlugin* plugin         = _lilv_plugin;
    LilvWorld*        world          = _lilvWorld;
    LilvNode*         patch_writable = lilv_new_uri(world, LV2_PATCH__writable);
    LilvNode*         patch_readable = lilv_new_uri(world, LV2_PATCH__readable);

    // This checks for control type -- in our case the patch__writable is returned as properties
    LilvNodes* properties = lilv_world_find_nodes(
    world,
    lilv_plugin_get_uri(plugin),
    writable ? patch_writable : patch_readable,
    NULL);
    
    if( ! properties )
    {
        DMESSAGE("Atom port has no properties");
        port->hints.visible = false;
        lilv_nodes_free(properties);
        lilv_node_free(patch_readable);
        lilv_node_free(patch_writable);
        return;
    }

    LILV_FOREACH(nodes, p, properties)
    {
        const LilvNode* property = lilv_nodes_get(properties, p);

        DMESSAGE("Property = %s", lilv_node_as_string(property));

        if (lilv_world_ask(world,
                        lilv_plugin_get_uri(plugin),
                        writable ? patch_writable : patch_readable,
                        property))
        {
            port->_property = property;
            break;
        }
    }
    
    LilvNode* rdfs_label;
    rdfs_label = lilv_new_uri(world, LILV_NS_RDFS "label");
    
    port->_lilv_label  = lilv_world_get(world, port->_property, rdfs_label, NULL);
    port->_lilv_symbol = lilv_world_get_symbol(world, port->_property);
    port->_property_mapped = _idata->_lv2_urid_map(_idata, lilv_node_as_uri( port->_property ));
    
    DMESSAGE("Properties label = %s", lilv_node_as_string(port->_lilv_label));
    DMESSAGE("Properties symbol = %s", lilv_node_as_string(port->_lilv_symbol));
    DMESSAGE("Property mapped = %u", port->_property_mapped);

    lilv_nodes_free(properties);

    lilv_node_free(patch_readable);
    lilv_node_free(patch_writable);
}
#endif  // LV2_WORKER_SUPPORT


#ifdef USE_SUIL

static uint32_t
ui_port_index(void* const controller, const char* port_symbol)
{
    LV2_Plugin *pLv2Plugin = static_cast<LV2_Plugin *> (controller);
    if (pLv2Plugin == NULL)
        return LV2UI_INVALID_PORT_INDEX;

    const LilvPlugin *plugin = pLv2Plugin->get_slv2_plugin();
    if (plugin == NULL)
        return LV2UI_INVALID_PORT_INDEX;

    DMESSAGE("port_symbol = %s", port_symbol);

    LilvWorld* world = pLv2Plugin->get_lilv_world();

    LilvNode *symbol = lilv_new_string(world, port_symbol);

    const LilvPort *port = lilv_plugin_get_port_by_symbol(plugin, symbol);
    lilv_node_free(symbol);

    if(!port)
        return LV2UI_INVALID_PORT_INDEX;

    const unsigned long port_index = lilv_port_get_index(plugin, port);

    DMESSAGE("port_index = %U", port_index);

    return port ? port_index : LV2UI_INVALID_PORT_INDEX;
}

static void
send_to_plugin(void* const handle,              // LV2_Plugin
                    uint32_t    port_index,     // what port to send it to
                    uint32_t    buffer_size,    // OU = float or atom port
                    uint32_t    protocol,       // type of event 
                    const void* buffer)         // param value sizeof(float) or atom event  (sizeof(LV2_Atom))
{
    LV2_Plugin *pLv2Plugin = static_cast<LV2_Plugin *> (handle);
    if (pLv2Plugin == NULL)
        return;

#ifdef LV2_WORKER_SUPPORT
    if(pLv2Plugin->_exit_process)
        return;
#endif
    if (protocol == 0U)
    {
        if (buffer_size != sizeof(float))
        {
            WARNING("ERROR invalid buffer size for control");
            return;
        }

        /* Set flag to tell set_control_value() that custom ui does not need update */
        pLv2Plugin->_is_from_custom_ui = true;

        /* Pass the control information to the plugin and other generic ui widgets */
        pLv2Plugin->set_control_value(port_index, *(const float*)buffer);
    }
#ifdef LV2_WORKER_SUPPORT
    else if (protocol == Plugin_Module_URI_Atom_eventTransfer) 
    {
        DMESSAGE("UI SENT LV2_ATOM__eventTransfer");
        pLv2Plugin->send_atom_to_plugin( port_index, buffer_size, buffer );
    }
#endif
    else
    {
        DMESSAGE("UI wrote with unsupported protocol %u (%s)\n", protocol);
        return;
    }
}

bool
LV2_Plugin::try_custom_ui()
{
    /* Toggle show and hide */
    if(_ui_instance)
    {
        if (_x_is_visible)
        {
            close_custom_ui();
            return true;
        }
        else
        {
            show_custom_ui();
            return true;
        }
    }

    _idata->ext.ext_data.data_access =
        lilv_instance_get_descriptor(_lilv_instance)->extension_data;
    const LV2UI_Idle_Interface* idle_iface = NULL;
    const LV2UI_Show_Interface* show_iface = NULL; 
    
    if( custom_ui_instantiate() )
    {
        if(_ui_instance)
        {
            idle_iface = _idata->ext.idle_iface = (const LV2UI_Idle_Interface*)suil_instance_extension_data(
                _ui_instance, LV2_UI__idleInterface);

            _idata->ext.resize_ui = (const LV2UI_Resize*)suil_instance_extension_data(
                _ui_instance, LV2_UI__resize);

            if( _use_showInterface )
            {
                show_iface = _idata->ext.ui_showInterface = (const LV2UI_Show_Interface*)suil_instance_extension_data(
                    _ui_instance, LV2_UI__showInterface);
            }
#ifdef LV2_EXTERNAL_UI
            else if(_use_external_ui)
            {
                _lv2_ui_widget = suil_instance_get_widget(_ui_instance);
                _lv2_ui_handle = (LV2UI_Handle)
					suil_instance_get_handle(_ui_instance);
            }
#endif
        }
    }
    else
    {
        return false;
    }

    /* The custom ui needs to know the current settings of the plugin upon init */
    update_ui_settings();
    
    if(_use_showInterface)
    {
        if(idle_iface && show_iface)
        {
            show_custom_ui();
            DMESSAGE("Running showInterface");
            return true;
        }
    }
#ifdef LV2_EXTERNAL_UI
    else if(_use_external_ui)
    {
        show_custom_ui();
        DMESSAGE("Running external UI");
        return true;
    }
#endif
    else if(_use_X11_interface)   /* Run the X11 embedded */
    {
        show_custom_ui();
        DMESSAGE("Running embedded X custom UI");
        return true;
    }

    return false;
}

bool
LV2_Plugin::custom_ui_instantiate()
{
    _ui_host = suil_host_new(send_to_plugin, ui_port_index, NULL, NULL);

    /* Get a plugin UI */
    _all_uis = lilv_plugin_get_uis(_lilv_plugin);

    _use_showInterface = false;
    const char* native_ui_type;

    /* Try showInterface first */
    for(unsigned int i = 0; i < v_ui_types.size(); ++i)
    {
        _lilv_user_interface = try_showInterface_ui(v_ui_types[i].c_str());
        if(_lilv_user_interface)
        {
            _use_showInterface = true;
            native_ui_type = v_ui_types[i].c_str();
            MESSAGE("Using Show Interface = %s", v_ui_types[i].c_str());
            break;
        }
    }

    /* We didn't find showInterface so try to find an embeddable X11 UI */
    if(!_use_showInterface)
    {
        _lilv_user_interface = try_X11_ui(v_ui_types[0].c_str());
        if(_lilv_user_interface)
        {
            native_ui_type = v_ui_types[0].c_str();
            _use_X11_interface = true;
        }
    }

    if(!_lilv_user_interface)
    {
#ifdef LV2_EXTERNAL_UI
        _lilv_user_interface = try_external_ui(LV2_EXTERNAL_UI__Widget);
        if(_lilv_user_interface)
        {
            native_ui_type = LV2_EXTERNAL_UI__Widget;
            _use_external_ui = true;
        }
        else
        {
#endif
            MESSAGE("NO CUSTOM UI SUPPORTED");
            return false;
#ifdef LV2_EXTERNAL_UI
        }
#endif
    }

    void * parent = NULL;

    if(_use_X11_interface)   /* If embedded X11 */
    {
        /* We seem to have an accepted ui, so lets try to embed it in an X window*/
        _x_is_resizable = isUiResizable();
        _X11_UI = new X11PluginUI(this, _x_is_resizable, true);
        _X11_UI->setTitle(label());
        parent = (LV2UI_Widget) _X11_UI->getPtr();
    }

#ifdef LV2_EXTERNAL_UI
    _lv2_ui_external_host.ui_closed = mixer_lv2_ui_closed;
    _lv2_ui_external_host.plugin_human_id = base_label();
    _lv2_ui_external_feature.URI = LV2_EXTERNAL_UI__Host;
    _lv2_ui_external_feature.data = &_lv2_ui_external_host;
#endif

    const LV2_Feature parent_feature {LV2_UI__parent, parent};

    const LV2_Feature instance_feature = {
        LV2_INSTANCE_ACCESS_URI, lilv_instance_get_handle(_lilv_instance)};

    const LV2_Feature data_feature = {LV2_DATA_ACCESS_URI,
                                      &_idata->ext.ext_data};

    DMESSAGE("parent = %p: parent_feature->data = %p", parent, parent_feature.data);
    const LV2_Feature idle_feature = {LV2_UI__idleInterface, NULL};

    const LV2_Feature* ui_features[] = {_idata->features[Plugin_Feature_URID_Map],
                                        _idata->features[Plugin_Feature_URID_Unmap],
                                        &instance_feature,
                                        &data_feature,
                                      //  &jalv->features.log_feature,
                                        &parent_feature,
                                        _idata->features[Plugin_Feature_Options],
                                        &idle_feature,
                                       // &jalv->features.request_value_feature,
                                       _idata->features[Plugin_Feature_Resize],
                                       _idata->features[Plugin_Feature_Make_path],
#ifdef LV2_EXTERNAL_UI
                                        &_lv2_ui_external_feature,
#endif
                                        NULL};

    const char* bundle_uri  = lilv_node_as_uri(lilv_ui_get_bundle_uri(_lilv_user_interface));
    const char* binary_uri  = lilv_node_as_uri(lilv_ui_get_binary_uri(_lilv_user_interface));
    char*       bundle_path = lilv_file_uri_parse(bundle_uri, NULL);
    char*       binary_path = lilv_file_uri_parse(binary_uri, NULL);

    /* This is the real deal */
    _ui_instance =
      suil_instance_new(_ui_host,
                        this,
                        native_ui_type,
                        lilv_node_as_uri(lilv_plugin_get_uri(_lilv_plugin)),
                        lilv_node_as_uri(lilv_ui_get_uri(_lilv_user_interface)),
                        lilv_node_as_uri(_lilv_ui_type),
                        bundle_path,
                        binary_path,
                        ui_features);

    lilv_free(binary_path);
    lilv_free(bundle_path);

    if( !_ui_instance )
    {
        DMESSAGE("_ui_instance == NULL");
        return false;
    }
    else
    {
        DMESSAGE("Got valid _ui_instance");
    }

    return true;
}

const LilvUI*
LV2_Plugin::try_X11_ui (const char* native_ui_type)
{
    const LilvUI* native_ui = NULL;

    if (native_ui_type)
    {
        LilvNode* host_type = lilv_new_uri(_lilvWorld, native_ui_type);

        LILV_FOREACH (uis, u, _all_uis)
        {
            const LilvUI*   ui   = lilv_uis_get(_all_uis, u);
            const bool      supported =
              lilv_ui_is_supported(ui, suil_ui_supported, host_type, &_lilv_ui_type);

            if (supported)
            {
                DMESSAGE("GOT UI X11");
                lilv_node_free(host_type);
                host_type = NULL;
                native_ui = ui;
            }
        }

        if(host_type)
            lilv_node_free(host_type);
    }

    return native_ui;
}

#ifdef LV2_EXTERNAL_UI
const LilvUI*
LV2_Plugin::try_external_ui (const char* native_ui_type)
{
    const LilvUI* native_ui = NULL;

    if (native_ui_type)
    {
        LilvNode* host_type = lilv_new_uri(_lilvWorld, native_ui_type);

        LILV_FOREACH (uis, u, _all_uis)
        {
            const LilvUI*   ui   = lilv_uis_get(_all_uis, u);
            const bool      supported =
              lilv_ui_is_supported(ui, suil_ui_supported, host_type, &_lilv_ui_type);

            if (supported)
            {
                DMESSAGE("GOT EXTERNAL");
                lilv_node_free(host_type);
                host_type = NULL;
                native_ui = ui;
            }
        }

        if(host_type)
            lilv_node_free(host_type);
    }

    return native_ui;
}
#endif  // LV2_EXTERNAL_UI

const LilvUI*
LV2_Plugin::try_showInterface_ui(const char* native_ui_type)
{
    LilvNode*   lv2_extensionData = lilv_new_uri(_lilvWorld, LV2_CORE__extensionData);
    LilvNode*   ui_showInterface = lilv_new_uri(_lilvWorld, LV2_UI__showInterface);
    const LilvUI* native_ui = NULL;

    /* Try to find a UI with ui:showInterface */
    if(_all_uis)
    {
        LILV_FOREACH (uis, u, _all_uis)
        {
            const LilvUI*   ui      = lilv_uis_get(_all_uis, u);
            const LilvNode* ui_node = lilv_ui_get_uri(ui);

            lilv_world_load_resource(_lilvWorld, ui_node);

            const bool supported = lilv_world_ask(_lilvWorld,
                                                  ui_node,
                                                  lv2_extensionData,
                                                  ui_showInterface);

            lilv_world_unload_resource(_lilvWorld, ui_node);

            if (supported)
            {
                native_ui = ui;
                DMESSAGE("GOT ShowInterface CUSTOM UI");
                break;
            }
            else
            {
                DMESSAGE("NO ShowInterface %s", native_ui_type);
                return NULL;
            }
        }
    }
    else
    {
        DMESSAGE("NO _all_uis");
        return NULL;
    }

    LilvNode* host_type = lilv_new_uri(_lilvWorld, native_ui_type);

    if (!lilv_ui_is_supported(
              native_ui, suil_ui_supported, host_type, &_lilv_ui_type))
    {
          native_ui = NULL;
    }

    if(host_type)
        lilv_node_free(host_type);

    return native_ui;
}

bool
LV2_Plugin::send_to_custom_ui( uint32_t port_index, uint32_t size, uint32_t protocol, const void* buf )
{
    /* port_index coming in is internal number - so convert to plugin .ttl number */
    port_index = control_input[port_index].hints.plug_port_index;

   // DMESSAGE("Port_index = %u: Value = %f", port_index, *(const float*)buf);
    if(_ui_instance)
    {
        suil_instance_port_event(
            _ui_instance, port_index, size, protocol, buf );
    }

    return true;
}

/**
 Send the current LV2 plugin output settings to the custom ui
 */
void
LV2_Plugin::update_custom_ui()
{
    if(!_ui_instance)
        return;

    for ( unsigned int i = 0; i < control_output.size(); ++i)
    {
        float value = control_output[i].control_value();
        uint32_t port_index = control_output[i].hints.plug_port_index;

        suil_instance_port_event(
            _ui_instance, port_index, sizeof(float), 0, &value );
    }
}

/**
 Send the current LV2 plugin input settings to the custom ui.
 */
void
LV2_Plugin::update_ui_settings()
{
    if(!_ui_instance)
        return;

    for ( unsigned int i = 0; i < control_input.size(); ++i)
    {
        float value = isnan(control_input[i].control_value()) ? 0.0f : control_input[i].control_value();
        uint32_t port_index = control_input[i].hints.plug_port_index;

        suil_instance_port_event(
            _ui_instance, port_index, sizeof(float), 0, &value );
    }
}

/**
 Callback for custom ui idle interface
 */
void 
LV2_Plugin::custom_update_ui ( void *v )
{
    ((LV2_Plugin*)v)->custom_update_ui();
}

/**
 The idle callback to update_custom_ui()
 */
void
LV2_Plugin::custom_update_ui()
{
#ifdef LV2_EXTERNAL_UI
    if(_use_external_ui)
    {
        if (_lv2_ui_widget)
            LV2_EXTERNAL_UI_RUN((LV2_External_UI_Widget *) _lv2_ui_widget);
    }
    else
#endif
    if(_use_X11_interface)    // X11 embedded
    {
        _X11_UI->idle();
    }

    if( _idata->ext.idle_iface)
    {
        if (_idata->ext.idle_iface->idle(suil_instance_get_handle(_ui_instance)))
        {
            DMESSAGE("INTERFACE CLOSED");
            _x_is_visible = false;
        }
    }

    if(_x_is_visible)
    {
        update_custom_ui();
        Fl::repeat_timeout( 0.03f, &LV2_Plugin::custom_update_ui, this );
    }
    else
    {
        close_custom_ui();
    }
}

void
LV2_Plugin::close_custom_ui()
{
    DMESSAGE("Closing Custom Interface");
    Fl::remove_timeout(&LV2_Plugin::custom_update_ui, this);

    if( _use_showInterface )
    {
        _idata->ext.ui_showInterface->hide(suil_instance_get_handle(_ui_instance));
        _x_is_visible = false;

        /* For some unknown reason the Calf plugins idle interface does not get reset
           after the above ->hide is called. Any subsequent call to ->show then fails.
           So, instead we destroy the custom UI here and then re-create on show. */
        if(_ui_instance)
        {
            suil_instance_free(_ui_instance);
            _ui_instance = NULL;
        }

        if(_ui_host)
        {
            suil_host_free(_ui_host);
            _ui_host = NULL;
        }
    }
#ifdef LV2_EXTERNAL_UI
    else if( _use_external_ui )
    {
        if (_lv2_ui_widget)
            LV2_EXTERNAL_UI_HIDE((LV2_External_UI_Widget *) _lv2_ui_widget);

        _x_is_visible = false;

        if(_ui_instance)
        {
            suil_instance_free(_ui_instance);
            _ui_instance = NULL;
        }

        if(_ui_host)
        {
            suil_host_free(_ui_host);
            _ui_host = NULL;
        }
    }
#endif
    else    // X11
    {
        hide_custom_ui();
    }
}

void
LV2_Plugin::show_custom_ui()
{
    if( _use_showInterface )
    {
        _idata->ext.ui_showInterface->show(suil_instance_get_handle(_ui_instance));
        _x_is_visible = true;

        Fl::add_timeout( 0.03f, &LV2_Plugin::custom_update_ui, this );
        return;
    }
#ifdef LV2_EXTERNAL_UI
    if( _use_external_ui )
    {
        if (_lv2_ui_widget)
            LV2_EXTERNAL_UI_SHOW((LV2_External_UI_Widget *) _lv2_ui_widget);

        _x_is_visible = true;
        Fl::add_timeout( 0.03f, &LV2_Plugin::custom_update_ui, this );
        return;
    }
#endif

    _x_is_visible = true;
    _X11_UI->show();

    Fl::add_timeout( 0.03f, &LV2_Plugin::custom_update_ui, this );
}

void
LV2_Plugin::hide_custom_ui()
{
    _x_is_visible = false;

    if (_X11_UI != nullptr)
        _X11_UI->hide();
}

bool
LV2_Plugin::isUiResizable() const
{
    NON_SAFE_ASSERT_RETURN(_idata->rdf_data != nullptr, false);

    for (uint32_t i=0; i < _idata->rdf_data->FeatureCount; ++i)
    {
        if (std::strcmp(_idata->rdf_data->Features[i].URI, LV2_UI__fixedSize) == 0)
            return false;

        if (std::strcmp(_idata->rdf_data->Features[i].URI, LV2_UI__noUserResize) == 0)
            return false;
    }

    return true;
}
#endif  // USE_SUIL

nframes_t
LV2_Plugin::get_module_latency ( void ) const
{
    // FIXME: we should probably cache this value
    unsigned int nport = 0;

    for ( unsigned int i = 0; i < _idata->rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_OUTPUT( _idata->rdf_data->Ports[i].Types ) &&
             LV2_IS_PORT_CONTROL( _idata->rdf_data->Ports[i].Types ) )
        {
            if ( LV2_IS_PORT_DESIGNATION_LATENCY( _idata->rdf_data->Ports[i].Designation ) )
                return control_output[nport].control_value();
            ++nport;
        }
    }

    return 0;
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
        for( unsigned int i = 0; i < atom_input.size(); ++i )
        {
            if ( atom_input[i]._clear_input_buffer )
            {
               // DMESSAGE("GOT atom input clear buffer");
                atom_input[i]._clear_input_buffer = false;
                lv2_evbuf_reset(atom_input[i].event_buffer(), true);
            }

#ifdef LV2_MIDI_SUPPORT
            /* Includes JACK MIDI in to plugin MIDI in and Time base */
            process_atom_in_events( nframes, i );
#endif
        }

        apply_ui_events( nframes );
#endif
        // Run the plugin for LV2
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->descriptor->run( _idata->handle[i], nframes );
        }

#ifdef LV2_WORKER_SUPPORT
#ifdef LV2_MIDI_SUPPORT
        /* Atom out to custom UI and plugin MIDI out to JACK MIDI out */
        for( unsigned int i = 0; i < atom_output.size(); ++i )
        {
            process_atom_out_events( nframes, i );
        }
#endif  // LV2_MIDI_SUPPORT

        /* Process any worker replies. */
        if ( _idata->ext.worker)
        {
            // FIXME
            // jalv_worker_emit_responses(&jalv->state_worker, jalv->instance);
            // non_worker_emit_responses(&jalv->state_worker, jalv->instance);
            non_worker_emit_responses(_lilv_instance);
            if ( _idata->ext.worker && _idata->ext.worker->end_run)
            {
                _idata->ext.worker->end_run(_lilv_instance->lv2_handle);
            }
        }
#endif

        _latency = get_module_latency();
    }
}

#ifdef LV2_WORKER_SUPPORT
/**
 * Gets the plugin file name and path if applicable.
 * The caller should ensure a valid atom_input[] index and file exists.
 * 
 * @param port_index
 *      The atom_input[] index. NOT the plugin .ttl port index.
 * 
 * @return 
 *      The file if there is one. The returned value should not be freed.
 */
char *
LV2_Plugin::get_file ( int port_index ) const
{
    return (char *) atom_input[port_index]._file.c_str();
}

void
LV2_Plugin::set_file (const std::string &file, int port_index, bool need_update )
{
    atom_input[port_index]._file = file;
    atom_input[port_index]._need_file_update = need_update;

    /* To refresh the button label in the parameter editor */
    if ( _editor )
        _editor->refresh_file_button_label(port_index);
}
#endif

void
LV2_Plugin::get ( Log_Entry &e ) const
{
    e.add( ":lv2_plugin_uri", _idata->descriptor->URI );
 
    /* these help us display the module on systems which are missing this plugin */
    e.add( ":plugin_ins", _plugin_ins );
    e.add( ":plugin_outs", _plugin_outs );

    if ( _use_custom_data  )
    {
        Module *m = (Module *) this;
        LV2_Plugin *pm = static_cast<LV2_Plugin *> (m);

        /* Export directory location */
        if(!export_import_strip.empty())
        {
            std::size_t found = export_import_strip.find_last_of("/\\");
            std::string path = (export_import_strip.substr(0, found));

            std::string location = pm->get_custom_data_location(path);

            pm->save_LV2_plugin_state(location);
            DMESSAGE("Export location = %s", location.c_str());

            std::string base_dir = location.substr(location.find_last_of("/\\") + 1);
            e.add( ":custom_data", base_dir.c_str() );
        }
        else
        {
            /* If we already have pm->_project_directory, it means that we have an existing project
               already loaded. So use that directory instead of making a new one */
            std::string s = pm->_project_directory;
            if(s.empty())
            {
                /* This is a new project */
                s = pm->get_custom_data_location(project_directory);
            }
            if ( !s.empty() )
            {
                /* This is an existing project */
                pm->_project_directory = s;
                pm->save_LV2_plugin_state(s);

                std::string base_dir = s.substr(s.find_last_of("/\\") + 1);
                e.add( ":custom_data", base_dir.c_str() );
            }
        }
    }

#ifdef LV2_WORKER_SUPPORT
    /* If using state restore then all the file paths are stored in the custom data file */
    if(!_use_custom_data)
    {
        Module *m = (Module *) this;
        LV2_Plugin *pm = static_cast<LV2_Plugin *> (m);
        for ( unsigned int i = 0; i < pm->atom_input.size(); ++i )
        {
            if ( pm->atom_input[i]._file.empty() )
                continue;

            char *s = pm->get_file(i);

            DMESSAGE("File to save = %s", s);

            if ( strlen ( s ) )
            {
                e.add(":filename", s );
            }
        }
    }
#endif

    Module::get( e );
}

void
LV2_Plugin::set ( Log_Entry &e )
{
    int n = 0;
    std::string restore = "";

    /* we need to have number() defined before we create the control inputs in load() */
    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

    	if ( ! strcmp(s, ":number" ) )
        {
	    n = atoi(v);
        }
    }

    /* need to call this to set label even for version 0 modules */
    number(n);

    for ( int i = 0; i < e.size(); ++i )
    {
        const char *s, *v;

        e.get( i, &s, &v );

        if ( ! strcmp( s, ":lv2_plugin_uri" ) )
        {
#ifdef LV2_WORKER_SUPPORT
            _loading_from_file = true;
#endif
            Module::Picked picked = { LV2, v, 0, "" };
            load_plugin( picked );
        }
        else if ( ! strcmp( s, ":plugin_ins" ) )
        {
            _plugin_ins = atoi( v );
        }
        else if ( ! strcmp( s, ":plugin_outs" ) )
        {
            _plugin_outs = atoi( v );
        }
        else if ( ! strcmp( s, ":custom_data" ) )
        {
            if(!export_import_strip.empty())
            {
                std::string path = export_import_strip;

                std::size_t found = path.find_last_of("/\\");
                restore = (path.substr(0, found));
                restore += "/";
                restore += v;
            }
            else
            {
                restore = project_directory;
                restore += "/";
                restore += v;
                _project_directory = restore;
            }
        }
    }

    Module::set( e );
#ifdef LV2_WORKER_SUPPORT
    for ( unsigned int i = 0; i < atom_input.size(); ++i )
    {
        if ( atom_input[i]._need_file_update )
        {
            send_file_to_plugin( i, get_file( i ) );
        }
    }
#endif

    if (!restore.empty())
    {
        restore_LV2_plugin_state(restore);
    }
}
