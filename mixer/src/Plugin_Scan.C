/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   Plugin_Scan.C
 * Author: sspresto
 * 
 * Created on January 27, 2024, 4:53 PM
 */

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Window.H>
#include "Plugin_Scan.H"
#include "../../nonlib/debug.h"

// Global cache of all plugins scanned
std::list<Plugin_Info> g_plugin_cache;

#ifdef LADSPA_SUPPORT
#define HAVE_LIBLRDF 1
static LADSPAInfo *ladspainfo = nullptr;
#endif

#ifdef LV2_SUPPORT
#include "lv2/LV2_RDF_Utils.hpp"
#endif

#ifdef CLAP_SUPPORT
    #include "clap/Clap_Discovery.H"
    static std::list<Plugin_Info> clap_PI_cache;
#endif

#ifdef VST2_SUPPORT
    #include "vst2/Vst2_Discovery.H"
    static std::list<Plugin_Info> vst2_PI_cache;
#endif

#ifdef VST3_SUPPORT
    #include "vst3/Vst3_Discovery.H"
    static std::list<Plugin_Info> vst3_PI_cache;
#endif
    
static Fl_Window * g_scanner_window = 0;
    
static void scanner_timeout(void*)
{
    g_scanner_window->redraw();
    g_scanner_window->show();
}

Plugin_Scan::Plugin_Scan() :
    _box(nullptr)
{
    g_scanner_window = new Fl_Window(600,100,"Scanning Plugins");
    _box = new Fl_Box(20,10,560,80,"Scanning");
    _box->box(FL_UP_BOX);
    _box->labelsize(14);
    _box->labelfont(FL_BOLD+FL_ITALIC);
    _box->labeltype(FL_SHADOW_LABEL);
    _box->show();
    g_scanner_window->end();
    g_scanner_window->set_modal();
}

Plugin_Scan::~Plugin_Scan()
{
}

void 
Plugin_Scan::close_scanner_window()
{
    g_scanner_window->hide();
    delete g_scanner_window;
    g_scanner_window = 0;
}

#ifdef LADSPA_SUPPORT
// For the LADSPA_Plugin class to avoid rescanning
void
Plugin_Scan::set_ladspainfo( LADSPAInfo * linfo )
{
    ladspainfo = linfo;
}

LADSPAInfo *
Plugin_Scan::get_ladspainfo()
{
    return ladspainfo;
}
#endif

/* Set global list of available plugins */
void
Plugin_Scan::get_all_plugins ( void )
{
    // Did we already scan? If so then don't do it again.
    if ( !g_plugin_cache.empty() )
        return;

    Fl::add_timeout(0.03, scanner_timeout);

    std::list<Plugin_Info> pr;

    Plugin_Scan pm;
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

    // Set the global cache
    if ( !pr.empty() )
    {
        g_plugin_cache.insert(std::end(g_plugin_cache), std::begin(pr), std::end(pr));
    }

    close_scanner_window();
}

#ifdef LADSPA_SUPPORT
void
Plugin_Scan::scan_LADSPA_plugins( std::list<Plugin_Info> & pr )
{
    if ( !ladspainfo )
    {
        ladspainfo = new LADSPAInfo();
    }

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
        
        if(_box)
        {
            _box->copy_label(pi.name.c_str());
            _box->redraw();
            Fl::check();
        }
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
Plugin_Scan::scan_LV2_plugins( std::list<Plugin_Info> & pr )
{
    Lv2WorldClass::getInstance().initIfNeeded(/*::getenv("LV2_PATH")*/);
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
        
        if(_box)
        {
            _box->copy_label(pi.name.c_str());
            _box->redraw();
            Fl::check();
        }
    }
}
#endif  // LV2_SUPPORT

#ifdef CLAP_SUPPORT
void
Plugin_Scan::scan_CLAP_plugins( std::list<Plugin_Info> & pr )
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

        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
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
Plugin_Scan::scan_VST2_plugins( std::list<Plugin_Info> & pr )
{
    if ( !vst2_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst2_PI_cache), std::end(vst2_PI_cache));
        return;
    }

    auto sp = vst2_discovery::installedVST2s();   // This to get paths

    for (const auto &q : sp)
    {
        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
        }
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
Plugin_Scan::scan_VST3_plugins( std::list<Plugin_Info> & pr )
{
    if ( !vst3_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst3_PI_cache), std::end(vst3_PI_cache));
        return;
    }

    auto sp = vst3_discovery::installedVST3s();   // This to get paths

    for (const auto &q : sp)
    {
        if(_box)
        {
            _box->copy_label(q.u8string().c_str());
            _box->redraw();
            Fl::check();
        }
        vst3_discovery::vst3_discovery_scan_file( q.u8string().c_str(), vst3_PI_cache);
    }

    if ( !vst3_PI_cache.empty() )
    {
        pr.insert(std::end(pr), std::begin(vst3_PI_cache), std::end(vst3_PI_cache));
        return;
    }
}
#endif  // VST3_SUPPORT
