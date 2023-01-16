/*******************************************************************************/
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
 * File:   ImplementationData.H
 * Author: sspresto
 *
 * Created on January 15, 2023, 1:52 PM
 */

#pragma once

#include <ladspa.h>
#include "LV2_RDF_Utils.hpp"
#include <lv2/data-access/data-access.h>
#include <dlfcn.h>

#define LV2_WORKER_SUPPORT 1
#define LV2_MIDI_SUPPORT 1
#define USE_SUIL 1

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
    Plugin_Module_URI_patch_Property,
    Plugin_Module_URI_patch_Value,
    Plugin_Module_URI_patch_Body,
    Plugin_Module_URI_patch_Set,
    Plugin_Module_URI_patch_Put,
    Plugin_Module_URI_patch_Writable,
    Plugin_Module_URI_patch_Readable,
#endif
#ifdef USE_SUIL
    Plugin_Module_URI_Resize,
    Plugin_Module_URI_Scale,
#endif
#ifdef LV2_MIDI_SUPPORT
    Plugin_Module_URI_Midi_event,
    Plugin_Module_URI_time_Position,
    Plugin_Module_URI_time_frame,
    Plugin_Module_URI_time_speed,
    Plugin_Module_URI_time_barBeat,
    Plugin_Module_URI_time_bar,
    Plugin_Module_URI_time_beatUnit,
    Plugin_Module_URI_time_beatsPerBar,
    Plugin_Module_URI_time_beatsPerMinute,
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
#ifdef USE_SUIL
            LV2_Extension_Data_Feature ext_data;
            const LV2UI_Idle_Interface* idle_iface;
            const LV2UI_Show_Interface* ui_showInterface;
            const LV2UI_Resize* resize_ui;
#endif
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
        lv2.mappedURIs.push_back(LV2_ATOM__Bool);               // Plugin_Module_URI_Atom_Bool
        lv2.mappedURIs.push_back(LV2_ATOM__Double);             // Plugin_Module_URI_Atom_Double
        lv2.mappedURIs.push_back(LV2_ATOM__Int);                // Plugin_Module_URI_Atom_Int
        lv2.mappedURIs.push_back(LV2_ATOM__Float);              // Plugin_Module_URI_Atom_Float
        lv2.mappedURIs.push_back(LV2_ATOM__Long);               // Plugin_Module_URI_Atom_Long
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__maxBlockLength); // Plugin_Module_URI_BufSize_MaxLength
        lv2.mappedURIs.push_back(LV2_BUF_SIZE__minBlockLength); // Plugin_Module_URI_BufSize_MinLength
        lv2.mappedURIs.push_back(LV2_PARAMETERS__sampleRate);   // Plugin_Module_URI_Parameter_SampleRate
#ifdef LV2_WORKER_SUPPORT
        lv2.mappedURIs.push_back(LV2_ATOM__Sequence);
        lv2.mappedURIs.push_back(LV2_ATOM__Chunk);
        lv2.mappedURIs.push_back(LV2_ATOM__eventTransfer);
        lv2.mappedURIs.push_back(LV2_PATCH__property);
        lv2.mappedURIs.push_back(LV2_PATCH__value);
        lv2.mappedURIs.push_back(LV2_PATCH__body);
        lv2.mappedURIs.push_back(LV2_PATCH__Set);
        lv2.mappedURIs.push_back(LV2_PATCH__Put);
#endif
#ifdef USE_SUIL
        lv2.mappedURIs.push_back(LV2_UI__resize);
        lv2.mappedURIs.push_back(LV2_UI__scaleFactor);
#endif
#ifdef LV2_MIDI_SUPPORT
        lv2.mappedURIs.push_back(LV2_MIDI__MidiEvent);
        lv2.mappedURIs.push_back(LV2_TIME__Position);
        lv2.mappedURIs.push_back(LV2_TIME__frame);
        lv2.mappedURIs.push_back(LV2_TIME__speed);
        lv2.mappedURIs.push_back(LV2_TIME__barBeat);
        lv2.mappedURIs.push_back(LV2_TIME__bar);
        lv2.mappedURIs.push_back(LV2_TIME__beatUnit);
        lv2.mappedURIs.push_back(LV2_TIME__beatsPerBar);
        lv2.mappedURIs.push_back(LV2_TIME__beatsPerMinute);
#endif
        lv2.ext.options = NULL;
        lv2.ext.state   = NULL;
        lv2.ext.worker  = NULL;
        lv2.ext.idle_iface = NULL;
        lv2.ext.ui_showInterface = NULL;
        lv2.ext.resize_ui = NULL;
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
#ifdef LV2_WORKER_SUPPORT
        if ( ::strcmp(uri, LV2_ATOM__Sequence) == 0 )
            return Plugin_Module_URI_Worker_Schedule;
        if ( ::strcmp(uri, LV2_ATOM__Chunk) == 0 )
            return Plugin_Module_URI_Atom_Chunk;
        if ( ::strcmp(uri, LV2_ATOM__eventTransfer) == 0 )
            return Plugin_Module_URI_Atom_eventTransfer;
        if ( ::strcmp(uri, LV2_PATCH__property) == 0 )
            return Plugin_Module_URI_patch_Property;
        if ( ::strcmp(uri, LV2_PATCH__value) == 0 )
            return Plugin_Module_URI_patch_Value;
        if ( ::strcmp(uri, LV2_PATCH__body) == 0 )
            return Plugin_Module_URI_patch_Body;
        if ( ::strcmp(uri, LV2_PATCH__Set) == 0 )
            return Plugin_Module_URI_patch_Set;
        if ( ::strcmp(uri, LV2_PATCH__Put) == 0 )
            return Plugin_Module_URI_patch_Put;
        if ( ::strcmp(uri, LV2_PATCH__writable) == 0 )
            return Plugin_Module_URI_patch_Writable;
        if ( ::strcmp(uri, LV2_PATCH__readable) == 0 )
            return Plugin_Module_URI_patch_Readable;
#endif  // LV2_WORKER_SUPPORT
#ifdef  USE_SUIL
        if ( ::strcmp(uri, LV2_UI__resize) == 0 )
            return Plugin_Module_URI_Resize;
        if ( ::strcmp(uri, LV2_UI__scaleFactor) == 0 )
            return Plugin_Module_URI_Scale;
#endif
#ifdef LV2_MIDI_SUPPORT
        if ( ::strcmp(uri, LV2_MIDI__MidiEvent) == 0 )
            return Plugin_Module_URI_Midi_event;
        if ( ::strcmp(uri, LV2_TIME__Position) == 0 )
            return Plugin_Module_URI_time_Position;
        if ( ::strcmp(uri, LV2_TIME__frame) == 0 )
            return Plugin_Module_URI_time_frame;
        if ( ::strcmp(uri, LV2_TIME__speed) == 0 )
            return Plugin_Module_URI_time_speed;
        if ( ::strcmp(uri, LV2_TIME__barBeat) == 0 )
            return Plugin_Module_URI_time_barBeat;
        if ( ::strcmp(uri, LV2_TIME__bar) == 0 )
            return Plugin_Module_URI_time_bar;
        if ( ::strcmp(uri, LV2_TIME__beatUnit) == 0 )
            return Plugin_Module_URI_time_beatUnit;
        if ( ::strcmp(uri, LV2_TIME__beatsPerBar) == 0 )
            return Plugin_Module_URI_time_beatsPerBar;
        if ( ::strcmp(uri, LV2_TIME__beatsPerMinute) == 0 )
            return Plugin_Module_URI_time_beatsPerMinute;
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

/* handy class to handle lv2 open/close */
class LV2_Lib_Manager
{
public:
    LV2_Lib_Manager() {}

    ~LV2_Lib_Manager()
    {
        for (std::vector<LV2LibraryInfo>::iterator it=libraries.begin(), end=libraries.end(); it != end; ++it)
        {
            LV2LibraryInfo& libinfo (*it);

            if ( libinfo.handle )
                dlclose( libinfo.handle );
        }

        libraries.clear();
    }

    const LV2_Descriptor* get_descriptor_for_uri(const std::string& binary, const char* uri)
    {
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

                LV2LibraryInfo info;
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
    struct LV2LibraryInfo {
        std::string binary;
        void*       handle;
        const LV2_Descriptor* desc;
    };
    std::vector<LV2LibraryInfo> libraries;
};

