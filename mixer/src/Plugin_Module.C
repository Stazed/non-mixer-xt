
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
#include <string>
#include <stdlib.h>
#include <FL/fl_draw.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_ask.H>

#include "Plugin_Module.H"
#include "Mixer_Strip.H"
#include "Chain.H"

#include "../../nonlib/debug.h"

#ifdef LADSPA_SUPPORT
#define HAVE_LIBLRDF 1
static LADSPAInfo *ladspainfo;
#endif

#ifdef CLAP_SUPPORT
    #include "clap/Clap_Discovery.H"
    static std::list<Plugin_Module::Plugin_Info> clap_PI_cache;
#endif

#ifdef VST2_SUPPORT
    #include "vst2/Vst2_Discovery.H"
    static std::list<Plugin_Module::Plugin_Info> vst2_PI_cache;
#endif

#ifdef VST3_SUPPORT
    #include "vst3/Vst3_Discovery.H"
    static std::list<Plugin_Module::Plugin_Info> vst3_PI_cache;
#endif

Thread* Plugin_Module::plugin_discover_thread;

static bool warn_legacy_once = false;

Plugin_Module::Plugin_Module ( ) :
    Module( 50, 35, name() ),
    _last_latency(0),
#ifdef LADSPA_SUPPORT
    _ladspainfo(nullptr),
#endif
    _plugin_ins(0),
    _plugin_outs(0),
    _crosswire(false),
    _latency(0)
{
    color( fl_color_average(  fl_rgb_color( 0x99, 0x7c, 0x3a ), FL_BACKGROUND_COLOR, 1.0f ));
    
    end();

    Plugin_Module::init();
    log_create();
}

Plugin_Module::~Plugin_Module ( )
{
    log_destroy();
}

#ifdef LADSPA_SUPPORT
void
Plugin_Module::set_ladspainfo( void )
{
    _ladspainfo = ladspainfo;
}
#endif

void
Plugin_Module::init ( void )
{
    /* module will be bypassed until plugin is loaded */
    *((float*)_bypass) = 1.0f;

    align( (Fl_Align)FL_ALIGN_CENTER | FL_ALIGN_INSIDE );
//     color( (Fl_Color)fl_color_average( FL_MAGENTA, FL_WHITE, 0.5f ) );

    int tw, th, tx, ty;

    bbox( tx, ty, tw, th );
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
    /* The synth case, 0 ins any outs. For these we only allow to add
       a zero synth if the JACK ins are 1. ie. n == 1 */
    if(plugin_ins() == 0 && (n == 1) )
        return plugin_outs();
    
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
    /* Only for LADSPA and LV2 */
    else if ( n > plugin_ins() &&
              ( plugin_ins() == 1 && plugin_outs() == 1 ) )
    {
        if( (_plug_type == Type_CLAP) || (_plug_type == Type_VST2) ||
               (_plug_type == Type_VST3) )
        {
            return -1;  // Don't support multiple instances
        }
        else    // LADSPA & LV2 can run multiple instance
            return n;
    }

    return -1;
}

bool
Plugin_Module::configure_inputs( int /*n*/ )
{
    return false;
}

void *
Plugin_Module::discover_thread ( void * )
{
    THREAD_ASSERT( Plugin_Discover );

    DMESSAGE( "Discovering plugins in the background" );
#ifdef LADSPA_SUPPORT
    ladspainfo = new LADSPAInfo();
#endif

#ifdef LV2_SUPPORT
    Lv2WorldClass::getInstance().initIfNeeded(/*::getenv("LV2_PATH")*/);
#endif

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
#ifdef LADSPA_SUPPORT
    if ( !ladspainfo )
    {
        if ( ! plugin_discover_thread )
            ladspainfo = new LADSPAInfo();
        else
            plugin_discover_thread->join();
    }
#endif
    std::list<Plugin_Module::Plugin_Info> pr;

    Plugin_Module pm;
#ifdef LADSPA_SUPPORT
    pm.scan_LADSPA_plugins( pr );   // Scan LADSPA
#endif
#ifdef LV2_SUPPORT
    pm.scan_LV2_plugins( pr );      // Scan LV2
#endif
#ifdef CLAP_SUPPORT
    pm.scan_CLAP_plugins( pr );     // Scan CLAP
#endif
#ifdef VST2_SUPPORT
    pm.scan_VST2_plugins( pr );     // Scan VST2
#endif
#ifdef VST3_SUPPORT
    pm.scan_VST3_plugins( pr );     // Scan VST3
#endif

    pr.sort();

    return pr;
}

#ifdef LADSPA_SUPPORT
void
Plugin_Module::scan_LADSPA_plugins( std::list<Plugin_Info> & pr )
{
    std::vector<LADSPAInfo::PluginInfo> plugins = ladspainfo->GetPluginInfo();

    int j = 0;
    for (std::vector<LADSPAInfo::PluginInfo>::iterator i=plugins.begin();
         i!=plugins.end(); ++i, j++)
    {
        Plugin_Info pi("LADSPA");

        pi.s_unique_id = "(null)";  // (null) since we have to have something for favorites save and scan
        pi.id = i->UniqueID;
        pi.author = i->Maker;
        pi.name = i->Name;
        pi.audio_inputs = i->AudioInputs;
        pi.audio_outputs = i->AudioOutputs;
        pi.category = "Unclassified";
        pr.push_back( pi );
    }

    /* Set the plugin category since the above scan does not set it */
    const std::vector<LADSPAInfo::PluginEntry> pe = ladspainfo->GetMenuList();
  
    for (std::vector<LADSPAInfo::PluginEntry>::const_iterator i= pe.begin();
         i !=pe.end(); ++i )
    {
        for ( std::list<Plugin_Info>::iterator k = pr.begin(); k != pr.end(); ++k )
        {
            if ( k->id == i->UniqueID )
            {
                k->category = i->Category;
            }
        }
    }
}
#endif  // LADSPA_SUPPORT

#ifdef LV2_SUPPORT
void
Plugin_Module::scan_LV2_plugins( std::list<Plugin_Info> & pr )
{
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
            if ( ::strcmp( featureURI, LV2_WORKER__schedule ) == 0 )
            {
              //  DMESSAGE("GOT Worker.schedule = %s", lilvPlugin.get_name().as_string());
                continue;
            }

            supported = false;
            break;
        }

        lilv_nodes_free(const_cast<LilvNodes*>(featureNodes.me));

        if ( ! supported )
            continue;

        Plugin_Info pi("LV2");
        
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
            else if (lilvPort.is_a(lv2World.port_atom))
            {
                if (lilvPort.supports_event(lv2World.midi_event) || lilvPort.supports_event(lv2World.time_position))
                {
                    // supported
                }
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
        LilvNode* name_node = lilv_plugin_get_name(lilvPlugin);
        const char* const s_name = lilv_node_as_string( name_node );
        if( s_name )
        {
            pi.name = s_name;
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
        pi.s_unique_id = lilvPlugin.get_uri().as_uri();
        pi.id = 0;
        pi.category = "Unclassified";   // Default

        /* Use existing LADSPA table categories for Plugin_Chooser lookup categories */
        if (const char* const category = lilvPlugin.get_class().get_label().as_string())
        {
            pi.category = category;
            for(unsigned k = 0; k < type_matches.size(); ++k)
            {
                if(!strcmp(type_matches[k].LV2_type.c_str(), pi.category.c_str()))
                    pi.category = type_matches[k].cat_type;
            }
        }

        pr.push_back( pi );
    }
}
#endif  // LV2_SUPPORT

#ifdef CLAP_SUPPORT
void
Plugin_Module::scan_CLAP_plugins( std::list<Plugin_Info> & pr )
{
    if ( !clap_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(clap_PI_cache), std::end(clap_PI_cache));
        return;
    }

    auto sp = clap_discovery::installedCLAPs();   // This to get paths

    for (const auto &q : sp)
    {
       // DMESSAGE("CLAP PLUG PATHS %s", q.u8string().c_str());
        auto entry = clap_discovery::entryFromCLAPPath(q);

        if (!entry)
        {
            DMESSAGE("Clap_entry returned a nullptr = %s", q.u8string().c_str());
            continue;
        }

        if ( !entry->init(q.u8string().c_str()) )  // This could be bundle
        {
            DMESSAGE("Could not initialize entry = %s", q.u8string().c_str());
            continue;
        }

        auto fac = static_cast<const clap_plugin_factory_t *>( entry->get_factory(CLAP_PLUGIN_FACTORY_ID) );
        
        if ( !fac )
        {
            DMESSAGE("Plugin factory is null %s", q.u8string().c_str());
            entry->deinit();
            continue;
        }
        
        auto plugin_count = fac->get_plugin_count(fac);     // how many in the bundle

        if (plugin_count <= 0)
        {
            DMESSAGE("Plugin factory has no plugins = %s: Count = %d", q.u8string().c_str(), plugin_count);
            entry->deinit();
            continue;
        }

        for (uint32_t pl = 0; pl < plugin_count; ++pl)
        {
            auto desc = fac->get_plugin_descriptor(fac, pl);

            Plugin_Info pi("CLAP");

            pi.name         = desc->name;
            pi.s_unique_id  = desc->id;
            pi.author       = desc->vendor;
            pi.id           = 0;
            pi.plug_path    = q.u8string().c_str();
            pi.category     = clap_discovery::get_plugin_category(desc->features);
            // desc->version;
            // desc->description;

            // Now lets make an instance to query ports
            auto host = clap_discovery::createCLAPInfoHost();
            clap_discovery::getHostConfig()->announceQueriedExtensions = false;
            auto inst = fac->create_plugin(fac, host, desc->id);

            if (!inst)
            {
                DMESSAGE("CLAP Plugin instance is null: %s", desc->name);
                continue;
            }

            if( !inst->init(inst) )
            {
                DMESSAGE("CLAP unable to initialize plugin: %s", desc->name);
                inst->destroy(inst);
                continue;
            }

            const clap_plugin_audio_ports_t *audio_ports
			= static_cast<const clap_plugin_audio_ports_t *> (
				inst->get_extension(inst, CLAP_EXT_AUDIO_PORTS));

            if (audio_ports && audio_ports->count && audio_ports->get)
            {
                clap_audio_port_info info;
                const uint32_t nins = audio_ports->count(inst, true);
                for (uint32_t i = 0; i < nins; ++i)
                {
                    ::memset(&info, 0, sizeof(info));
                    if (audio_ports->get(inst, i, true, &info))
                    {
                        pi.audio_inputs += info.channel_count;
                    }
                }

                const uint32_t nouts = audio_ports->count(inst, false);
                for (uint32_t i = 0; i < nouts; ++i)
                {
                    ::memset(&info, 0, sizeof(info));
                    if (audio_ports->get(inst, i, false, &info))
                    {
                        pi.audio_outputs += info.channel_count;
                    }
                }
            }

            inst->destroy(inst);

            clap_PI_cache.push_back( pi );

        //    DMESSAGE("Name = %s: Path = %s: ID = %d: Audio Ins = %d: Audio Outs = %d",
        //            pi.name.c_str(), pi.plug_path.c_str(), pi.id, pi.audio_inputs, pi.audio_outputs);
        }
    }

    if ( !clap_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(clap_PI_cache), std::end(clap_PI_cache));
        return;
    }
}
#endif  // CLAP_SUPPORT

#ifdef VST2_SUPPORT
void
Plugin_Module::scan_VST2_plugins( std::list<Plugin_Info> & pr )
{
    if ( !vst2_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst2_PI_cache), std::end(vst2_PI_cache));
        return;
    }

    auto sp = vst2_discovery::installedVST2s();   // This to get paths

    for (const auto &q : sp)
    {
        vst2_discovery::vst2_discovery_scan_file( q.u8string().c_str(), vst2_PI_cache);
    }

    if ( !vst2_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst2_PI_cache), std::end(vst2_PI_cache));
        return;
    }
}
#endif  // VST2_SUPPORT

#ifdef VST3_SUPPORT
void
Plugin_Module::scan_VST3_plugins( std::list<Plugin_Info> & pr )
{
    if ( !vst3_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst3_PI_cache), std::end(vst3_PI_cache));
        return;
    }

    auto sp = vst3_discovery::installedVST3s();   // This to get paths

    for (const auto &q : sp)
    {
        vst3_discovery::vst3_discovery_scan_file( q.u8string().c_str(), vst3_PI_cache);
    }

    if ( !vst3_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst3_PI_cache), std::end(vst3_PI_cache));
        return;
    }
}
#endif  // VST3_SUPPORT

void
Plugin_Module::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );
}

/**
 This generates the plugin state save file/directory we use for customData.
 It generates the random directory suffix '.nABCD' to support multiple instances.
 */
std::string
Plugin_Module::get_custom_data_location(const std::string &path)
{
    std::string project_base = path;

    if(project_base.empty())
        return "";

    std::string slabel = "/";
    slabel += label();

    /* Just replacing spaces in the plugin label with '_' cause it looks better */
    std::replace( slabel.begin(), slabel.end(), ' ', '_');

    project_base += slabel;

    /* This generates the random directory suffix '.nABCD' to support multiple instances */
    char id_str[6];

    id_str[0] = 'n';
    id_str[5] = 0;

    for ( int i = 1; i < 5; i++)
        id_str[i] = 'A' + (rand() % 25);

    project_base += ".";
    project_base += id_str;

    DMESSAGE("project_base = %s", project_base.c_str());

    return project_base;
}

void
Plugin_Module::set ( Log_Entry & )
{
    if(!warn_legacy_once)
    {
        fl_alert( "Non-mixer-xt ERROR - This snapshot contains legacy unsupported modules.\n"
                "See Help/Projects to convert to the new format!" );

        warn_legacy_once = true;
    }
}
