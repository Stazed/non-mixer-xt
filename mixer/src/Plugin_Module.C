
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
#include <ladspa.h>
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

#include "LV2_RDF_Utils.hpp"

#include "Chain.H"
//#include "Client/Client.H"

#include <dsp.h>

#include <algorithm>



/* supported lv2 features */
enum Plugin_Module_Supported_Features {
    Plugin_Feature_BufSize_Bounded = 0,
    Plugin_Feature_BufSize_Fixed,
    Plugin_Feature_Options,
    Plugin_Feature_URI_Map,
    Plugin_Feature_URID_Map,
    Plugin_Feature_URID_Unmap,
    Plugin_Features_Count
};

/* pre-mapped lv2 uris */
enum Plugin_Module_URIs {
    Plugin_Module_URI_Null = 0,
    Plugin_Module_URI_Atom_Double,
    Plugin_Module_URI_Atom_Int,
    Plugin_Module_URI_BufSize_MaxLength,
    Plugin_Module_URI_BufSize_MinLength,
    Plugin_Module_URI_Parameter_SampleRate,
    Plugin_Module_URI_Count
};

/* lv2 plugin options */
/* mostly empty for now, plugins usually require it because of buf-size extension */
struct Plugin_Module_Options {
    enum OptIndex {
        MaxBlockLenth = 0,
        MinBlockLenth,
        SampleRate,
        Null,
        Count
    };

    int maxBufferSize;
    int minBufferSize;
    double sampleRate;
    LV2_Options_Option opts[Count];

    Plugin_Module_Options()
    {
        maxBufferSize = 0;
        minBufferSize = 0;
        sampleRate = 0.0;

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
        optSampleRate.size    = sizeof(double);
        optSampleRate.type    = Plugin_Module_URI_Atom_Double;
        optSampleRate.value   = &sampleRate;

        LV2_Options_Option& optNull(opts[Null]);
        optNull.context = LV2_OPTIONS_INSTANCE;
        optNull.subject = 0;
        optNull.key     = Plugin_Module_URI_Null;
        optNull.size    = 0;
        optNull.type    = Plugin_Module_URI_Null;
        optNull.value   = NULL;
    }
};

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

/* keep this out of the header to avoid spreading ladspa.h dependency */
struct Plugin_Module::ImplementationData
{
    const LADSPA_Descriptor      *descriptor;
    struct {
        const LV2_Descriptor     *descriptor;
        const LV2_RDF_Descriptor *rdf_data;
        LV2_Feature*              features[Plugin_Features_Count+1];
        // extension data
        Plugin_Module_Options     options;
        std::vector<std::string>  mappedURIs;
        // extensions
        struct {
            const LV2_Options_Interface* options;
            const LV2_State_Interface*   state;
            const LV2_Worker_Interface*  worker;
        } ext;
    } lv2;
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
        lv2.mappedURIs.push_back(LV2_ATOM__Double);             // Plugin_Module_URI_Atom_Double
        lv2.mappedURIs.push_back(LV2_ATOM__Int);                // Plugin_Module_URI_Atom_Int
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__maxBlockLength); // Plugin_Module_URI_BufSize_MaxLength
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__minBlockLength); // Plugin_Module_URI_BufSize_MinLength
        lv2.mappedURIs.push_back(LV2_PARAMETERS__sampleRate);   // Plugin_Module_URI_Parameter_SampleRate

        lv2.ext.options = NULL;
        lv2.ext.state   = NULL;
        lv2.ext.worker  = NULL;
    }

    ~ImplementationData()
    {
        if ( lv2.rdf_data )
        {
            delete lv2.rdf_data;
            lv2.rdf_data = NULL;
        }

        if ( lv2.features[Plugin_Feature_URI_Map] && lv2.features[Plugin_Feature_URI_Map]->data )
            delete (LV2_URI_Map_Feature*)lv2.features[Plugin_Feature_URI_Map]->data;

        if ( lv2.features[Plugin_Feature_URID_Map] && lv2.features[Plugin_Feature_URID_Map]->data )
            delete (LV2_URID_Map*)lv2.features[Plugin_Feature_URID_Map]->data;

        if ( lv2.features[Plugin_Feature_URID_Unmap] && lv2.features[Plugin_Feature_URID_Unmap]->data )
            delete (LV2_URID_Unmap*)lv2.features[Plugin_Feature_URID_Unmap]->data;
    }

    // -------------------------------------------------------------------
    // LV2 Features

    static uint32_t _lv2_uri_to_id(LV2_URI_Map_Callback_Data data, const char*, const char* uri)
    {
        return _lv2_urid_map(data, uri);
    }

    static LV2_URID _lv2_urid_map(LV2_URID_Map_Handle handle, const char* uri)
    {
        if ( handle == NULL || ! uri )
            return Plugin_Module_URI_Null;
        if ( ::strcmp(uri, LV2_ATOM__Double) == 0 )
            return Plugin_Module_URI_Atom_Double;
        if ( ::strcmp(uri, LV2_ATOM__Int) == 0 )
            return Plugin_Module_URI_Atom_Int;
        if ( ::strcmp(uri, LV2_BUF_SIZE__maxBlockLength) == 0 )
            return Plugin_Module_URI_BufSize_MaxLength;
        if ( ::strcmp(uri, LV2_BUF_SIZE__minBlockLength) == 0 )
            return Plugin_Module_URI_BufSize_MinLength;
        if ( ::strcmp(uri, LV2_PARAMETERS__sampleRate) == 0 )
            return Plugin_Module_URI_Parameter_SampleRate;

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



Plugin_Module::Plugin_Module ( ) : Module( 50, 35, name() )
{
    init();

    color( fl_color_average(  fl_rgb_color( 0x99, 0x7c, 0x3a ), FL_BACKGROUND_COLOR, 1.0f ));
    
    end();

    log_create();
}

Plugin_Module::~Plugin_Module ( )
{
    log_destroy();
    plugin_instances( 0 );
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
}



void
Plugin_Module::init ( void )
{
    _latency = 0;
    _last_latency = 0;
    _idata = new Plugin_Module::ImplementationData();
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

    LV2_URI_Map_Feature* const uriMapFt = new LV2_URI_Map_Feature;
    uriMapFt->callback_data             = _idata;
    uriMapFt->uri_to_id                 = ImplementationData::_lv2_uri_to_id;

    LV2_URID_Map* const uridMapFt       = new LV2_URID_Map;
    uridMapFt->handle                   = _idata;
    uridMapFt->map                      = ImplementationData::_lv2_urid_map;

    LV2_URID_Unmap* const uridUnmapFt   = new LV2_URID_Unmap;
    uridUnmapFt->handle                 = _idata;
    uridUnmapFt->unmap                  = ImplementationData::_lv2_urid_unmap;

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
        if (const char* const name = lilvPlugin.get_name().as_string())
            pi.name = name;

        if (const char* const author = lilvPlugin.get_author_name().as_string())
            pi.author = author;

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
                if ( ! (h = _idata->lv2.descriptor->instantiate( _idata->lv2.descriptor, sample_rate(), _idata->lv2.rdf_data->Bundle, _idata->lv2.features ) ) )
                {
                    WARNING( "Failed to instantiate plugin" );
                    return false;
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
                    else if ( ! LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                        _idata->lv2.descriptor->connect_port( h, k, NULL );
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

   PresetList = _idata->lv2.rdf_data->PresetListStructs;

    _plugin_ins = _plugin_outs = 0;

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
                _idata->lv2.ext.worker = NULL;
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
                    _plugin_ins++;
                }
                else if (LV2_IS_PORT_OUTPUT(_idata->lv2.rdf_data->Ports[i].Types))
                {
                    _plugin_outs++;
                    add_port( Port( this, Port::OUTPUT, Port::AUDIO, _idata->lv2.rdf_data->Ports[i].Name ) );
                }
            }
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
                }

                /* Should always check toggled and enumeration after integer since 
                 some LV2s will have both */
                if ( LV2_IS_PORT_TOGGLED( rdfport.Properties ) )
                {
                    p.hints.type = Port::Hints::BOOLEAN;
                }
                if( LV2_IS_PORT_ENUMERATION(rdfport.Properties) )
                {
                    p.hints.type = Port::Hints::INTEGER;
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

    if (_is_lv2)
    {
        if ( ! (h = _idata->lv2.descriptor->instantiate( _idata->lv2.descriptor, sample_rate(), _idata->lv2.rdf_data->Bundle, _idata->lv2.features ) ) )
        {
            WARNING( "Failed to instantiate plugin" );
            return false;
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
            else if ( ! LV2_IS_PORT_AUDIO( _idata->lv2.rdf_data->Ports[k].Types ) )
                _idata->lv2.descriptor->connect_port( h, k, NULL );
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
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->lv2.descriptor->run( _idata->handle[i], nframes );
        }
        else
        {
            for ( unsigned int i = 0; i < _idata->handle.size(); ++i )
                _idata->descriptor->run( _idata->handle[i], nframes );
        }

        _latency = get_module_latency();
    }
}


