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
    // TODO
}

void
LV2_Plugin::init ( void )
{
    // TODO
}

void
LV2_Plugin::process ( nframes_t nframes )
{
    // TODO
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