
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

#define HAVE_LIBLRDF 1

static LADSPAInfo *ladspainfo;
Thread* Plugin_Module::plugin_discover_thread;

static bool warn_legacy_once = false;

Plugin_Module::Plugin_Module ( ) : Module( 50, 35, name() )
{
    color( fl_color_average(  fl_rgb_color( 0x99, 0x7c, 0x3a ), FL_BACKGROUND_COLOR, 1.0f ));
    
    end();

    log_create();
}

Plugin_Module::~Plugin_Module ( )
{
    log_destroy();
}

void
Plugin_Module::init ( void )
{
    if(!_is_lv2)
    {
        _ladspainfo = ladspainfo;
    }

    _latency = 0;
    _last_latency = 0;
    /* module will be bypassed until plugin is loaded */
    *((float*)_bypass) = 1.0f;

    _crosswire = false;

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
    else if ( n > plugin_ins() &&
              ( plugin_ins() == 1 && plugin_outs() == 1 ) )
    {
        return n;
    }

    return -1;
}

bool
Plugin_Module::configure_inputs( int /*n*/ )
{
    return false;
}

#ifdef LV2_MIDI_SUPPORT
void
Plugin_Module::configure_midi_inputs ()
{
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
Plugin_Module::configure_midi_outputs ()
{
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
         i!=plugins.end(); ++i, j++)
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
                if (lilvPort.supports_event(lv2World.midi_event) || lilvPort.supports_event(lv2World.time_position))
                {
#ifndef LV2_MIDI_SUPPORT
                    supported = false;
                    break;  
#endif
                }
                // supported or optional
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
         i !=pe.end(); ++i )
    {
        for ( std::list<Plugin_Info>::iterator j = pr.begin(); j != pr.end(); ++j )
        {
            if ( j->id == i->UniqueID )
            {
                j->category = i->Category;
            }
        }
    }

    return pr;
}

void
Plugin_Module::resize_buffers ( nframes_t buffer_size )
{
    Module::resize_buffers( buffer_size );
}

void
Plugin_Module::set ( Log_Entry & )
{
    if(!warn_legacy_once)
    {
        fl_alert( "Non-mixer ERROR - This snapshot contains legacy unsupported modules.\n"
                "See Help/Projects to convert to the new format!" );

        warn_legacy_once = true;
    }
}
