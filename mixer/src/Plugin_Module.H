
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

#pragma once

#include "Module.H"
#include "Loggable.H"
#include "LV2_RDF_Utils.hpp"
#include "CarlaPluginUI.H"
#include <ladspa.h>
#include <lv2/data-access/data-access.h>
#include "zix/ring.h"
#include "zix/sem.h"
#include "zix/thread.h"
#include "lv2/atom-forge.h"

#define PRESET_SUPPORT 1
#define USE_CARLA 1

#define ATOM_BUFFER_SIZE 4096   // FIXME  /*jalv->opts.buffer_size*/ 

#ifdef USE_CARLA
typedef void (*EventProcPtr)(XEvent* ev);
#endif

class Fl_Menu_Button;
class Thread;


/* supported lv2 features */
enum Plugin_Module_Supported_Features {
    Plugin_Feature_BufSize_Bounded = 0,
    Plugin_Feature_BufSize_Fixed,
    Plugin_Feature_Options,
    Plugin_Feature_URI_Map,
    Plugin_Feature_URID_Map,
    Plugin_Feature_URID_Unmap,
#ifdef LV2_WORKER_SUPPORT
    Plugin_Feature_Worker_Schedule,
#endif
#ifdef USE_SUIL
    Plugin_Feature_Resize,
#endif
    Plugin_Features_Count
};

/* pre-mapped lv2 uris */
enum Plugin_Module_URIs {
    Plugin_Module_URI_Null = 0,
    Plugin_Module_URI_Atom_Bool,
    Plugin_Module_URI_Atom_Double,
    Plugin_Module_URI_Atom_Int,
    Plugin_Module_URI_Atom_Float,
    Plugin_Module_URI_Atom_Long,
    Plugin_Module_URI_BufSize_MaxLength,
    Plugin_Module_URI_BufSize_MinLength,
    Plugin_Module_URI_Parameter_SampleRate,
#ifdef LV2_WORKER_SUPPORT
    Plugin_Module_URI_Worker_Schedule,
    Plugin_Module_URI_Atom_Chunk,
    Plugin_Module_URI_Atom_eventTransfer,
    Plugin_Module_URI_patch_Set,
    Plugin_Module_URI_patch_Put,
#endif
#ifdef USE_SUIL
    Plugin_Module_URI_Resize,
    Plugin_Module_URI_Scale,
#endif
    Plugin_Module_URI_Count
};

/* lv2 plugin options */
/* mostly empty for now, plugins usually require it because of buf-size extension */
struct Plugin_Module_Options {
    enum OptIndex {
        MaxBlockLenth = 0,
        MinBlockLenth,
        SampleRate,
        ScaleFactor,
        Null,
        Count
    };

    int maxBufferSize;
    int minBufferSize;
    float sampleRate;
    float uiScale;
    LV2_Options_Option opts[Count];

    Plugin_Module_Options()
    {
        maxBufferSize = 0;
        minBufferSize = 0;
        sampleRate = 0.0;
        uiScale = 1.0f;

        LV2_Options_Option& optMaxBlockLenth(opts[MaxBlockLenth]);
        optMaxBlockLenth.context = LV2_OPTIONS_INSTANCE;
        optMaxBlockLenth.subject = 0;
        optMaxBlockLenth.key     = Plugin_Module_URI_BufSize_MaxLength;
        optMaxBlockLenth.size    = sizeof(int);
        optMaxBlockLenth.type    = Plugin_Module_URI_Atom_Int;
        optMaxBlockLenth.value   = &maxBufferSize;

        LV2_Options_Option& optMinBlockLenth(opts[MinBlockLenth]);
        optMinBlockLenth.context = LV2_OPTIONS_INSTANCE;
        optMinBlockLenth.subject = 0;
        optMinBlockLenth.key     = Plugin_Module_URI_BufSize_MinLength;
        optMinBlockLenth.size    = sizeof(int);
        optMinBlockLenth.type    = Plugin_Module_URI_Atom_Int;
        optMinBlockLenth.value   = &minBufferSize;

        LV2_Options_Option& optSampleRate(opts[SampleRate]);
        optSampleRate.context = LV2_OPTIONS_INSTANCE;
        optSampleRate.subject = 0;
        optSampleRate.key     = Plugin_Module_URI_Parameter_SampleRate;
        optSampleRate.size    = sizeof(float);
        optSampleRate.type    = Plugin_Module_URI_Atom_Float;
        optSampleRate.value   = &sampleRate;

        LV2_Options_Option& optScaleFactor(opts[ScaleFactor]);
        optScaleFactor.context = LV2_OPTIONS_INSTANCE;
        optScaleFactor.subject = 0;
        optScaleFactor.key     = Plugin_Module_URI_Scale;
        optScaleFactor.size    = sizeof(float);
        optScaleFactor.type    = Plugin_Module_URI_Atom_Float;
        optScaleFactor.value   = &uiScale;

        LV2_Options_Option& optNull(opts[Null]);
        optNull.context = LV2_OPTIONS_INSTANCE;
        optNull.subject = 0;
        optNull.key     = Plugin_Module_URI_Null;
        optNull.size    = 0;
        optNull.type    = Plugin_Module_URI_Null;
        optNull.value   = NULL;
    }
};

struct ImplementationData
{
    const LADSPA_Descriptor      *descriptor;
    struct 
    {
        const LV2_Descriptor     *descriptor;
        const LV2_RDF_Descriptor *rdf_data;
        LV2_Feature*              features[Plugin_Features_Count+1];
        // extension data
        Plugin_Module_Options     options;
        std::vector<std::string>  mappedURIs;
        // extensions
        struct
        {
            const LV2_Options_Interface* options;
            const LV2_State_Interface*   state;
            const LV2_Worker_Interface*  worker;
#ifdef LV2_WORKER_SUPPORT
            ZixRing*                    requests;   ///< Requests to the worker
            ZixRing*                    responses;  ///< Responses from the worker
            ZixRing*                    plugin_to_ui;   ///< Port events from plugin
            ZixRing*                    ui_to_plugin;   ///< Port events from UI
            void*                       ui_event_buf;   ///< Buffer for reading UI port events
            void*                       response;   ///< Worker response buffer
            ZixSem                      sem;        ///< Worker semaphore
            ZixThread                   thread;     ///< Worker thread
            LV2_Atom_Forge              forge;      ///< Atom forge
//	const LV2_Worker_Interface* iface;      ///< Plugin worker interface
            bool                        threaded;   ///< Run work in another thread
#endif
#ifdef USE_SUIL
            LilvUIs*          uis;          ///< All plugin UIs (RDF data)
            const LilvUI*     ui;           ///< Plugin UI (RDF data)
            const LilvNode*   ui_type;      ///< Plugin UI type (unwrapped)
            LV2_Extension_Data_Feature ext_data;
            const LV2UI_Idle_Interface* idle_iface;
            const LV2UI_Show_Interface* ui_showInterface;
            const LV2UI_Resize* resize_ui;
#endif
        } ext;
    } lv2;

#ifdef LV2_WORKER_SUPPORT
    ZixSem             work_lock;      ///< Lock for plugin work() method
//    JalvWorker         Worker;         ///< Worker thread implementation
//    JalvWorker         state_worker;   ///< Synchronous worker for state restore
    bool               exit;           ///< True iff execution is finished
    bool               safe_restore;   ///< Plugin restore() is thread-safe
#endif

    std::vector<void*> handle;

    ImplementationData()
    {
        descriptor     = NULL;
        lv2.descriptor = NULL;
        lv2.rdf_data   = NULL;

        for (int i=0; i<Plugin_Features_Count; ++i)
        {
            lv2.features[i] = new LV2_Feature;
            lv2.features[i]->URI = NULL;
            lv2.features[i]->data = NULL;
        }
        lv2.features[Plugin_Features_Count] = NULL;

        lv2.mappedURIs.push_back("");                           // Plugin_Module_URI_Null
        lv2.mappedURIs.push_back(LV2_ATOM__Bool);               // Plugin_Module_URI_Atom_Bool
        lv2.mappedURIs.push_back(LV2_ATOM__Double);             // Plugin_Module_URI_Atom_Double
        lv2.mappedURIs.push_back(LV2_ATOM__Int);                // Plugin_Module_URI_Atom_Int
        lv2.mappedURIs.push_back(LV2_ATOM__Float);              // Plugin_Module_URI_Atom_Float
        lv2.mappedURIs.push_back(LV2_ATOM__Long);               // Plugin_Module_URI_Atom_Long
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__maxBlockLength); // Plugin_Module_URI_BufSize_MaxLength
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__minBlockLength); // Plugin_Module_URI_BufSize_MinLength
        lv2.mappedURIs.push_back(LV2_PARAMETERS__sampleRate);   // Plugin_Module_URI_Parameter_SampleRate
        lv2.mappedURIs.push_back(LV2_UI__scaleFactor);
#ifdef LV2_WORKER_SUPPORT
        lv2.mappedURIs.push_back(LV2_ATOM__Sequence);
        lv2.mappedURIs.push_back(LV2_ATOM__Chunk);
        lv2.mappedURIs.push_back(LV2_ATOM__eventTransfer);
        lv2.mappedURIs.push_back(LV2_PATCH__Set);
        lv2.mappedURIs.push_back(LV2_PATCH__Put);
        lv2.mappedURIs.push_back(LV2_UI__resize);
        zix_sem_init(&work_lock, 1);
        zix_sem_init(&lv2.ext.sem, 0);
        
        lv2.ext.threaded = false;
        exit = false;
        safe_restore = false;
#endif
        lv2.ext.options = NULL;
        lv2.ext.state   = NULL;
        lv2.ext.worker  = NULL;
        lv2.ext.uis     = NULL;
        lv2.ext.ui      = NULL;
        lv2.ext.ui_type = NULL;
    }

    ~ImplementationData()
    {
        if ( lv2.rdf_data )
        {
            delete lv2.rdf_data;
            lv2.rdf_data = NULL;
        }
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
        if ( lv2.features[Plugin_Feature_URI_Map] && lv2.features[Plugin_Feature_URI_Map]->data )
            delete (LV2_URI_Map_Feature*)lv2.features[Plugin_Feature_URI_Map]->data;
_Pragma("GCC diagnostic pop")
        if ( lv2.features[Plugin_Feature_URID_Map] && lv2.features[Plugin_Feature_URID_Map]->data )
            delete (LV2_URID_Map*)lv2.features[Plugin_Feature_URID_Map]->data;

        if ( lv2.features[Plugin_Feature_URID_Unmap] && lv2.features[Plugin_Feature_URID_Unmap]->data )
            delete (LV2_URID_Unmap*)lv2.features[Plugin_Feature_URID_Unmap]->data;
#ifdef LV2_WORKER_SUPPORT
        if ( lv2.features[Plugin_Feature_Worker_Schedule] && lv2.features[Plugin_Feature_Worker_Schedule]->data )
            delete (LV2_Worker_Schedule*)lv2.features[Plugin_Feature_Worker_Schedule]->data;
 
        free(lv2.ext.ui_event_buf);
#endif
#ifdef USE_SUIL
        if ( lv2.features[Plugin_Feature_Resize] && lv2.features[Plugin_Feature_Resize]->data )
            delete (LV2UI_Resize*)lv2.features[Plugin_Feature_Resize]->data;
#endif
    }

    // -------------------------------------------------------------------
    // LV2 Features
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
    static uint32_t _lv2_uri_to_id(LV2_URI_Map_Callback_Data data, const char*, const char* uri)
    {
        return _lv2_urid_map(data, uri);
    }
_Pragma("GCC diagnostic pop")
    static LV2_URID _lv2_urid_map(LV2_URID_Map_Handle handle, const char* uri)
    {
        if ( handle == NULL || ! uri )
            return Plugin_Module_URI_Null;
        if (::strcmp(uri, LV2_ATOM__Bool) == 0)
            return Plugin_Module_URI_Atom_Bool;
        if ( ::strcmp(uri, LV2_ATOM__Double) == 0 )
            return Plugin_Module_URI_Atom_Double;
        if ( ::strcmp(uri, LV2_ATOM__Int) == 0 )
            return Plugin_Module_URI_Atom_Int;
        if ( ::strcmp(uri, LV2_ATOM__Float) == 0 )
            return Plugin_Module_URI_Atom_Float;
        if (::strcmp(uri, LV2_ATOM__Long) == 0)
            return Plugin_Module_URI_Atom_Long;
        if ( ::strcmp(uri, LV2_BUF_SIZE__maxBlockLength) == 0 )
            return Plugin_Module_URI_BufSize_MaxLength;
        if ( ::strcmp(uri, LV2_BUF_SIZE__minBlockLength) == 0 )
            return Plugin_Module_URI_BufSize_MinLength;
        if ( ::strcmp(uri, LV2_PARAMETERS__sampleRate) == 0 )
            return Plugin_Module_URI_Parameter_SampleRate;
        if ( ::strcmp(uri, LV2_UI__scaleFactor) == 0 )
            return Plugin_Module_URI_Scale;
#ifdef LV2_WORKER_SUPPORT
        if ( ::strcmp(uri, LV2_ATOM__Sequence) == 0 )
            return Plugin_Module_URI_Worker_Schedule;
        if ( ::strcmp(uri, LV2_ATOM__Chunk) == 0 )
            return Plugin_Module_URI_Atom_Chunk;

        if ( ::strcmp(uri, LV2_ATOM__eventTransfer) == 0 )
            return Plugin_Module_URI_Atom_eventTransfer;
        if ( ::strcmp(uri, LV2_PATCH__Set) == 0 )
            return Plugin_Module_URI_patch_Set;
        if ( ::strcmp(uri, LV2_PATCH__Put) == 0 )
            return Plugin_Module_URI_patch_Put;
#endif  // LV2_WORKER_SUPPORT
#ifdef  USE_SUIL
        if ( ::strcmp(uri, LV2_UI__resize) == 0 )
            return Plugin_Module_URI_Resize;
#endif

        ImplementationData* self = (ImplementationData*)handle;

        uint32_t ret = 0;
        for (std::vector<std::string>::iterator it = self->lv2.mappedURIs.begin(), end = self->lv2.mappedURIs.end(); it != end; ++it)
        {
            if (ret == 0)
            {
                ret = 1;
                continue;
            }

            const std::string& thisURI(*it);

            if ( thisURI == uri )
                return ret;

            ++ret;
        }

        self->lv2.mappedURIs.push_back(uri);
        return ret;
    }

    static const char* _lv2_urid_unmap(LV2_URID_Map_Handle handle, LV2_URID urid)
    {
        if ( handle == NULL || urid == 0 )
            return NULL;

        ImplementationData* self = (ImplementationData*)handle;

        if ( urid <= self->lv2.mappedURIs.size() )
            return self->lv2.mappedURIs[urid].c_str();

        return NULL;
    }
};




class Plugin_Module : public Module {

    static Thread *plugin_discover_thread;

public:

    class Plugin_Info
    {
    public:
        const char *path;
        unsigned long id;
        std::string name;
        std::string author;
        std::string category;
        int audio_inputs;
        int audio_outputs;
        const char *type;
        bool favorite;

        Plugin_Info ( bool is_lv2 )
            {
                path = 0;
                id = 0;
               
                audio_inputs = 0;
                audio_outputs = 0;
                type = is_lv2 ? "LV2" : "LADSPA";
                favorite = 0;
            }

        bool operator< ( const Plugin_Info &rhs ) {
            return strcmp( name.c_str(), rhs.name.c_str() ) < 1;
        }
    };

    bool load ( Picked picked );
    
#ifdef PRESET_SUPPORT
    /* Preset loading stuff */
    std::vector<LV2_RDF_Preset> PresetList;
    const LilvPlugin*     m_plugin;     /**< Plugin "class" (actually just a few strings) */
    LilvWorld* 	m_lilvWorld{};
    const LilvPlugins*	m_lilvPlugins{};
    LilvInstance*   m_instance{};       /**< Plugin "instance" (loaded shared lib) */
    LV2_URID_Map* _uridMapFt;
    LilvWorld* get_lilv_world() {return m_lilvWorld;}
    const LilvPlugin* get_slv2_plugin() const {return m_plugin; }
    const LilvPlugins* get_lilv_plugins() const {return m_lilvPlugins; }

    void set_control_value(unsigned long port_index, float value);

    void update_control_parameters(int choice);
    /* End Preset loading */
#endif
    
#ifdef USE_SUIL
  SuilHost*     m_ui_host;     ///< Plugin UI host support
  SuilInstance* m_ui_instance; ///< Plugin UI instance (shared library)
  bool          m_ui_closed;
  bool          m_use_showInterface;

  /* X window for custom plugin ui */
#ifdef USE_CARLA
    Display* fDisplay;
    Window   fHostWindow;
    Window   fChildWindow;
    bool     fChildWindowConfigured;
    bool     fChildWindowMonitoring;
    bool     fIsVisible;
    bool     fFirstShow;
    bool     fSetSizeCalledAtLeastOnce;
    bool     fIsIdling;
    bool     fIsResizable;
    EventProcPtr fEventProc;
#else
  Display *x_display;
  Window x_parent_win;
  Window x_child_win;
#endif
#endif

private:

    bool load_ladspa ( unsigned long id );
    bool load_lv2 ( const char* uri );

    volatile nframes_t _latency;
    nframes_t _last_latency;
    
    void init ( void );

    void bbox ( int &X, int &Y, int &W, int &H )
        {
            X = x();
            Y = y() + 5;
            W = w();
            H = h() - 10;
        }

 //   void cb_handle(Fl_Widget*);
 //   static void cb_handle(Fl_Widget*, void*);

 //   Fl_Button *close_button;
public:
//    struct ImplementationData;

    ImplementationData *_idata;

private:
    int _plugin_ins;
    int _plugin_outs;
#ifdef LV2_WORKER_SUPPORT
    int _atom_ins;
    int _atom_outs;
    bool _loading_from_file;
#endif
    bool _crosswire;

    static void *discover_thread ( void * );
 
    
    void set_input_buffer ( int n, void *buf );
    void set_output_buffer ( int n, void *buf );
    void set_control_buffer ( int n, void *buf );
    void activate ( void );
    void deactivate ( void );
    
    bool apply ( sample_t *buf, nframes_t nframes );

    bool plugin_instances ( unsigned int );

    void connect_ports ( void );

    bool loaded ( void ) const;

public:

    virtual bool get_impulse_response ( sample_t *buf, nframes_t nframes );

    virtual nframes_t get_module_latency ( void ) const;

    virtual void update ( void );

    static std::list<Plugin_Info> get_all_plugins ( void );

    static void spawn_discover_thread ( void );
    static void join_discover_thread ( void );

    Plugin_Module ( );
    virtual ~Plugin_Module();

    int plugin_ins ( void ) const { return _plugin_ins; }
    int plugin_outs ( void ) const { return _plugin_outs; }

//    void select_plugin ( unsigned long id );

    const char *name ( void ) const { return "Plugin"; }

    int can_support_inputs ( int );
    bool configure_inputs ( int );

    virtual bool bypass ( void ) const { return _bypass; }
    virtual void bypass ( bool v );

    virtual void process ( nframes_t );

    void handle_port_connection_change ( void );
    void handle_sample_rate_change ( nframes_t sample_rate );
    void resize_buffers ( nframes_t buffer_size );
    
#ifdef LV2_WORKER_SUPPORT
    void non_worker_init(Plugin_Module* plug,
                 const LV2_Worker_Interface* iface,
                 bool threaded);

    void non_worker_emit_responses( LilvInstance* instance );
    void non_worker_finish( void );
    void non_worker_destroy( void );
    bool send_to_ui( uint32_t port_index, uint32_t type, uint32_t size, const void* body );
    void ui_port_event( uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void* buffer );
    void send_file_to_plugin( int port, const std::string &filename );
    void send_atom_to_plugin( uint32_t port_index, uint32_t buffer_size, const void* buffer);
    void apply_ui_events( uint32_t nframes, unsigned int port );
    void set_lv2_port_properties ( Port * port, bool writable );
    void get_atom_output_events( void );
    int write_atom_event(const uint32_t    port_index,
                 const uint32_t    size,
                 const LV2_URID    type,
                 const void* const body);
#endif
    
#ifdef USE_SUIL
    bool try_custom_ui();
    bool custom_ui_instantiate();
    const LilvUI* try_X11_ui (const char* native_ui_type);
    const LilvUI* try_showInterface_ui(const char* native_ui_type);
    bool send_to_custom_ui( uint32_t port_index, uint32_t size, uint32_t type, const void* buf );
    void update_custom_ui();
    void update_ui_settings();
    void custom_update_ui();
    static void custom_update_ui ( void * );
    void init_x();
    void close_custom_ui();
    Window getChildWindow() const;

#ifdef USE_CARLA
    void show_custom_ui();
    void hide_custom_ui();
    void setSize(const uint width, const uint height, const bool forceUpdate, const bool resizeChild);
#else
    void close_x();
#endif
#endif

    LOG_CREATE_FUNC( Plugin_Module );

    MODULE_CLONE_FUNC( Plugin_Module );

protected:

    void get ( Log_Entry &e ) const;
    void set ( Log_Entry &e );

};
