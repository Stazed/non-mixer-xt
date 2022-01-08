
/*******************************************************************************/
/* Copyright (C) 2009 Jonathan Moore Liles                                     */
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

/* Filter module. Can host LADPSA Plugins, or can be inherited from to make internal
   modules with special features and appearance. */

#include "const.h"

#include <string.h>
#include <vector>
#include <string>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>

#include <FL/fl_draw.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Button.H>

#include "Plugin_Module.H"

#include "debug.h"

#define HAVE_LIBLRDF 1
#include "LADSPAInfo.h"

#include "Chain.H"

#include <dsp.h>

#include <algorithm>



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
        const float val = *(float *) value;
        const unsigned long port_index = lilv_port_get_index(plugin, port);
        
      //  DMESSAGE("port Index = %lu: value = %f", port_index, val);
        
        pLv2Plugin->set_control_value(port_index, val);
    }
    
    lilv_node_free(symbol);
}
#endif

#ifdef LV2_WORKER_SUPPORT

static LV2_Worker_Status
non_worker_respond(LV2_Worker_Respond_Handle handle,
                    uint32_t                  size,
                    const void*               data)
{
    DMESSAGE("non_worker_respond");
    Plugin_Module* worker = static_cast<Plugin_Module *> (handle);

    zix_ring_write(worker->_idata->lv2.ext.responses, (const char*)&size, sizeof(size));
    zix_ring_write(worker->_idata->lv2.ext.responses, (const char*)data, size);
    return LV2_WORKER_SUCCESS;
}

static void*
worker_func(void* data)
{
    DMESSAGE("worker_func");
    Plugin_Module* worker = (Plugin_Module*)data;
    void*       buf    = NULL;
    while (true)
    {
        DMESSAGE("worker_func - TOP");
        zix_sem_wait( &worker->_idata->lv2.ext.sem );
        if ( worker->_idata->exit )
        {
            break;
        }
        DMESSAGE("worker_func - MIDDLE");
        uint32_t size = 0;
        zix_ring_read(worker->_idata->lv2.ext.requests, (char*)&size, sizeof(size));

        if (!(buf = realloc(buf, size))) {
                fprintf(stderr, "error: realloc() failed\n");
                free(buf);
                return NULL;
        }

        zix_ring_read(worker->_idata->lv2.ext.requests, (char*)buf, size);

        zix_sem_wait(&worker->_idata->work_lock);
        DMESSAGE("worker_func - BOTTOM");
        worker->_idata->lv2.ext.worker->work(
                worker->m_instance->lv2_handle, non_worker_respond, worker, size, buf);

        zix_sem_post(&worker->_idata->work_lock);
        DMESSAGE("worker_func - END");
    }

    free(buf);
    return NULL;
}

LV2_Worker_Status
non_worker_schedule(LV2_Worker_Schedule_Handle handle,
                     uint32_t                   size,
                     const void*                data)
{
    DMESSAGE("non_worker_schedule");
    Plugin_Module* worker = static_cast<Plugin_Module *> (handle);
    
    if ( worker->_idata->lv2.ext.threaded )
    {
        // Schedule a request to be executed by the worker thread
        zix_ring_write(worker->_idata->lv2.ext.requests, (const char*)&size, sizeof(size));
        zix_ring_write(worker->_idata->lv2.ext.requests, (const char*)data, size);
        zix_sem_post( &worker->_idata->lv2.ext.sem );
    }
    else 
    {
        // Execute work immediately in this thread
        zix_sem_wait(&worker->_idata->work_lock);

        worker->_idata->lv2.ext.worker->work(
        worker->m_instance->lv2_handle, non_worker_respond, worker, size, data);

        zix_sem_post(&worker->_idata->work_lock);
    }
    return LV2_WORKER_SUCCESS;
}

static int
patch_set_get(Plugin_Module* plugin,
              const LV2_Atom_Object* obj,
              const LV2_Atom_URID**  property,
              const LV2_Atom**       value)
{
    lv2_atom_object_get(obj, plugin->_idata->_lv2_urid_map(plugin->_idata, LV2_PATCH__property),
                        (const LV2_Atom*)property,
                        plugin->_idata->_lv2_urid_map(plugin->_idata, LV2_PATCH__value),
                        value,
                        0);
    if (!*property)
    {
        WARNING( "patch:Set message with no property" );
        return 1;
    }
    else if ((*property)->atom.type != plugin->_idata->lv2.ext.forge.URID)
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
    lv2_atom_object_get(obj, plugin->_idata->_lv2_urid_map(plugin->_idata, LV2_PATCH__body),
                        (const LV2_Atom*)body,
                        0);
    if (!*body)
    {
        WARNING( "patch:Put message with no body" );
        return 1;
    }
    else if (!lv2_atom_forge_is_object_type(&plugin->_idata->lv2.ext.forge, (*body)->atom.type))
    {
        WARNING( "patch:Put body is not an object" );
        return 1;
    }

    return 0;
}

#endif  // LV2_WORKER_SUPPORT

/* handy class to handle lv2 open/close */
class LV2_Lib_Manager
{
public:
    LV2_Lib_Manager() {}

    ~LV2_Lib_Manager()
    {
        for (std::vector<LibraryInfo>::iterator it=libraries.begin(), end=libraries.end(); it != end; ++it)
        {
            LibraryInfo& libinfo (*it);

            if ( libinfo.handle )
                dlclose( libinfo.handle );
        }

        libraries.clear();
    }

    const LV2_Descriptor* get_descriptor_for_uri(const std::string& binary, const char* uri)
    {
#if 0
        // We need to check each time even if binary is the same
        // in case of multiple plugins bundled in the same binary
        for (std::vector<LibraryInfo>::iterator it=libraries.begin(), end=libraries.end(); it != end; ++it)
        {
            const LibraryInfo& libinfo(*it);

            if (libinfo.binary == binary)
                return libinfo.desc;
        }
#endif
        if (void* const handle = dlopen(binary.c_str(), RTLD_LAZY))
        {
            if (LV2_Descriptor_Function descfn = (LV2_Descriptor_Function)dlsym(handle, "lv2_descriptor"))
            {
                uint32_t i=0;
                const LV2_Descriptor* desc;
                while ((desc = descfn(i++)) != NULL)
                {
                    if ( ::strcmp(desc->URI, uri) == 0)
                        break;
                }
                if ( ! desc ) return NULL;

                LibraryInfo info;
                info.binary = binary;
                info.handle = handle;
                info.desc   = desc;
                libraries.push_back( info );

                return desc;
            }
        }

        return NULL;
    }

private:
    struct LibraryInfo {
        std::string binary;
        void*       handle;
        const LV2_Descriptor* desc;
    };
    std::vector<LibraryInfo> libraries;
};

static LV2_Lib_Manager lv2_lib_manager;

static LADSPAInfo *ladspainfo;
Thread* Plugin_Module::plugin_discover_thread;



Plugin_Module::Plugin_Module ( ) : Module( 50, 35, name() )
{
    init();

    color( fl_color_average(  fl_rgb_color( 0x99, 0x7c, 0x3a ), FL_BACKGROUND_COLOR, 1.0f ));
    
    end();

    log_create();
}

Plugin_Module::~Plugin_Module ( )
{
#ifdef LV2_WORKER_SUPPORT
 //   non_worker_finish();  // FIXME
 //   non_worker_destroy();

    zix_ring_free(_idata->lv2.ext.plugin_events);
    zix_ring_free(_idata->lv2.ext.ui_events);
#endif
    log_destroy();
    plugin_instances( 0 );
    
#ifdef PRESET_SUPPORT
    lilv_world_free(m_lilvWorld);
#endif
}



void
Plugin_Module::get ( Log_Entry &e ) const
{
//    char s[512];
//    snprintf( s, sizeof( s ), "ladspa:%lu", _idata->descriptor->UniqueID );
    if (_is_lv2)
        e.add( ":lv2_plugin_uri", _idata->lv2.descriptor->URI );
    else
        e.add( ":plugin_id", _idata->descriptor->UniqueID );

    /* these help us display the module on systems which are missing this plugin */
    e.add( ":plugin_ins", _plugin_ins );
    e.add( ":plugin_outs", _plugin_outs );

    Module::get( e );
}

void
Plugin_Module::set ( Log_Entry &e )
{
    int n = 0;

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

        if ( ! strcmp( s, ":plugin_id" ) )
        {
            load_ladspa( (unsigned long) atoll ( v ) );
        }
        else if ( ! strcmp( s, ":lv2_plugin_uri" ) )
        {
#ifdef LV2_WORKER_SUPPORT
            _loading_from_file = true;
#endif
            load_lv2( v );
        }
        else if ( ! strcmp( s, ":plugin_ins" ) )
        {
            _plugin_ins = atoi( v );
        }
        else if ( ! strcmp( s, ":plugin_outs" ) )
        {
            _plugin_outs = atoi( v );
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
}



void
Plugin_Module::init ( void )
{
    _latency = 0;
    _last_latency = 0;
    _idata = new ImplementationData();
    /* module will be bypassed until plugin is loaded */
    _bypass = true;
    _crosswire = false;
    _is_lv2 = false;

    align( (Fl_Align)FL_ALIGN_CENTER | FL_ALIGN_INSIDE );
//     color( (Fl_Color)fl_color_average( FL_MAGENTA, FL_WHITE, 0.5f ) );

    int tw, th, tx, ty;

    bbox( tx, ty, tw, th );

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
    _loading_from_file = false;
    _idata->lv2.ext.ui_event_buf     = malloc(ATOM_BUFFER_SIZE);
    LV2_Worker_Schedule* const m_lv2_schedule  = new LV2_Worker_Schedule;
    m_lv2_schedule->handle              = this;
    m_lv2_schedule->schedule_work       = non_worker_schedule;
#endif

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
#endif
    
#if 0
    	jalv->features.sched.handle = &jalv->worker;
	jalv->features.sched.schedule_work = jalv_worker_schedule;
	init_feature(&jalv->features.sched_feature,
	             LV2_WORKER__schedule, &jalv->features.sched);

	jalv->features.ssched.handle = &jalv->state_worker;
	jalv->features.ssched.schedule_work = jalv_worker_schedule;
	init_feature(&jalv->features.state_sched_feature,
	             LV2_WORKER__schedule, &jalv->features.ssched);

	/* Check for thread-safe state restore() method. */
	LilvNode* state_threadSafeRestore = lilv_new_uri(
		jalv->world, LV2_STATE__threadSafeRestore);
	if (lilv_plugin_has_feature(jalv->plugin, state_threadSafeRestore)) {
		jalv->safe_restore = true;
	}
	lilv_node_free(state_threadSafeRestore);
    
            // EVENT BUFFERS
        
	/* Create Plugin <=> UI communication buffers */
	jalv->ui_events     = zix_ring_new(jalv->opts.buffer_size);
	jalv->plugin_events = zix_ring_new(jalv->opts.buffer_size);
	zix_ring_mlock(jalv->ui_events);
	zix_ring_mlock(jalv->plugin_events);
    
    
    
#endif
    
#ifdef PRESET_SUPPORT
    m_lilvWorld = lilv_world_new();
    lilv_world_load_all(m_lilvWorld);
    m_lilvPlugins = lilv_world_get_all_plugins(m_lilvWorld);
#endif
}

void
Plugin_Module::update ( void )
{
    if ( _last_latency != _latency )
    {
        DMESSAGE( "Plugin latency changed to %lu", (unsigned long)_latency );

        chain()->client()->recompute_latencies();
    }

    _last_latency = _latency;

    update_tooltip();
}

int
Plugin_Module::can_support_inputs ( int n )
{
    /* this is the simple case */
    if ( plugin_ins() == n )
        return plugin_outs();
    /* e.g. MONO going into STEREO */
    /* we'll duplicate our inputs */
    else if ( n < plugin_ins() &&
              1 == n  )
    {
        return plugin_outs();
    }
    /* e.g. STEREO going into MONO */
    /* we'll run multiple instances of the plugin */
    else if ( n > plugin_ins() &&
              ( plugin_ins() == 1 && plugin_outs() == 1 ) )
    {
        return n;
    }

    return -1;
}

bool
Plugin_Module::configure_inputs( int n )
{
    unsigned int inst = _idata->handle.size();

    if ( ninputs() != n )
    {
        _crosswire = false;

        if ( n != ninputs() )
        {
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

void *
Plugin_Module::discover_thread ( void * )
{
    THREAD_ASSERT( Plugin_Discover );

    DMESSAGE( "Discovering plugins in the background" );

    ladspainfo = new LADSPAInfo();

    Lv2WorldClass::getInstance().initIfNeeded(/*::getenv("LV2_PATH")*/);

    return NULL;
}

/* Spawn a background thread for plugin discovery */
void
Plugin_Module::spawn_discover_thread ( void )
{
    if ( plugin_discover_thread )
    {
        FATAL( "Plugin discovery thread is already running or has completed" );
    }

    plugin_discover_thread = new Thread( "Plugin_Discover" );

    plugin_discover_thread->clone( &Plugin_Module::discover_thread, NULL );
}

void
Plugin_Module::join_discover_thread ( void )
{
    plugin_discover_thread->join();
}

#ifdef PRESET_SUPPORT
void
Plugin_Module::set_control_value(unsigned long port_index, float value)
{
    for ( unsigned int i = 0; i < control_input.size(); ++i )
    {
        if ( port_index == control_input[i].hints.plug_port_index )
        {
            control_input[i].control_value(value);
            DMESSAGE("Port Index = %d: Value = %f", port_index, value);
        }
    }
}

void 
Plugin_Module::update_control_parameters(int choice)
{    
    const Lv2WorldClass& lv2World = Lv2WorldClass::getInstance();

    DMESSAGE("PresetList[%d].URI = %s", choice, PresetList[choice].URI);

    LilvState *state = lv2World.getStateFromURI(PresetList[choice].URI, _uridMapFt);
    lilv_state_restore(state, m_instance,  mixer_lv2_set_port_value, this, 0, NULL);

    lilv_state_free(state);
}
#endif


/* return a list of available plugins */
std::list<Plugin_Module::Plugin_Info>
Plugin_Module::get_all_plugins ( void )
{
    if ( !ladspainfo )
    {
        if ( ! plugin_discover_thread )
            ladspainfo = new LADSPAInfo();
        else
            plugin_discover_thread->join();
    }

    std::vector<LADSPAInfo::PluginInfo> plugins = ladspainfo->GetPluginInfo();

    std::list<Plugin_Module::Plugin_Info> pr;

    int j = 0;
    for (std::vector<LADSPAInfo::PluginInfo>::iterator i=plugins.begin();
         i!=plugins.end(); i++, j++)
    {
        Plugin_Info pi(false);

        //   pi[j].path = i->Name.c_str();
        pi.path = NULL;
        pi.id = i->UniqueID;
        pi.author = i->Maker;
        pi.name = i->Name;
        pi.audio_inputs = i->AudioInputs;
        pi.audio_outputs = i->AudioOutputs;
        pi.category = "Unclassified";
        pr.push_back( pi );
    }
    
    struct catagory_match
    {
        std::string cat_type;
        std::string LV2_type;
    };

    /* To convert LV2 plugin class to LADSPA categories for plugin chooser consistency */
    std::vector<catagory_match> type_matches
    {
        {"Amplitude/Amplifiers", "Amplifier Plugin"},
        {"Amplitude/Distortions", "Distortion Plugin"},
        {"Amplitude/Dynamics/Compressors", "Compressor Plugin" },
        {"Amplitude/Dynamics/Envelope", "Envelope Plugin" },
        {"Amplitude/Dynamics/Expander", "Expander Plugin" },
        {"Amplitude/Dynamics/Gates", "Gate Plugin"},
        {"Amplitude/Dynamics/Limiters", "Limiter Plugin"},
        {"Amplitude/Dynamics", "Dynamics Plugin"},
        {"Amplitude/Modulators", "Modulator Plugin"},
        {"Amplitude/Waveshapers", "Waveshaper Plugin"},
        {"Frequency/EQs/Multiband", "Multiband EQ Plugin"},
        {"Frequency/EQs/Parametric", "Parametric EQ Plugin"},
        {"Frequency/EQs", "Equaliser Plugin"},
        {"Frequency/Filters/Allpass", "Allpass Filter Plugin"},
        {"Frequency/Filters/Bandpass", "Bandpass Filter Plugin"},
        {"Frequency/Filters/Comb", "Comb Filter Plugin"},
        {"Frequency/Filters/Highpass", "Highpass Filter Plugin"},
        {"Frequency/Filters/Lowpass", "Lowpass Filter Plugin"},
        {"Frequency/Filters/Notch", "Notch Filter Plugin"},
        {"Frequency/Filters", "Filter Plugin" },
        {"Frequency/Pitch shifters", "Pitch Shifter Plugin"},
        {"Generators/Oscillators", "Oscillator Plugin"},
        {"Generators", "Generator Plugin"},
        {"Simulators/Reverbs", "Reverb Plugin"},
        {"Simulators", "Simulator Plugin"},
        {"Spectral", "Spectral Plugin"},
        {"Time/Delays", "Delay Plugin"},
        {"Time/Flangers", "Flanger Plugin"},
        {"Time/Phasers", "Phaser Plugin"},
        {"Utilities", "Utility Plugin"}
    };

    const Lv2WorldClass& lv2World(Lv2WorldClass::getInstance());
    for (uint i=0, count=lv2World.getPluginCount(); i<count; i++)
    {
        const LilvPlugin* const cPlugin(lv2World.getPluginFromIndex(i));
        if (cPlugin == NULL) continue;

        Lilv::Plugin lilvPlugin(cPlugin);
        if (! lilvPlugin.get_uri().is_uri()) continue;

        bool supported = true;
        Lilv::Nodes featureNodes(lilvPlugin.get_required_features());

        LILV_FOREACH(nodes, it, featureNodes)
        {
            Lilv::Node featureNode(featureNodes.get(it));
            const char* const featureURI(featureNode.as_uri());
            if (featureURI == NULL) continue;

            if ( ::strcmp( featureURI, LV2_BUF_SIZE__boundedBlockLength ) == 0 )
                continue;
            if ( ::strcmp( featureURI, LV2_BUF_SIZE__fixedBlockLength   ) == 0 )
                continue;
            if ( ::strcmp( featureURI, LV2_OPTIONS__options ) == 0 )
                continue;
            if ( ::strcmp( featureURI, LV2_URI_MAP_URI      ) == 0 )
                continue;
            if ( ::strcmp( featureURI, LV2_URID__map        ) == 0 )
                continue;
            if ( ::strcmp( featureURI, LV2_URID__unmap      ) == 0 )
                continue;
#ifdef LV2_WORKER_SUPPORT
            if ( ::strcmp( featureURI, LV2_WORKER__schedule ) == 0 )
            {
              //  DMESSAGE("GOT Worker.schedule = %s", lilvPlugin.get_name().as_string());
                continue;
            }
#endif
            supported = false;
            break;
        }

        lilv_nodes_free(const_cast<LilvNodes*>(featureNodes.me));

        if ( ! supported )
            continue;

        Plugin_Info pi(true);
        
        // get audio port count and check for supported ports
        pi.audio_inputs = 0;
        pi.audio_outputs = 0;

        for (uint j=0, count=lilvPlugin.get_num_ports(); j<count; ++j)
        {
            Lilv::Port lilvPort(lilvPlugin.get_port_by_index(j));

            bool isInput;

            /**/ if (lilvPort.is_a(lv2World.port_input))
                isInput = true;
            else if (lilvPort.is_a(lv2World.port_output))
                isInput = false;
            else
                continue;

            if (lilvPort.is_a(lv2World.port_audio))
            {
                if (isInput)
                    ++(pi.audio_inputs);
                else
                    ++(pi.audio_outputs);
            }
            else if (lilvPort.is_a(lv2World.port_control) || lilvPort.has_property(lv2World.pprop_optional))
            {
                // supported or optional
            }
#ifdef LV2_WORKER_SUPPORT
            else if (lilvPort.is_a(lv2World.port_atom))
            {
                // supported of optional
            }
#endif
            else
            {
                // not supported
                supported = false;
                break;
            }
        }

        if ( ! supported )
            continue;
        
        // get name and author
        LilvNode* name_node = lilv_plugin_get_name(lilvPlugin);
        const char* const name = lilv_node_as_string( name_node );
        if( name )
        {
            pi.name = name;
        }
        lilv_node_free(name_node);

        LilvNode* author_node = lilv_plugin_get_author_name(lilvPlugin);
        const char* const author = lilv_node_as_string( author_node );
        if ( author )
        {
            pi.author = author;
        }
        lilv_node_free(author_node);

        // base info done
        pi.path = strdup(lilvPlugin.get_uri().as_uri());
        pi.id = 0;
        pi.category = "Unclassified";   // Default

        /* Use existing LADSPA table categories for Plugin_Chooser lookup categories */
        if (const char* const category = lilvPlugin.get_class().get_label().as_string())
        {
            pi.category = category;
            for(unsigned i = 0; i < type_matches.size(); ++i)
            {
                if(!strcmp(type_matches[i].LV2_type.c_str(), pi.category.c_str()))
                    pi.category = type_matches[i].cat_type;
            }
        }

        pr.push_back( pi );
    }

    pr.sort();

    const std::vector<LADSPAInfo::PluginEntry> pe = ladspainfo->GetMenuList();
  
    for (std::vector<LADSPAInfo::PluginEntry>::const_iterator i= pe.begin();
         i !=pe.end(); i++ )
    {
        for ( std::list<Plugin_Info>::iterator j = pr.begin(); j != pr.end(); j++ )
        {
            if ( j->id == i->UniqueID )
            {
                j->category = i->Category;
            }
        }
    }

    return pr;
}

bool
Plugin_Module::plugin_instances ( unsigned int n )
{
    if ( _idata->handle.size() > n )
    {
        for ( int i = _idata->handle.size() - n; i--; )
        {
            DMESSAGE( "Destroying plugin instance" );

            if (_is_lv2)
            {
                LV2_Handle h = _idata->handle.back();

                if ( _idata->lv2.descriptor->deactivate )
                    _idata->lv2.descriptor->deactivate( h );
                if ( _idata->lv2.descriptor->cleanup )
                    _idata->lv2.descriptor->cleanup( h );
            }
            else
            {
                LADSPA_Handle h = _idata->handle.back();

                if ( _idata->descriptor->deactivate )
                    _idata->descriptor->deactivate( h );
                if ( _idata->descriptor->cleanup )
                    _idata->descriptor->cleanup( h );
            }

            _idata->handle.pop_back();
        }
    }
    else if ( _idata->handle.size() < n )
    {
        for ( int i = n - _idata->handle.size(); i--; )
        {
            DMESSAGE( "Instantiating plugin... with sample rate %lu", (unsigned long)sample_rate());

            void* h;

            if (_is_lv2)
            {
                m_instance = lilv_plugin_instantiate(m_plugin,  sample_rate(), _idata->lv2.features);

                if ( ! m_instance )
                {
                    WARNING( "Failed to instantiate plugin" );
                    return false;
                }
                else
                {
                    h = m_instance->lv2_handle;
                    _idata->lv2.descriptor = m_instance->lv2_descriptor;    // probably not necessary
                }
            }
            else
            {
                if ( ! (h = _idata->descriptor->instantiate( _idata->descriptor, sample_rate() ) ) )
                {
                    WARNING( "Failed to instantiate plugin" );
                    return false;
                }
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
            if (_is_lv2)
            {
                for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
                {
                    if ( LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[k].Types ) )
                    {
                        if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                            _idata->lv2.descriptor->connect_port( h, k, (float*)control_input[ij++].buffer() );
                        else if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                            _idata->lv2.descriptor->connect_port( h, k, (float*)control_output[oj++].buffer() );
                    }
                    // we need to connect non audio/control ports to NULL
                    else if ( ! LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) &&
                             !LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[k].Types ))
                        _idata->lv2.descriptor->connect_port( h, k, NULL );

#ifdef LV2_WORKER_SUPPORT
                    if (LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[k].Types ))
                    {
                        if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                        {
                            if ( atom_input[aji].event_buffer() )
                                lv2_evbuf_free( atom_input[aji].event_buffer() );
                            
                            const size_t buf_size = ATOM_BUFFER_SIZE;
                            atom_input[aji].event_buffer( lv2_evbuf_new(buf_size,
                                                                  _uridMapFt->map(_uridMapFt->handle,
                                                                                  LV2_ATOM__Chunk),
                                                                  _uridMapFt->map(_uridMapFt->handle,
                                                                                  LV2_ATOM__Sequence)) ); 
                            lilv_instance_connect_port(
				m_instance, k, lv2_evbuf_get_buffer(atom_input[aji].event_buffer()));
                            
                            DMESSAGE("ATOM IN event_buffer = %p", lv2_evbuf_get_buffer(atom_input[aji].event_buffer()));

                            aji++;
                        }
                        else if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                        {
                            if ( atom_output[ajo].event_buffer() )
                                lv2_evbuf_free( atom_output[ajo].event_buffer() );

                            const size_t buf_size = ATOM_BUFFER_SIZE;
                            atom_output[ajo].event_buffer( lv2_evbuf_new(buf_size,
                                                                  _uridMapFt->map(_uridMapFt->handle,
                                                                                  LV2_ATOM__Chunk),
                                                                  _uridMapFt->map(_uridMapFt->handle,
                                                                                  LV2_ATOM__Sequence)) );
                            lilv_instance_connect_port(
				m_instance, k, lv2_evbuf_get_buffer( atom_output[ajo].event_buffer() ));
                            
                            /* This sets the capacity */
                            lv2_evbuf_reset(atom_output[ajo].event_buffer(), false);

                            DMESSAGE("ATOM OUT event_buffer = %p", lv2_evbuf_get_buffer( atom_output[ajo].event_buffer() ) );

                            ajo++;
                        }
                    }
#endif
                }

                // connect ports to magic bogus value to aid debugging.
                for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
                    if ( LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                        _idata->lv2.descriptor->connect_port( h, k, (float*)0x42 );
            }
            else
            {
                for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
                {
                    if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[k] ) )
                    {
                        if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[k] ) )
                            _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_input[ij++].buffer() );
                        else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[k] ) )
                            _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_output[oj++].buffer() );
                    }
                }

                // connect ports to magic bogus value to aid debugging.
                for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
                    if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
                        _idata->descriptor->connect_port( h, k, (LADSPA_Data*)0x42 );
            }
        }
    }

    return true;
}

void
Plugin_Module::bypass ( bool v )
{
    if ( v != bypass() )
    {
        if ( v )
            deactivate();
        else
            activate();
    }
}

nframes_t
Plugin_Module::get_module_latency ( void ) const
{
    // FIXME: we should probably cache this value
    if (_is_lv2)
    {
        unsigned int nport = 0;

        for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
        {
            if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
                 LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[i].Types ) )
            {
                if ( LV2_IS_PORT_DESIGNATION_LATENCY( _idata->lv2.rdf_data->Ports[i].Designation ) )
                    return control_output[nport].control_value();
                ++nport;
            }
        }
    }
    else
    {
        for ( unsigned int i = ncontrol_outputs(); i--; )
        {
            if ( !strcasecmp( "latency", control_output[i].name() ) )
            {
                return control_output[i].control_value();
            }
        }
    }
    
    return 0;
}

bool
Plugin_Module::load ( Module::Picked picked )
{
    if ( !ladspainfo )
    {
        if ( ! plugin_discover_thread )
            ladspainfo = new LADSPAInfo();
        else
            plugin_discover_thread->join();
    }

    return picked.is_lv2 ? load_lv2(picked.uri) : load_ladspa(picked.unique_id);
}

bool
Plugin_Module::load_ladspa ( unsigned long id )
{
    _is_lv2 = false;
    _idata->descriptor = ladspainfo->GetDescriptorByID( id );

    _plugin_ins = _plugin_outs = 0;

    if ( ! _idata->descriptor )
    {
        /* unknown plugin ID */
        WARNING( "Unknown plugin ID: %lu", id );
	char s[25];

	snprintf( s, 24, "! %lu", id );
	
        base_label( s );
        return false;
    }

    base_label( _idata->descriptor->Name );

    if ( _idata->descriptor )
    {
        if ( LADSPA_IS_INPLACE_BROKEN( _idata->descriptor->Properties ) )
        {
            WARNING( "Cannot use this plugin because it is incapable of processing audio in-place" );
            return false;
        }
       
        /* else if ( ! LADSPA_IS_HARD_RT_CAPABLE( _idata->descriptor->Properties ) ) */
        /* { */
        /*     WARNING( "Cannot use this plugin because it is incapable of hard real-time operation" ); */
        /*     return false; */
        /* } */

        MESSAGE( "Name: %s", _idata->descriptor->Name );

        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
        {
            if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
            {
                if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    add_port( Port( this, Port::INPUT, Port::AUDIO, _idata->descriptor->PortNames[ i ] ) );
                    _plugin_ins++;
                }
                else if (LADSPA_IS_PORT_OUTPUT(_idata->descriptor->PortDescriptors[i]))
                {
                    _plugin_outs++;
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO, _idata->descriptor->PortNames[ i ] ) );
                }
            }
        }

        MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);

        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
        {
            if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[i] ) )
            {
                Port::Direction d = Port::INPUT;

                if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    d = Port::INPUT;
                }
                else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[i] ) )
                {
                    d = Port::OUTPUT;
                }

                Port p( this, d, Port::CONTROL, _idata->descriptor->PortNames[ i ] );

                p.hints.default_value = 0;

                LADSPA_PortRangeHintDescriptor hd = _idata->descriptor->PortRangeHints[i].HintDescriptor;

                if ( LADSPA_IS_HINT_BOUNDED_BELOW(hd) )
                {
                    p.hints.ranged = true;
                    p.hints.minimum = _idata->descriptor->PortRangeHints[i].LowerBound;
                    if ( LADSPA_IS_HINT_SAMPLE_RATE(hd) )
                    {
                        p.hints.minimum *= sample_rate();
                    }
                }
                if ( LADSPA_IS_HINT_BOUNDED_ABOVE(hd) )
                {
                    p.hints.ranged = true;
                    p.hints.maximum = _idata->descriptor->PortRangeHints[i].UpperBound;
                    if ( LADSPA_IS_HINT_SAMPLE_RATE(hd) )
                    {
                        p.hints.maximum *= sample_rate();
                    }
                }

                if ( LADSPA_IS_HINT_HAS_DEFAULT(hd) )
                {

                    float Max=1.0f, Min=-1.0f, Default=0.0f;
                    int Port=i;

                    // Get the bounding hints for the port
                    LADSPA_PortRangeHintDescriptor HintDesc=_idata->descriptor->PortRangeHints[Port].HintDescriptor;
                    if (LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc))
                    {
                        Min=_idata->descriptor->PortRangeHints[Port].LowerBound;
                        if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc))
                        {
                            Min*=sample_rate();
                        }
                    }
                    if (LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc))
                    {
                        Max=_idata->descriptor->PortRangeHints[Port].UpperBound;
                        if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc))
                        {
                            Max*=sample_rate();
                        }
                    }

#ifdef LADSPA_VERSION
// We've got a version of the header that supports port defaults
                    if (LADSPA_IS_HINT_HAS_DEFAULT(HintDesc)) {
                        // LADSPA_HINT_DEFAULT_0 is assumed anyway, so we don't check for it
                        if (LADSPA_IS_HINT_DEFAULT_1(HintDesc)) {
                            Default = 1.0f;
                        } else if (LADSPA_IS_HINT_DEFAULT_100(HintDesc)) {
                            Default = 100.0f;
                        } else if (LADSPA_IS_HINT_DEFAULT_440(HintDesc)) {
                            Default = 440.0f;
                        } else {
                            // These hints may be affected by SAMPLERATE, LOGARITHMIC and INTEGER
                            if (LADSPA_IS_HINT_DEFAULT_MINIMUM(HintDesc) &&
                                LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc)) {
                                Default=_idata->descriptor->PortRangeHints[Port].LowerBound;
                            } else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(HintDesc) &&
                                       LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc)) {
                                Default=_idata->descriptor->PortRangeHints[Port].UpperBound;
                            } else if (LADSPA_IS_HINT_BOUNDED_BELOW(HintDesc) &&
                                       LADSPA_IS_HINT_BOUNDED_ABOVE(HintDesc)) {
                                // These hints require both upper and lower bounds
                                float lp = 0.0f, up = 0.0f;
                                float min = _idata->descriptor->PortRangeHints[Port].LowerBound;
                                float max = _idata->descriptor->PortRangeHints[Port].UpperBound;
                                if (LADSPA_IS_HINT_DEFAULT_LOW(HintDesc)) {
                                    lp = 0.75f;
                                    up = 0.25f;
                                } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(HintDesc)) {
                                    lp = 0.5f;
                                    up = 0.5f;
                                } else if (LADSPA_IS_HINT_DEFAULT_HIGH(HintDesc)) {
                                    lp = 0.25f;
                                    up = 0.75f;
                                }

                                if (LADSPA_IS_HINT_LOGARITHMIC(HintDesc)) {

                                    p.hints.type = Port::Hints::LOGARITHMIC;

                                    if (min==0.0f || max==0.0f) {
                                        // Zero at either end means zero no matter
                                        // where hint is at, since:
                                        //  log(n->0) -> Infinity
                                        Default = 0.0f;
                                    } else {
                                        // Catch negatives
                                        bool neg_min = min < 0.0f ? true : false;
                                        bool neg_max = max < 0.0f ? true : false;

                                        if (!neg_min && !neg_max) {
                                            Default = exp(::log(min) * lp + ::log(max) * up);
                                        } else if (neg_min && neg_max) {
                                            Default = -exp(::log(-min) * lp + ::log(-max) * up);
                                        } else {
                                            // Logarithmic range has asymptote
                                            // so just use linear scale
                                            Default = min * lp + max * up;
                                        }
                                    }
                                } else {
                                    Default = min * lp + max * up;
                                }
                            }
                            if (LADSPA_IS_HINT_SAMPLE_RATE(HintDesc)) {
                                Default *= sample_rate();
                            }
                        }

                        if (LADSPA_IS_HINT_INTEGER(HintDesc)) {
                            if ( p.hints.ranged &&
                                 0 == (int)p.hints.minimum &&
                                 1 == (int)p.hints.maximum )
                                p.hints.type = Port::Hints::BOOLEAN;
                            else
                                p.hints.type = Port::Hints::INTEGER;
                            Default = floorf(Default);
                        }
                        if (LADSPA_IS_HINT_TOGGLED(HintDesc)){
                            p.hints.type = Port::Hints::BOOLEAN;
                        }
                    }
#else
                    Default = 0.0f;
#endif
                    p.hints.default_value = Default;
                }

                float *control_value = new float;

                *control_value = p.hints.default_value;

                p.connect_to( control_value );

                add_port( p );

                DMESSAGE( "Plugin has control port \"%s\" (default: %f)", _idata->descriptor->PortNames[ i ], p.hints.default_value );
            }
        }
    }
    else
    {
        WARNING( "Failed to load plugin" );
        return false;
    }

    int instances = plugin_instances( 1 );
    
    if ( instances )
    {
        bypass( false );
    }

    return instances;
}

bool
Plugin_Module::load_lv2 ( const char* uri )
{
    _is_lv2 = true;
    _idata->lv2.rdf_data = lv2_rdf_new( uri, true );

#ifdef PRESET_SUPPORT
    PresetList = _idata->lv2.rdf_data->PresetListStructs;
    _uridMapFt =  (LV2_URID_Map*) _idata->lv2.features[Plugin_Feature_URID_Map]->data;
    LilvNode* plugin_uri = lilv_new_uri(get_lilv_world(), uri);
    m_plugin = lilv_plugins_get_by_uri(get_lilv_plugins(), plugin_uri);
    lilv_node_free(plugin_uri);
#endif

    _plugin_ins = _plugin_outs = 0;
#ifdef LV2_WORKER_SUPPORT
    _atom_ins = _atom_outs = 0;
    /* Create Plugin <=> UI communication buffers */
    _idata->lv2.ext.ui_events = zix_ring_new(ATOM_BUFFER_SIZE);
    _idata->lv2.ext.plugin_events = zix_ring_new(ATOM_BUFFER_SIZE);
    
    zix_ring_mlock(_idata->lv2.ext.ui_events);
    zix_ring_mlock(_idata->lv2.ext.plugin_events);
#endif

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
                _idata->lv2.ext.state = NULL;

            if ( _idata->lv2.ext.worker != NULL && _idata->lv2.ext.worker->work == NULL )
            {
                _idata->lv2.ext.worker = NULL;
            }
#ifdef LV2_WORKER_SUPPORT
            else
            {
                DMESSAGE("Setting worker initialization");

                zix_sem_init(&_idata->lv2.ext.sem, 0);
                lv2_atom_forge_init(&_idata->lv2.ext.forge, _uridMapFt);
                non_worker_init(this,  _idata->lv2.ext.worker, true);
		if (_idata->safe_restore)   // FIXME
                {
                    MESSAGE( "open -- Plugin Has safe_restore" );
                    non_worker_init(this, _idata->lv2.ext.worker, false);
		}
            }
#endif
        }
        else
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
                    add_port( Port( this, Port::INPUT, Port::ATOM, _idata->lv2.rdf_data->Ports[i].Name ) );
                    
                    if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->lv2.rdf_data->Ports[i].Types ))
                    {
                        DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - INPUT ");
                        atom_input[_atom_ins].hints.type = Port::Hints::PATCH_MESSAGE;
                        atom_input[_atom_ins].hints.plug_port_index = i;
                    }
                    _atom_ins++;
                    
                    DMESSAGE("GOT ATOM SEQUENCE PORT IN = %s", _idata->lv2.rdf_data->Ports[i].Name);
                }
                else if (LV2_IS_PORT_OUTPUT(_idata->lv2.rdf_data->Ports[i].Types))
                {
                    add_port( Port( this, Port::OUTPUT, Port::ATOM, _idata->lv2.rdf_data->Ports[i].Name ) );
                    
                    if (LV2_PORT_SUPPORTS_PATCH_MESSAGE( _idata->lv2.rdf_data->Ports[i].Types ))
                    {
                        DMESSAGE(" LV2_PORT_SUPPORTS_PATCH_MESSAGE - OUTPUT ");
                        atom_output[_atom_outs].hints.type = Port::Hints::PATCH_MESSAGE;
                        atom_output[_atom_outs].hints.plug_port_index = i;
                    }
                    _atom_outs++;

                    DMESSAGE("GOT ATOM SEQUENCE PORT OUT = %s", _idata->lv2.rdf_data->Ports[i].Name);
                }
            }
#endif
        }

        MESSAGE( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs);

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
        delete _idata->lv2.rdf_data;
        _idata->lv2.rdf_data = NULL;
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
        set_lv2_port_properties( &atom_input[i] );
    }
    
    for (unsigned int i = 0; i < atom_output.size(); ++i)
    {
        set_lv2_port_properties( &atom_output[i] );
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
            lilv_state_restore(state, m_instance,  NULL, this, 0, _idata->lv2.features);

            lilv_state_free(state);
        }
    }
    else
        _loading_from_file = false;
    
#endif

    return instances;
}

void
Plugin_Module::set_input_buffer ( int n, void *buf )
{
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
        h = _idata->handle[0];

    if (_is_lv2)
    {
        for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
            if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
                 LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[i].Types ) )
                if ( n-- == 0 )
                    _idata->lv2.descriptor->connect_port( h, i, (float*)buf );
    }
    else
    {
        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
            if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[i] ) &&
                 LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
                if ( n-- == 0 )
                    _idata->descriptor->connect_port( h, i, (LADSPA_Data*)buf );
    }
}

bool
Plugin_Module::loaded ( void ) const
{
    return _idata->handle.size() > 0 && ( _is_lv2 ? _idata->lv2.rdf_data && _idata->lv2.descriptor : _idata->descriptor != NULL );
}

void
Plugin_Module::set_output_buffer ( int n, void *buf )
{
    void* h;

    if ( instances() > 1 )
    {
        h = _idata->handle[n];
        n = 0;
    }
    else
        h = _idata->handle[0];

    if (_is_lv2)
    {
        for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
            if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
                LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[i].Types ) )
                if ( n-- == 0 )
                    _idata->lv2.descriptor->connect_port( h, i, (float*)buf );
    }
    else
    {
        for ( unsigned int i = 0; i < _idata->descriptor->PortCount; ++i )
            if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[i] ) &&
                 LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[i] ) )
                if ( n-- == 0 )
                    _idata->descriptor->connect_port( h, i, (LADSPA_Data*)buf );
    }
}

void
Plugin_Module::activate ( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Activating plugin \"%s\"", label() );

    if ( !bypass() )
        FATAL( "Attempt to activate already active plugin" );

    if ( chain() )
        chain()->client()->lock();

    if (_is_lv2)
    {
        if ( _idata->lv2.descriptor->activate )
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->lv2.descriptor->activate( _idata->handle[i] );
    }
    else
    {
        if ( _idata->descriptor->activate )
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->descriptor->activate( _idata->handle[i] );
    }

    _bypass = false;

    if ( chain() )
        chain()->client()->unlock();
}

void
Plugin_Module::deactivate( void )
{
    if ( !loaded() )
        return;

    DMESSAGE( "Deactivating plugin \"%s\"", label() );

    if ( chain() )
        chain()->client()->lock();

    _bypass = true;

    if (_is_lv2)
    {
        if ( _idata->lv2.descriptor->deactivate )
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->lv2.descriptor->deactivate( _idata->handle[i] );
    }
    else
    {
        if ( _idata->descriptor->deactivate )
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->descriptor->deactivate( _idata->handle[i] );
    }

    if ( chain() )
        chain()->client()->unlock();
}

void
Plugin_Module::handle_port_connection_change ( void )
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
Plugin_Module::handle_sample_rate_change ( nframes_t sample_rate )
{
    if ( ! _is_lv2 )
        return;
    if ( ! _idata->lv2.rdf_data )
        return;

    _idata->lv2.options.sampleRate = sample_rate;

    if ( _idata->lv2.ext.options && _idata->lv2.ext.options->set )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            _idata->lv2.ext.options->set( _idata->handle[i], &(_idata->lv2.options.opts[Plugin_Module_Options::SampleRate]) );
    }

    unsigned int nport = 0;

    for ( unsigned int i = 0; i < _idata->lv2.rdf_data->PortCount; ++i )
    {
        if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[i].Types ) &&
             LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[i].Types ) )
        {
            if ( LV2_IS_PORT_DESIGNATION_SAMPLE_RATE( _idata->lv2.rdf_data->Ports[i].Designation ) )
            {
                control_input[nport].control_value( sample_rate );
                break;
            }
            ++nport;
        }
    }
}

void
Plugin_Module::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );

    if ( ! _is_lv2 )
        return;
    if ( ! _idata->lv2.rdf_data )
        return;

    _idata->lv2.options.maxBufferSize = buffer_size;
    _idata->lv2.options.minBufferSize = buffer_size;

    if ( _idata->lv2.ext.options && _idata->lv2.ext.options->set )
    {
        for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
        {
            _idata->lv2.ext.options->set( _idata->handle[i], &(_idata->lv2.options.opts[Plugin_Module_Options::MaxBlockLenth]) );
            _idata->lv2.ext.options->set( _idata->handle[i], &(_idata->lv2.options.opts[Plugin_Module_Options::MinBlockLenth]) );
        }
    }
}

#ifdef LV2_WORKER_SUPPORT
void
Plugin_Module::non_worker_init(Plugin_Module* plug,
                 const LV2_Worker_Interface* iface,
                 bool                        threaded)
{
    DMESSAGE("Threaded = %d", threaded);
    plug->_idata->lv2.ext.worker = iface;
    plug->_idata->lv2.ext.threaded = threaded;

    if (threaded)
    {
        zix_thread_create(&plug->_idata->lv2.ext.thread, ATOM_BUFFER_SIZE, worker_func, plug);
        plug->_idata->lv2.ext.requests = zix_ring_new(ATOM_BUFFER_SIZE);
        zix_ring_mlock(plug->_idata->lv2.ext.requests);
    }
    
    plug->_idata->lv2.ext.responses = zix_ring_new(ATOM_BUFFER_SIZE);
    plug->_idata->lv2.ext.response = malloc(ATOM_BUFFER_SIZE);
    zix_ring_mlock(plug->_idata->lv2.ext.responses);
}

void
Plugin_Module::non_worker_emit_responses( LilvInstance* instance)
{
    // FIXME is this the place for this??
    for (unsigned int i = 0; i < atom_input.size(); ++i)
    {
        if ( atom_input[i]._clear_input_buffer )
        {
            atom_input[i]._clear_input_buffer = false;
            lv2_evbuf_reset(atom_input[i].event_buffer(), true);
        }
    }
    
    if (_idata->lv2.ext.responses)
    {
        uint32_t read_space = zix_ring_read_space(_idata->lv2.ext.responses);
        while (read_space)
        {
            DMESSAGE("GOT responses read space");
            uint32_t size = 0;
            zix_ring_read(_idata->lv2.ext.responses, (char*)&size, sizeof(size));

            zix_ring_read(_idata->lv2.ext.responses, (char*)_idata->lv2.ext.responses, size);

            _idata->lv2.ext.worker->work_response(
                instance->lv2_handle, size, _idata->lv2.ext.responses);

            read_space -= sizeof(size) + size;
        }
    }
}

void
Plugin_Module::non_worker_finish( void )
{
    if (_idata->lv2.ext.threaded) 
    {
        zix_sem_post(&_idata->lv2.ext.sem);
        zix_thread_join(_idata->lv2.ext.thread, NULL);
    }
}

void
Plugin_Module::non_worker_destroy( void )
{
    if (_idata->lv2.ext.requests) 
    {
        if (_idata->lv2.ext.threaded)
        {
            zix_ring_free(_idata->lv2.ext.requests);
        }

        zix_ring_free(_idata->lv2.ext.responses);
        free(_idata->lv2.ext.response);
    }
}

bool
Plugin_Module::send_to_ui( uint32_t port_index, uint32_t type, uint32_t size, const void* body)
{
    /* TODO: Be more discriminate about what to send */
    char evbuf[sizeof(ControlChange) + sizeof(LV2_Atom)];
    ControlChange* ev = (ControlChange*)evbuf;
    ev->index    = port_index;
    ev->protocol = _idata->_lv2_urid_map(_idata, LV2_ATOM__eventTransfer);
    ev->size     = sizeof(LV2_Atom) + size;

    LV2_Atom* atom = (LV2_Atom*)ev->body;
    atom->type = type;
    atom->size = size;

    if (zix_ring_write_space( _idata->lv2.ext.plugin_events ) >= sizeof(evbuf) + size)
    {
        DMESSAGE("Write to .plugin_events");
        zix_ring_write(_idata->lv2.ext.plugin_events, evbuf, sizeof(evbuf));
        zix_ring_write(_idata->lv2.ext.plugin_events, (const char*)body, size);
        return true;
    }
    else
    {
        WARNING( "Plugin => UI buffer overflow!" );
        return false;
    }
}

/**
 * Called from ui timeout
 * @return 
 *      Not used.
 */
int 
Plugin_Module::update_ui( void )
{
    /* Emit UI events. */
    ControlChange ev;
    const size_t  space = zix_ring_read_space( _idata->lv2.ext.plugin_events );
    for (size_t i = 0;
         i + sizeof(ev) < space;
         i += sizeof(ev) + ev.size)
    {
        DMESSAGE("Reading .plugin_events");
        /* Read event header to get the size */
        zix_ring_read( _idata->lv2.ext.plugin_events, (char*)&ev, sizeof(ev));

        /* Resize read buffer if necessary */
        _idata->lv2.ext.ui_event_buf = realloc(_idata->lv2.ext.ui_event_buf, ev.size);
        void* const buf = _idata->lv2.ext.ui_event_buf;

        /* Read event body */
        zix_ring_read( _idata->lv2.ext.plugin_events, (char*)buf, ev.size);

        ui_port_event( ev.index, ev.size, ev.protocol, buf );
    }

    return 1;
}

void
Plugin_Module::ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer )
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
    if (lv2_atom_forge_is_object_type( &_idata->lv2.ext.forge, atom->type))
    {
//        updating = true;  // FIXME
        const LV2_Atom_Object* obj = (const LV2_Atom_Object*)buffer;
        if (obj->body.otype ==  _idata->_lv2_urid_map(_idata, LV2_PATCH__Set))
        {
            const LV2_Atom_URID* property = NULL;
            const LV2_Atom*      value    = NULL;
            if (!patch_set_get(this, obj, &property, &value))
            {
                DMESSAGE("To set_file(): atom_input_index = %u: Value received = %s",ai , (char *) (value + 1) );
                set_file ((char *) (value + 1), ai, false ); // false means don't update, since this IS the update
            }
        } 
        else if (obj->body.otype == _idata->_lv2_urid_map(_idata, LV2_PATCH__Put))
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
            WARNING("Unknown object type?");
        }
      //  updating = false; // FIXME
    }
}

void
Plugin_Module::send_file_to_plugin( int port, std::string filename )
{
    DMESSAGE("File = %s", filename.c_str());
    
    /* Set the file for non-mixer here - may be redundant some times */
    atom_input[port]._file = filename;
    
    uint32_t size = filename.size() + 1;
    
    // Copy forge since it is used by process thread
    LV2_Atom_Forge       forge =  _idata->lv2.ext.forge;
    LV2_Atom_Forge_Frame frame;
    uint8_t              buf[1024];
    lv2_atom_forge_set_buffer(&forge, buf, sizeof(buf));

    lv2_atom_forge_object(&forge, &frame, 0, _idata->_lv2_urid_map(_idata, LV2_PATCH__Set) );
    lv2_atom_forge_key(&forge, _idata->_lv2_urid_map(_idata, LV2_PATCH__property) );
    lv2_atom_forge_urid(&forge, atom_input[port]._property_mapped);
    lv2_atom_forge_key(&forge, _idata->_lv2_urid_map(_idata, LV2_PATCH__value));
    lv2_atom_forge_atom(&forge, size, _idata->lv2.ext.forge.Path);
    lv2_atom_forge_write(&forge, (const void*)filename.c_str(), size);

    const LV2_Atom* atom = lv2_atom_forge_deref(&forge, frame.ref);
    uint32_t       buffer_size = lv2_atom_total_size(atom);
    
    char buf2[sizeof(ControlChange) + buffer_size];
    ControlChange* ev = (ControlChange*)buf2;
    ev->index    =   atom_input[port].hints.plug_port_index; 
    ev->protocol =  _idata->_lv2_urid_map(_idata, LV2_ATOM__eventTransfer);
    ev->size     =  buffer_size;
    memcpy(ev->body, (const void*) atom, buffer_size);
    zix_ring_write( _idata->lv2.ext.ui_events, buf2, sizeof(buf2));

    /*jalv_ui_write(jalv,
                jalv->control_in,   // port atom_input[port].hints.control_in;
                lv2_atom_total_size(atom),
                jalv->urids.atom_eventTransfer,
                atom);*/
}

// jalv_apply_ui_events
void
Plugin_Module::apply_ui_events( uint32_t nframes, unsigned int port )
{
    ControlChange ev;
    const size_t  space = zix_ring_read_space(_idata->lv2.ext.ui_events);

    for (size_t i = 0; i < space; i += sizeof(ev) + ev.size)
    {
        zix_ring_read(_idata->lv2.ext.ui_events, (char*)&ev, sizeof(ev));
        char body[ev.size];
        if (zix_ring_read(_idata->lv2.ext.ui_events, body, ev.size) != ev.size)
        {
            DMESSAGE("error: Error reading from UI ring buffer");
            break;
        }
        
/*        assert(ev.index < jalv->num_ports);
        struct Port* const port = &jalv->ports[ev.index];
        if (ev.protocol == 0)
        {
                assert(ev.size == sizeof(float));
                port->control = *(float*)body;
        }
*/
        else if (ev.protocol == _idata->_lv2_urid_map(_idata, LV2_ATOM__eventTransfer))
        {
            DMESSAGE("LV2 ATOM eventTransfer");
            LV2_Evbuf_Iterator    e    = lv2_evbuf_end( atom_input[port].event_buffer() ); 
            const LV2_Atom* const atom = (const LV2_Atom*)body;
            lv2_evbuf_write(&e, nframes, 0, atom->type, atom->size,
                            (const uint8_t*)LV2_ATOM_BODY_CONST(atom));
        }
        else
        {
            DMESSAGE("error: Unknown control change protocol %u", ev.protocol);
        }
    }
}

void
Plugin_Module::set_lv2_port_properties (Port * port )
{
//    bool writable = false;   // FIXME

    const LilvPlugin* plugin         = m_plugin;
    LilvWorld*        world          = m_lilvWorld;
    LilvNode*         patch_writable = lilv_new_uri(world, LV2_PATCH__writable);
    LilvNode*         patch_readable = lilv_new_uri(world, LV2_PATCH__readable);

    // This checks for control type -- in our case the patch__writable is returned as properties
    LilvNodes* properties = lilv_world_find_nodes(
    world,
    lilv_plugin_get_uri(plugin),
    patch_writable,
    NULL);
    
    if( ! properties )
    {
        DMESSAGE("Atom port has no properties");
        return;
    }

    LILV_FOREACH(nodes, p, properties)
    {
        const LilvNode* property = lilv_nodes_get(properties, p);

        DMESSAGE("Property = %s", lilv_node_as_string(property));

        if (lilv_world_ask(world,
                        lilv_plugin_get_uri(plugin),
                        patch_writable,
                        property))
        {
            port->_property = property;
            break;
        }
    }
    
    LilvNode* rdfs_label;
    rdfs_label = lilv_new_uri(world, LILV_NS_RDFS "label");
    
    port->_label  = lilv_world_get(world, port->_property, rdfs_label, NULL);
    port->_symbol = lilv_world_get_symbol(world, port->_property);
    port->_property_mapped = _idata->_lv2_urid_map(_idata, lilv_node_as_uri( port->_property ));
    
    DMESSAGE("Properties label = %s", lilv_node_as_string(port->_label));
    DMESSAGE("Properties symbol = %s", lilv_node_as_string(port->_symbol));
    DMESSAGE("Property mapped = %u", port->_property_mapped);

    lilv_nodes_free(properties);

    lilv_node_free(patch_readable);
    lilv_node_free(patch_writable);
}

void
Plugin_Module::get_atom_output_events( void )
{
    for ( unsigned int k = 0; k < atom_output.size(); ++k )
    {
        for (LV2_Evbuf_Iterator i = lv2_evbuf_begin(atom_output[k].event_buffer());
            lv2_evbuf_is_valid(i);
            i = lv2_evbuf_next(i))
        {
            // Get event from LV2 buffer
            uint32_t frames    = 0;
            uint32_t subframes = 0;
            uint32_t type      = 0;
            uint32_t size      = 0;
            uint8_t* body      = NULL;
            lv2_evbuf_get(i, &frames, &subframes, &type, &size, &body);

            DMESSAGE("GOT ATOM EVENT BUFFER");

            send_to_ui(atom_output[k].hints.plug_port_index, type, size, body);
        }

        /* Clear event output for plugin to write to on next cycle */
        lv2_evbuf_reset(atom_output[k].event_buffer(), false);
    }            
}

#endif  // LV2_WORKER_SUPPORT



bool 
Plugin_Module::get_impulse_response ( sample_t *buf, nframes_t nframes )
{
    apply( buf, nframes );
    
    if ( buffer_is_digital_black( buf + 1, nframes - 1 ))
        /* no impulse response... */
        return false;

    return true;
}

/** Instantiate a temporary version of the plugin, and run it (in place) against the provided buffer */
bool
Plugin_Module::apply ( sample_t *buf, nframes_t nframes )
{
// actually osc or UI    THREAD_ASSERT( UI );

    void* h;
    LilvInstance* temp_instance;

    if (_is_lv2)
    {
        temp_instance = lilv_plugin_instantiate(m_plugin,  sample_rate(), _idata->lv2.features);

        if ( !temp_instance )
        {
            WARNING( "Failed to instantiate plugin" );
            return false;
        }
        else
        {
            h = temp_instance->lv2_handle;
            _idata->lv2.descriptor = temp_instance->lv2_descriptor;
        }
    }
    else
    {
        if ( ! (h = _idata->descriptor->instantiate( _idata->descriptor, sample_rate() ) ) )
        {
            WARNING( "Failed to instantiate plugin" );
            return false;
        }
    }

    int ij = 0;
    int oj = 0;
#ifdef LV2_WORKER_SUPPORT
    int aji = 0;
    int ajo = 0;
#endif
    
    if (_is_lv2)
    {
        for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
        {
            if ( LV2_IS_PORT_CONTROL( _idata->lv2.rdf_data->Ports[k].Types ) )
            {   
                if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                    _idata->lv2.descriptor->connect_port( h, k, (float*)control_input[ij++].buffer() );
                else if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                    _idata->lv2.descriptor->connect_port( h, k, (float*)control_output[oj++].buffer() );
            }
            // we need to connect non audio/control ports to NULL
            else if ( ! LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) &&
                        !LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[k].Types ))
                _idata->lv2.descriptor->connect_port( h, k, NULL );

#ifdef LV2_WORKER_SUPPORT
            if (LV2_IS_PORT_ATOM_SEQUENCE ( _idata->lv2.rdf_data->Ports[k].Types ))
            {
                if ( LV2_IS_PORT_INPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                {
                    if ( atom_input[aji].tmp_event_buffer() )
                        lv2_evbuf_free( atom_input[aji].tmp_event_buffer() );

                    const size_t buf_size = ATOM_BUFFER_SIZE;
                    atom_input[aji].tmp_event_buffer( lv2_evbuf_new(buf_size, _uridMapFt->map(_uridMapFt->handle,
                                                    LV2_ATOM__Chunk),
                                                    _uridMapFt->map(_uridMapFt->handle,
                                                    LV2_ATOM__Sequence)) );
                    lilv_instance_connect_port(
                        temp_instance, k, lv2_evbuf_get_buffer( atom_input[aji].tmp_event_buffer() ));

                    aji++;
                }
                else if ( LV2_IS_PORT_OUTPUT( _idata->lv2.rdf_data->Ports[k].Types ) )
                {
                    if ( atom_output[ajo].tmp_event_buffer() )
                        lv2_evbuf_free( atom_output[ajo].tmp_event_buffer() );

                    const size_t buf_size = ATOM_BUFFER_SIZE;
                    atom_output[ajo].tmp_event_buffer( lv2_evbuf_new(buf_size, _uridMapFt->map(_uridMapFt->handle,
                                                    LV2_ATOM__Chunk),
                                                    _uridMapFt->map(_uridMapFt->handle,
                                                    LV2_ATOM__Sequence)) );
                    lilv_instance_connect_port(
                        temp_instance, k, lv2_evbuf_get_buffer( atom_output[ajo].tmp_event_buffer() ));

                    ajo++;
                }
            }
#endif
        }

        if ( _idata->lv2.descriptor->activate )
            _idata->lv2.descriptor->activate( h );
    }
    else
    {
        for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
        {
            if ( LADSPA_IS_PORT_CONTROL( _idata->descriptor->PortDescriptors[k] ) )
            {
                if ( LADSPA_IS_PORT_INPUT( _idata->descriptor->PortDescriptors[k] ) )
                    _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_input[ij++].buffer() );
                else if ( LADSPA_IS_PORT_OUTPUT( _idata->descriptor->PortDescriptors[k] ) )
                    _idata->descriptor->connect_port( h, k, (LADSPA_Data*)control_output[oj++].buffer() );
            }
        }

        if ( _idata->descriptor->activate )
            _idata->descriptor->activate( h );
    }

    int tframes = 512;
    float tmp[tframes];

    memset( tmp, 0, sizeof( float ) * tframes );

    if (_is_lv2)
    {
        for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
            if ( LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                _idata->lv2.descriptor->connect_port( h, k, tmp );
    }
    else
    {
        for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
            if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
                _idata->descriptor->connect_port( h, k, tmp );
    }

    /* flush any parameter interpolation */
    if (_is_lv2)
    {
        _idata->lv2.descriptor->run( h, tframes );

        for ( unsigned int k = 0; k < _idata->lv2.rdf_data->PortCount; ++k )
            if ( LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                _idata->lv2.descriptor->connect_port( h, k, buf );
    }
    else
    {
        _idata->descriptor->run( h, tframes );

        for ( unsigned int k = 0; k < _idata->descriptor->PortCount; ++k )
            if ( LADSPA_IS_PORT_AUDIO( _idata->descriptor->PortDescriptors[k] ) )
                _idata->descriptor->connect_port( h, k, buf );
    }

    /* run for real */
    if (_is_lv2)
    {
        _idata->lv2.descriptor->run( h, nframes );

        if ( _idata->lv2.descriptor->deactivate )
            _idata->lv2.descriptor->deactivate( h );
        if ( _idata->lv2.descriptor->cleanup )
            _idata->lv2.descriptor->cleanup( h );
    }
    else
    {
        _idata->descriptor->run( h, nframes );

        if ( _idata->descriptor->deactivate )
            _idata->descriptor->deactivate( h );
        if ( _idata->descriptor->cleanup )
            _idata->descriptor->cleanup( h );
    }

    return true;
}
/**********/
/* Client */
/**********/

void
Plugin_Module::process ( nframes_t nframes )
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
        if (_is_lv2)
        {
#ifdef LV2_WORKER_SUPPORT
            get_atom_output_events();
            
            for( unsigned int i = 0; i < atom_input.size(); ++i )
            {
                apply_ui_events(  nframes, i );
                atom_input[i]._clear_input_buffer = true;
            }
#endif
            // Run the plugin for LV2
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
            {
                _idata->lv2.descriptor->run( _idata->handle[i], nframes );
            }

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
                
                update_ui();
            }
#endif
        }
        else    // ladspa
        {
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->descriptor->run( _idata->handle[i], nframes );
        }

        _latency = get_module_latency();
    }
}


