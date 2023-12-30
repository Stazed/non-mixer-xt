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
 * File:   Vst3_Discovery.C
 * Author: sspresto
 * 
 * Created on December 16, 2023, 5:44 AM
 */

#ifdef VST3_SUPPORT

#include <iostream>     // cerr
#include <dlfcn.h>      // dlopen, dlerror, dlsym
#include <cstring>      // strcmp
#include <algorithm>    // transform, toLower

#include "Vst3_Discovery.H"
#include "../../../nonlib/debug.h"
#include "travesty/host.h"
#include "utils/CarlaVst3Utils.hpp"

#ifdef CONFIG_VST3
#include "pluginterfaces/vst/ivsthostapplication.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include "pluginterfaces/gui/iplugview.h"
#endif

namespace vst3_discovery
{
#ifndef CONFIG_VST3
#define MAX_DISCOVERY_AUDIO_IO 64
#define MAX_DISCOVERY_CV_IO 32
static constexpr const uint PLUGIN_HAS_CUSTOM_UI = 0x008;
static constexpr const uint PLUGIN_HAS_CUSTOM_EMBED_UI = 0x1000;
static constexpr const uint PLUGIN_IS_SYNTH = 0x004;
static constexpr const uint32_t kBufferSize  = 512;
static constexpr const double   kSampleRate  = 44100.0;
#endif

std::vector<std::filesystem::path>
installedVST3s()
{
    auto sp = validVST3SearchPaths();
    std::vector<std::filesystem::path> vst3s;

    for (const auto &p : sp)
    {
        DMESSAGE("VST3 PLUG PATHS %s", p.u8string().c_str());
        try
        {
            for (auto const &dir_entry : std::filesystem::recursive_directory_iterator(p))
            {
                if (dir_entry.path().extension().u8string() == ".vst3")
                {
                    if (std::filesystem::is_directory(dir_entry.path()))
                    {
                        vst3s.emplace_back(dir_entry.path());
                    }
                }
            }
        }

        catch (const std::filesystem::filesystem_error &)
        {
            MESSAGE("Vst3 path directory not found - %s", p.u8string().c_str());
        }
    }
    return vst3s;
}

/**
 * This returns all the search paths for VST3 plugin locations.
 * @return 
 *      vector of filesystem::path of vst3 locations.
 */

std::vector<std::filesystem::path>
validVST3SearchPaths()
{
    std::vector<std::filesystem::path> res;

    /* These are the standard locations for linux */
    res.emplace_back("/usr/lib/vst3");

    // some distros make /usr/lib64 a symlink to /usr/lib so don't include it
    // or we get duplicates.
    if(std::filesystem::is_symlink("/usr/lib64"))
    {
        if(!strcmp(std::filesystem::read_symlink("/usr/lib64").c_str(), "/usr/lib"))
            res.emplace_back("/usr/lib64/vst3");
    }
    else
    {
        res.emplace_back("/usr/lib64/vst3");
    }
    
    res.emplace_back("/usr/local/lib/vst3");

    // some distros make /usr/local/lib64 a symlink to /usr/local/lib so don't include it
    // or we get duplicates.
    if(std::filesystem::is_symlink("/us/local/lib64"))
    {
        if(!strcmp(std::filesystem::read_symlink("/usr/local/lib64").c_str(), "/usr/local/lib"))
            res.emplace_back("/usr/local/lib64/vst3");
    }
    else
    {
        res.emplace_back("/usr/local/lib64/vst3");
    }

    res.emplace_back(std::filesystem::path(getenv("HOME")) / std::filesystem::path(".vst3"));

    return res;
}


#ifdef CONFIG_VST3

static inline
std::string getCategoryFromName(const std::string &name) noexcept
{
    if ( name.empty() )
        return "Unclassified";

    std::string sname(name);

    std::transform(sname.begin(), sname.end(), sname.begin(), [](unsigned char c){ return std::tolower(c); });
    
    // generic tags first
    if (sname.find("delay") != std::string::npos)
        return "Time/Delays";
    if (sname.find("reverb") != std::string::npos)
        return "Simulators/Reverbs";

    // filter
    if (sname.find("filter") != std::string::npos)
        return "Frequency/Filters";

    // distortion
    if (sname.find("distortion") != std::string::npos)
        return "Amplitude/Distortions";

    // dynamics
    if (sname.find("dynamics") != std::string::npos)
        return "Amplitude/Dynamics";
    if (sname.find("amplifier") != std::string::npos)
        return "Amplitude/Dynamics";
    if (sname.find("compressor") != std::string::npos)
        return "Amplitude/Dynamics/Compressors";
    if (sname.find("enhancer") != std::string::npos)
        return "Amplitude/Dynamics";
    if (sname.find("exciter") != std::string::npos)
        return "Amplitude/Dynamics";
    if (sname.find("gate") != std::string::npos)
        return "Amplitude/Dynamics";
    if (sname.find("limiter") != std::string::npos)
        return "Amplitude/Dynamics/Limiters";

    // modulator
    if (sname.find("modulator") != std::string::npos)
        return "Amplitude/Modulators";
    if (sname.find("chorus") != std::string::npos)
        return "Amplitude/Modulators";
    if (sname.find("flanger") != std::string::npos)
        return "Time/Flangers" ;
    if (sname.find("phaser") != std::string::npos)
        return "Time/Phasers";
    if (sname.find("saturator") != std::string::npos)
        return "Amplitude/Modulators";

    // utility
    if (sname.find("utility") != std::string::npos)
        return "Utilities";
    if (sname.find("analyzer") != std::string::npos)
        return "Analyser Plugin";
    if (sname.find("converter") != std::string::npos)
        return "Utilities";
    if (sname.find("deesser") != std::string::npos)
        return "Utilities";
    if (sname.find("mixer") != std::string::npos)
        return "Utilities";

    // common tags
    if (sname.find("verb") != std::string::npos)
        return "Simulators/Reverbs";

    if (sname.find("eq") != std::string::npos)
        return "Frequency/EQs";

    if (sname.find("tool") != std::string::npos)
        return "Utilities";

    // synth
    if (sname.find("synth") != std::string::npos)
        return "Instrument Plugin";

    // other
    if (sname.find("misc") != std::string::npos)
        return "Unclassified";
    if (sname.find("other") != std::string::npos)
        return "Unclassified";

    return "Unclassified";
}


//-----------------------------------------------------------------------------

using namespace Steinberg;

//-----------------------------------------------------------------------------

class qtractor_vst3_scan_host : public Vst::IHostApplication
{
public:

    qtractor_vst3_scan_host ()
    {
        FUNKNOWN_CTOR
    }

    virtual ~qtractor_vst3_scan_host ()
    {
        FUNKNOWN_DTOR
    }

    DECLARE_FUNKNOWN_METHODS

    //--- IHostApplication ----
    //
    tresult PLUGIN_API getName (Vst::String128 name) override
    {
        //const QString str("qtractor_plugin_scan");
        const std::string str("non-mixer-xt");
        //const int nsize = qMin(str.length(), 127);
        const int nsize = str.length() < 127 ? str.length() : 127;
        ::memcpy(name, str.c_str(), nsize * sizeof(Vst::TChar));
        name[nsize] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance (TUID /*cid*/, TUID /*_iid*/, void **obj) override
    {
        *obj = nullptr;
        return kResultFalse;
    }

    FUnknown *get() { return static_cast<Vst::IHostApplication *> (this); }
};


tresult PLUGIN_API qtractor_vst3_scan_host::queryInterface (
	const char *_iid, void **obj )
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IHostApplication)
    QUERY_INTERFACE(_iid, obj, IHostApplication::iid, IHostApplication)

    *obj = nullptr;
    return kNoInterface;
}


uint32 PLUGIN_API qtractor_vst3_scan_host::addRef (void)
    { return 1;	}

uint32 PLUGIN_API qtractor_vst3_scan_host::release (void)
    { return 1; }


static qtractor_vst3_scan_host g_vst3HostContext;


//----------------------------------------------------------------------
// class qtractor_vst3_scan::Impl -- VST3 plugin interface impl.
//

class qtractor_vst3_scan::Impl
{
public:

    // Constructor.
    Impl() : m_module(nullptr), m_component(nullptr), m_controller(nullptr) {}

    // destructor.
    ~Impl() { close_descriptor(); close(); }

    // File loader.
    bool open ( const std::string& sFilename )
    {
        close();

        DMESSAGE("Open %s", sFilename.c_str());

        m_module = ::dlopen(sFilename.c_str(), RTLD_LOCAL | RTLD_LAZY);
        if (!m_module)
            return false;

        typedef bool (*VST3_ModuleEntry)(void *);
        const VST3_ModuleEntry module_entry
                = VST3_ModuleEntry(::dlsym(m_module, "ModuleEntry"));

        if (module_entry)
            module_entry(m_module);

        return true;
    }

    bool open_descriptor ( unsigned long iIndex )
    {
        if (!m_module)
            return false;

        close_descriptor();

        typedef IPluginFactory *(*VST3_GetFactory)();
        const VST3_GetFactory get_plugin_factory
                = VST3_GetFactory(::dlsym(m_module, "GetPluginFactory"));
        if (!get_plugin_factory)
        {

            DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                    " *** Failed to resolve plug-in factory.", this, iIndex);

            return false;
        }

        IPluginFactory *factory = get_plugin_factory();
        if (!factory)
        {
            DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                    " *** Failed to retrieve plug-in factory.", this, iIndex);

            return false;
        }

        PFactoryInfo FI;
        factory->getFactoryInfo(&FI);

        m_factoryInfo = FI;

        IPluginFactory2 *factory2 = FUnknownPtr<IPluginFactory2> (factory);
        IPluginFactory3 *factory3 = FUnknownPtr<IPluginFactory3> (factory);
        
        if (factory3)
            factory3->setHostContext(g_vst3HostContext.get());

        const int32 nclasses = factory->countClasses();

        unsigned long i = 0;

        for (int32 n = 0; n < nclasses; ++n)
        {

            PClassInfo classInfo;
            if (factory->getClassInfo(n, &classInfo) != kResultOk)
                continue;

            if (::strcmp(classInfo.category, kVstAudioEffectClass))
                    continue;

            if (iIndex == i)
            {
                PClassInfoW classInfoW;
                if (factory3 && factory3->getClassInfoUnicode(n, &classInfoW) == kResultOk)
                {
                    m_sSubCategories = classInfoW.subCategories;
                } else 
                {
                    PClassInfo2 classInfo2;
                    if (factory2 && factory2->getClassInfo2(n, &classInfo2) == kResultOk)
                    {
                        m_sSubCategories = classInfo2.subCategories;
                    } else
                    {
                        m_sSubCategories = "Unclassified";
                    }
                }

                m_classInfo = classInfo;

                Vst::IComponent *component = nullptr;
                if (factory->createInstance(
                                classInfo.cid, Vst::IComponent::iid,
                                (void **) &component) != kResultOk)
                {
                    DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                            " *** Failed to create plug-in component.", this, iIndex);

                    return false;
                }

                m_component = owned(component);

                if (m_component->initialize(g_vst3HostContext.get()) != kResultOk)
                {
                    DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                            " *** Failed to initialize plug-in component.", this, iIndex);
                    close_descriptor();
                    return false;
                }

                Vst::IEditController *controller = nullptr;
                if (m_component->queryInterface(
                                Vst::IEditController::iid,
                                (void **) &controller) != kResultOk)
                {
                    TUID controller_cid;
                    if (m_component->getControllerClassId(controller_cid) == kResultOk)
                    {
                        if (factory->createInstance(
                                        controller_cid, Vst::IEditController::iid,
                                        (void **) &controller) != kResultOk)
                        {
                            DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                                    " *** Failed to create plug-in controller.", this, iIndex);
                        }
                        if (controller &&
                                controller->initialize(g_vst3HostContext.get()) != kResultOk)
                        {
                            DMESSAGE("qtractor_vst3_scan::Impl[%p]::open_descriptor(%lu)"
                                    " *** Failed to initialize plug-in controller.", this, iIndex);
                            controller = nullptr;
                        }
                    }
                }

                if (controller) m_controller = owned(controller);

                // Connect components...
                if (m_component && m_controller)
                {
                    FUnknownPtr<Vst::IConnectionPoint> component_cp(m_component);
                    FUnknownPtr<Vst::IConnectionPoint> controller_cp(m_controller);
                    if (component_cp && controller_cp)
                    {
                        component_cp->connect(controller_cp);
                        controller_cp->connect(component_cp);
                    }
                }

                return true;
            }

            ++i;
        }

        return false;
    }

    void close_descriptor ()
    {
        if (m_component && m_controller)
        {
            FUnknownPtr<Vst::IConnectionPoint> component_cp(m_component);
            FUnknownPtr<Vst::IConnectionPoint> controller_cp(m_controller);
            if (component_cp && controller_cp)
            {
                component_cp->disconnect(controller_cp);
                controller_cp->disconnect(component_cp);
            }
        }

        if (m_component && m_controller &&
                FUnknownPtr<Vst::IEditController> (m_component).getInterface())
        {
            m_controller->terminate();
        }

        m_controller = nullptr;

        if (m_component)
        {
            m_component->terminate();
            m_component = nullptr;
        }
    }

    void close ()
    {
        if (!m_module)
            return;

        typedef void (*VST3_ModuleExit)();
        const VST3_ModuleExit module_exit
                = VST3_ModuleExit(::dlsym(m_module, "ModuleExit"));

        if (module_exit)
            module_exit();

        ::dlclose(m_module);
        m_module = nullptr;
    }

    // Accessors.
    Vst::IComponent *component() const
        { return m_component; }

    Vst::IEditController *controller() const
        { return m_controller; }

    const PClassInfo& classInfo() const
        { return m_classInfo; }

    const PFactoryInfo& factoryInfo() const
        { return m_factoryInfo; }

    std::string subCategory() 
        { return m_sSubCategories; }

    int numChannels ( Vst::MediaType type, Vst::BusDirection direction ) const
    {
        if (!m_component)
            return -1;

        int nchannels = 0;

        const int32 nbuses = m_component->getBusCount(type, direction);
        for (int32 i = 0; i < nbuses; ++i)
        {
            Vst::BusInfo busInfo;
            if (m_component->getBusInfo(type, direction, i, busInfo) == kResultOk)
            {
                if ((busInfo.busType == Vst::kMain) ||
                        (busInfo.flags & Vst::BusInfo::kDefaultActive))
                    nchannels += busInfo.channelCount;
            }
        }

        return nchannels;
    }

private:

    std::string   m_sSubCategories;

    // Instance variables.
    void *m_module;

    PClassInfo m_classInfo;

    PFactoryInfo m_factoryInfo;

    IPtr<Vst::IComponent> m_component;
    IPtr<Vst::IEditController> m_controller;
};

//----------------------------------------------------------------------
// class qtractor_vst3_scan -- VST3 plugin interface
//

// Constructor.
qtractor_vst3_scan::qtractor_vst3_scan (void) : m_pImpl(new Impl())
{
    clear();
}

// destructor.
qtractor_vst3_scan::~qtractor_vst3_scan (void)
{
    close_descriptor();
    close();

    delete m_pImpl;
}

// File loader.
bool qtractor_vst3_scan::open ( const std::string& sFilename )
{
    close();

//    DMESSAGE("qtractor_vst3_scan[%p]::open(\"%s\")", this, sFilename.c_str());

    return m_pImpl->open(sFilename);
}

bool qtractor_vst3_scan::open_descriptor ( unsigned long iIndex )
{
    close_descriptor();

//  DMESSAGE("qtractor_vst3_scan[%p]::open_descriptor( %lu)", this, iIndex);

    if (!m_pImpl->open_descriptor(iIndex))
        return false;

    const PClassInfo& classInfo = m_pImpl->classInfo();
    m_sName = classInfo.name;

    const PFactoryInfo& factoryInfo = m_pImpl->factoryInfo();
    m_sVendor = factoryInfo.vendor;

    m_sSubCategories = m_pImpl->subCategory();

    //	m_iUniqueID = qHash(QByteArray(classInfo.cid, sizeof(TUID)));
    m_iUniqueID = atoi(classInfo.cid);  // FIXME 

    m_iAudioIns  = m_pImpl->numChannels(Vst::kAudio, Vst::kInput);
    m_iAudioOuts = m_pImpl->numChannels(Vst::kAudio, Vst::kOutput);

#if 0
    m_iMidiIns   = m_pImpl->numChannels(Vst::kEvent, Vst::kInput);
    m_iMidiOuts  = m_pImpl->numChannels(Vst::kEvent, Vst::kOutput);

    Vst::IEditController *controller = m_pImpl->controller();
    if (controller)
    {
        IPtr<IPlugView> editor
            = owned(controller->createView(Vst::ViewType::kEditor));
        m_bEditor = (editor != nullptr);
    }

    m_iControlIns  = 0;
    m_iControlOuts = 0;

    if (controller)
    {
        const int32 nparams = controller->getParameterCount();
        for (int32 i = 0; i < nparams; ++i)
        {
            Vst::ParameterInfo paramInfo;
            if (controller->getParameterInfo(i, paramInfo) == kResultOk)
            {
                if (paramInfo.flags & Vst::ParameterInfo::kIsReadOnly)
                        ++m_iControlOuts;
                else
                if (paramInfo.flags & Vst::ParameterInfo::kCanAutomate)
                        ++m_iControlIns;
            }
        }
    }
#endif
    return true;
}

// Convert filesystem paths for vst3 to binary file name of vst3
std::string qtractor_vst3_scan::get_vst3_object_file(std::string filename)
{
    std::filesystem::path binaryfilename = filename;

    std::filesystem::path p = filename;

    binaryfilename += CARLA_OS_SEP_STR;

    binaryfilename += "Contents" CARLA_OS_SEP_STR V3_CONTENT_DIR CARLA_OS_SEP_STR;
    binaryfilename += p.stem();

    binaryfilename += ".so";

    if ( ! std::filesystem::exists(binaryfilename) )
    {
        WARNING("Failed to find a suitable VST3 bundle binary %s", binaryfilename.c_str());
        return "";
    }

    return binaryfilename.c_str();
}


// File unloader.
void qtractor_vst3_scan::close_descriptor (void)
{
    m_pImpl->close_descriptor();

    clear();
}


void qtractor_vst3_scan::close (void)
{
  //  DMESSAGE("qtractor_vst3_scan[%p]::close()", this);

    m_pImpl->close();
}


// Properties.
bool qtractor_vst3_scan::isOpen (void) const
{
    return (m_pImpl->controller() != nullptr);
}


// Cleaner/wiper.
void qtractor_vst3_scan::clear (void)
{
    m_sName.clear();
    m_sVendor.clear();
    m_sSubCategories.clear();

    m_iUniqueID    = 0;
    m_iAudioIns    = 0;
    m_iAudioOuts   = 0;
#if 0
    m_iControlIns  = 0;
    m_iControlOuts = 0;
    m_iMidiIns     = 0;
    m_iMidiOuts    = 0;
    m_bEditor      = false;
#endif
}


//-------------------------------------------------------------------------
// qtractor_vst3_scan_file - The main scan procedure.
//

void qtractor_vst3_scan_file ( const std::string& sFilename, std::list<Plugin_Module::Plugin_Info> & vst3pr )
{
    qtractor_vst3_scan plugin;

    std::string sVst3Object = plugin.get_vst3_object_file(sFilename);

    if (!plugin.open(sVst3Object))
    {
        DMESSAGE("Could not open %s", sVst3Object.c_str());
        return;
    }

    unsigned long i = 0;
    while (plugin.open_descriptor(i))
    {
        Plugin_Module::Plugin_Info pi("VST3");

        pi.name = plugin.name();
        pi.author = plugin.vendor();

        if (std::strstr(plugin.subCategory().c_str(), "Instrument") != nullptr)
        {
            pi.category =  "Instrument Plugin";
        }
        else
        {
             pi.category = getCategoryFromName(pi.name);
        }

        pi.audio_inputs = plugin.audioIns();
        pi.audio_outputs = plugin.audioOuts();

        pi.s_unique_id = sVst3Object;
        pi.id = plugin.uniqueID();          // FIXME garbage

        vst3pr.push_back(pi);

        DMESSAGE("name = %s: category = %s: path = %s: ID = %ul",
                pi.name.c_str(), pi.category.c_str(), pi.s_unique_id.c_str(), pi.id);

        plugin.close_descriptor();
        ++i;
    }

    plugin.close();

    if (i == 0)
        WARNING("%s plugin file error");
}

#else   // travesty

using namespace CARLA_BACKEND_NAMESPACE;

struct carla_v3_host_application : v3_host_application_cpp {
    carla_v3_host_application()
    {
        query_interface = v3_query_interface_static<v3_host_application_iid>;
        ref = v3_ref_static;
        unref = v3_unref_static;
        app.get_name = carla_get_name;
        app.create_instance = carla_create_instance;
    }

private:
    static v3_result V3_API carla_get_name(void*, v3_str_128 name)
    {
        static const char hostname[] = "Carla-Discovery\0";

        for (size_t i=0; i<sizeof(hostname); ++i)
            name[i] = hostname[i];

        return V3_OK;
    }

    static v3_result V3_API carla_create_instance(void*, v3_tuid, v3_tuid, void**) { return V3_NOT_IMPLEMENTED; }

    CARLA_DECLARE_NON_COPYABLE(carla_v3_host_application)
    CARLA_PREVENT_HEAP_ALLOCATION
};

struct carla_v3_param_value_queue : v3_param_value_queue_cpp {
    carla_v3_param_value_queue()
    {
        query_interface = v3_query_interface_static<v3_param_value_queue_iid>;
        ref = v3_ref_static;
        unref = v3_unref_static;
        queue.get_param_id = carla_get_param_id;
        queue.get_point_count = carla_get_point_count;
        queue.get_point = carla_get_point;
        queue.add_point = carla_add_point;
    }

private:
    static v3_param_id V3_API carla_get_param_id(void*) { return 0; }
    static int32_t V3_API carla_get_point_count(void*) { return 0; }
    static v3_result V3_API carla_get_point(void*, int32_t, int32_t*, double*) { return V3_NOT_IMPLEMENTED; }
    static v3_result V3_API carla_add_point(void*, int32_t, double, int32_t*) { return V3_NOT_IMPLEMENTED; }

    CARLA_DECLARE_NON_COPYABLE(carla_v3_param_value_queue)
    CARLA_PREVENT_HEAP_ALLOCATION
};

struct carla_v3_param_changes : v3_param_changes_cpp {
    carla_v3_param_changes()
    {
        query_interface = v3_query_interface_static<v3_param_changes_iid>;
        ref = v3_ref_static;
        unref = v3_unref_static;
        changes.get_param_count = carla_get_param_count;
        changes.get_param_data = carla_get_param_data;
        changes.add_param_data = carla_add_param_data;
    }

private:
    static int32_t V3_API carla_get_param_count(void*) { return 0; }
    static v3_param_value_queue** V3_API carla_get_param_data(void*, int32_t) { return nullptr; }
    static v3_param_value_queue** V3_API carla_add_param_data(void*, const v3_param_id*, int32_t*) { return nullptr; }

    CARLA_DECLARE_NON_COPYABLE(carla_v3_param_changes)
    CARLA_PREVENT_HEAP_ALLOCATION
};

struct carla_v3_event_list : v3_event_list_cpp {
    carla_v3_event_list()
    {
        query_interface = v3_query_interface_static<v3_event_list_iid>;
        ref = v3_ref_static;
        unref = v3_unref_static;
        list.get_event_count = carla_get_event_count;
        list.get_event = carla_get_event;
        list.add_event = carla_add_event;
    }

private:
    static uint32_t V3_API carla_get_event_count(void*) { return 0; }
    static v3_result V3_API carla_get_event(void*, int32_t, v3_event*) { return V3_NOT_IMPLEMENTED; }
    static v3_result V3_API carla_add_event(void*, v3_event*) { return V3_NOT_IMPLEMENTED; }

    CARLA_DECLARE_NON_COPYABLE(carla_v3_event_list)
    CARLA_PREVENT_HEAP_ALLOCATION
};

static bool v3_exit_false(const V3_EXITFN v3_exit)
{
    v3_exit();
    return false;
}

/*
 * Open 'filename' library (must not be null).
 * May return null, in which case "lib_error" has the error.
 */
static inline
lib_t lib_open(const char* const filename, const bool global = false) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(filename != nullptr && filename[0] != '\0', nullptr);

    try {
#ifdef CARLA_OS_WIN
        return ::LoadLibraryA(filename);
        // unused
        (void)global;
#else
        return ::dlopen(filename, RTLD_NOW|(global ? RTLD_GLOBAL : RTLD_LOCAL));
#endif
    } CARLA_SAFE_EXCEPTION_RETURN("lib_open", nullptr);
}

/*
 * Close a previously opened library (must not be null).
 * If false is returned, "lib_error" has the error.
 */
bool lib_close(const lib_t lib)
{
    CARLA_SAFE_ASSERT_RETURN(lib != nullptr, false);

    try {
#ifdef CARLA_OS_WIN
        return ::FreeLibrary(lib);
#else
        return (::dlclose(lib) == 0);
#endif
    } CARLA_SAFE_EXCEPTION_RETURN("lib_close", false);
}


/*
 * Get a library symbol (must not be null) as a function.
 * Returns null if the symbol is not found.
 */
template<typename Func>
static inline
Func lib_symbol(const lib_t lib, const char* const symbol) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(lib != nullptr, nullptr);
    CARLA_SAFE_ASSERT_RETURN(symbol != nullptr && symbol[0] != '\0', nullptr);

    try {
#ifdef CARLA_OS_WIN
# if defined(__GNUC__) && (__GNUC__ >= 9)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-function-type"
# endif
        return reinterpret_cast<Func>(::GetProcAddress(lib, symbol));
# if defined(__GNUC__) && (__GNUC__ >= 9)
#  pragma GCC diagnostic pop
# endif
#else
        return reinterpret_cast<Func>(::dlsym(lib, symbol));
#endif
    } CARLA_SAFE_EXCEPTION_RETURN("lib_symbol", nullptr);
}

static inline
const char* PluginCategory2Str(const PluginCategory category) noexcept
{
    switch (category)
    {
    case PLUGIN_CATEGORY_NONE:
        return "PLUGIN_CATEGORY_NONE";
    case PLUGIN_CATEGORY_SYNTH:
        return "PLUGIN_CATEGORY_SYNTH";
    case PLUGIN_CATEGORY_DELAY:
        return "PLUGIN_CATEGORY_DELAY";
    case PLUGIN_CATEGORY_EQ:
        return "PLUGIN_CATEGORY_EQ";
    case PLUGIN_CATEGORY_FILTER:
        return "PLUGIN_CATEGORY_FILTER";
    case PLUGIN_CATEGORY_DISTORTION:
        return "PLUGIN_CATEGORY_DISTORTION";
    case PLUGIN_CATEGORY_DYNAMICS:
        return "PLUGIN_CATEGORY_DYNAMICS";
    case PLUGIN_CATEGORY_MODULATOR:
        return "PLUGIN_CATEGORY_MODULATOR";
    case PLUGIN_CATEGORY_UTILITY:
        return "PLUGIN_CATEGORY_UTILITY";
    case PLUGIN_CATEGORY_OTHER:
        return "PLUGIN_CATEGORY_OTHER";
    }

    WARNING("PluginCategory2Str(%i) - invalid category", category);
    return "";
}

static inline
const char* getPluginCategoryAsString(const PluginCategory category) noexcept
{
  //  DMESSAGE("CarlaBackend::getPluginCategoryAsString(%i:%s)", category, PluginCategory2Str(category));

    switch (category)
    {
    case PLUGIN_CATEGORY_NONE:
        return "Unclassified";
    case PLUGIN_CATEGORY_SYNTH:
        return "Instrument Plugin";
    case PLUGIN_CATEGORY_DELAY:
        return "Time/Delays";
    case PLUGIN_CATEGORY_EQ:
        return "Frequency/EQs";
    case PLUGIN_CATEGORY_FILTER:
        return "Frequency/Filters";
    case PLUGIN_CATEGORY_DISTORTION:
        return "Amplitude/Distortions";
    case PLUGIN_CATEGORY_DYNAMICS:
        return "Amplitude/Dynamics";
    case PLUGIN_CATEGORY_MODULATOR:
        return "Amplitude/Modulators";
    case PLUGIN_CATEGORY_UTILITY:
        return "Utilities";
    case PLUGIN_CATEGORY_OTHER:
        return "Unclassified";
    }

    WARNING("getPluginCategoryAsString(%i) - invalid category", category);
    return "Unclassified";
}

static inline
PluginCategory getPluginCategoryFromName(const char* const name) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(name != nullptr && name[0] != '\0', PLUGIN_CATEGORY_NONE);
    DMESSAGE("getPluginCategoryFromName(\"%s\")", name);

    std::string sname(name);

    if (sname.empty())
        return PLUGIN_CATEGORY_NONE;

    std::transform(sname.begin(), sname.end(), sname.begin(), [](unsigned char c){ return std::tolower(c); });
    
    // generic tags first
    if (sname.find("delay") != std::string::npos)
        return PLUGIN_CATEGORY_DELAY;
    if (sname.find("reverb") != std::string::npos)
        return PLUGIN_CATEGORY_DELAY;

    // filter
    if (sname.find("filter") != std::string::npos)
        return PLUGIN_CATEGORY_FILTER;

    // distortion
    if (sname.find("distortion") != std::string::npos)
        return PLUGIN_CATEGORY_DISTORTION;

    // dynamics
    if (sname.find("dynamics") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("amplifier") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("compressor") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("enhancer") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("exciter") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("gate") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;
    if (sname.find("limiter") != std::string::npos)
        return PLUGIN_CATEGORY_DYNAMICS;

    // modulator
    if (sname.find("modulator") != std::string::npos)
        return PLUGIN_CATEGORY_MODULATOR;
    if (sname.find("chorus") != std::string::npos)
        return PLUGIN_CATEGORY_MODULATOR;
    if (sname.find("flanger") != std::string::npos)
        return PLUGIN_CATEGORY_MODULATOR;
    if (sname.find("phaser") != std::string::npos)
        return PLUGIN_CATEGORY_MODULATOR;
    if (sname.find("saturator") != std::string::npos)
        return PLUGIN_CATEGORY_MODULATOR;

    // utility
    if (sname.find("utility") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;
    if (sname.find("analyzer") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;
    if (sname.find("converter") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;
    if (sname.find("deesser") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;
    if (sname.find("mixer") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;

    // common tags
    if (sname.find("verb") != std::string::npos)
        return PLUGIN_CATEGORY_DELAY;

    if (sname.find("eq") != std::string::npos)
        return PLUGIN_CATEGORY_EQ;

    if (sname.find("tool") != std::string::npos)
        return PLUGIN_CATEGORY_UTILITY;

    // synth
    if (sname.find("synth") != std::string::npos)
        return PLUGIN_CATEGORY_SYNTH;

    // other
    if (sname.find("misc") != std::string::npos)
        return PLUGIN_CATEGORY_OTHER;
    if (sname.find("other") != std::string::npos)
        return PLUGIN_CATEGORY_OTHER;

    return PLUGIN_CATEGORY_NONE;
}


bool do_vst3_check(lib_t& libHandle,
        const char* const filename,
        const bool doInit,
        std::list<Plugin_Module::Plugin_Info> & pr)
{
    V3_ENTRYFN v3_entry = nullptr;
    V3_EXITFN v3_exit = nullptr;
    V3_GETFN v3_get = nullptr;

    // if passed filename is not a plugin binary directly, inspect bundle and find one
    if (libHandle == nullptr)
    {

       // water::String binaryfilename = filename;
        std::filesystem::path binaryfilename = filename;
        
        std::filesystem::path p = filename;

       // if ( !binaryfilename.preferred_separator )
            binaryfilename += CARLA_OS_SEP_STR;
     //   if (!binaryfilename.endsWithChar(CARLA_OS_SEP))
     //       binaryfilename += CARLA_OS_SEP_STR;

        binaryfilename += "Contents" CARLA_OS_SEP_STR V3_CONTENT_DIR CARLA_OS_SEP_STR;
        binaryfilename += p.stem();
     //   binaryfilename += water::File(filename).getFileNameWithoutExtension();

        binaryfilename += ".so";

        if ( ! std::filesystem::exists(binaryfilename) )
       // if (! water::File(binaryfilename).existsAsFile())
        {
            WARNING("Failed to find a suitable VST3 bundle binary %s", binaryfilename.c_str());
            return false;
        }

        libHandle = lib_open(binaryfilename.u8string().c_str());
    //    libHandle = lib_open(binaryfilename.toRawUTF8());

        if (libHandle == nullptr)
        {
            WARNING("Lib error %s", filename);
           // print_lib_error(filename);
            return false;
        }
    }

    v3_entry = lib_symbol<V3_ENTRYFN>(libHandle, V3_ENTRYFNNAME);
    v3_exit = lib_symbol<V3_EXITFN>(libHandle, V3_EXITFNNAME);
    v3_get = lib_symbol<V3_GETFN>(libHandle, V3_GETFNNAME);

    // ensure entry and exit points are available
    if (v3_entry == nullptr || v3_exit == nullptr || v3_get == nullptr)
    {
        WARNING("ERROR, Not a VST3 plugin");
        //DISCOVERY_OUT("error", "Not a VST3 plugin");
        return false;
    }

    v3_entry(libHandle);

    carla_v3_host_application hostApplication;
    carla_v3_host_application* hostApplicationPtr = &hostApplication;
    v3_funknown** const hostContext = (v3_funknown**)&hostApplicationPtr;

    // fetch initial factory
    v3_plugin_factory** factory1 = v3_get();
    CARLA_SAFE_ASSERT_RETURN(factory1 != nullptr, v3_exit_false(v3_exit));

    // get factory info
    v3_factory_info factoryInfo = {};
    CARLA_SAFE_ASSERT_RETURN(v3_cpp_obj(factory1)->get_factory_info(factory1, &factoryInfo) == V3_OK,
                             v3_exit_false(v3_exit));

    // get num classes
    const int32_t numClasses = v3_cpp_obj(factory1)->num_classes(factory1);
    CARLA_SAFE_ASSERT_RETURN(numClasses > 0, v3_exit_false(v3_exit));

    // query 2nd factory
    v3_plugin_factory_2** factory2 = nullptr;
    if (v3_cpp_obj_query_interface(factory1, v3_plugin_factory_2_iid, &factory2) == V3_OK)
    {
        CARLA_SAFE_ASSERT_RETURN(factory2 != nullptr, v3_exit_false(v3_exit));
    }
    else
    {
        CARLA_SAFE_ASSERT(factory2 == nullptr);
        factory2 = nullptr;
    }

    // query 3rd factory
    v3_plugin_factory_3** factory3 = nullptr;
    if (factory2 != nullptr && v3_cpp_obj_query_interface(factory2, v3_plugin_factory_3_iid, &factory3) == V3_OK)
    {
        CARLA_SAFE_ASSERT_RETURN(factory3 != nullptr, v3_exit_false(v3_exit));
    }
    else
    {
        CARLA_SAFE_ASSERT(factory3 == nullptr);
        factory3 = nullptr;
    }

    // set host context (application) if 3rd factory provided
    if (factory3 != nullptr)
        v3_cpp_obj(factory3)->set_host_context(factory3, hostContext);

    // go through all relevant classes
    for (int32_t i=0; i<numClasses; ++i)
    {
        // v3_class_info_2 is ABI compatible with v3_class_info
        union {
            v3_class_info v1;
            v3_class_info_2 v2;
        } classInfo = {};

        if (factory2 != nullptr)
            v3_cpp_obj(factory2)->get_class_info_2(factory2, i, &classInfo.v2);
        else
            v3_cpp_obj(factory1)->get_class_info(factory1, i, &classInfo.v1);

        // safety check
        CARLA_SAFE_ASSERT_CONTINUE(classInfo.v1.cardinality == 0x7FFFFFFF);

        // only check for audio plugins
        if (std::strcmp(classInfo.v1.category, "Audio Module Class") != 0)
            continue;

        // create instance
        void* instance = nullptr;
        CARLA_SAFE_ASSERT_CONTINUE(v3_cpp_obj(factory1)->create_instance(factory1, classInfo.v1.class_id,
                                                                         v3_component_iid, &instance) == V3_OK);
        CARLA_SAFE_ASSERT_CONTINUE(instance != nullptr);

        // initialize instance
        v3_component** const component = static_cast<v3_component**>(instance);

        CARLA_SAFE_ASSERT_CONTINUE(v3_cpp_obj_initialize(component, hostContext) == V3_OK);

        // create edit controller
        v3_edit_controller** controller = nullptr;
        bool shouldTerminateController;
        if (v3_cpp_obj_query_interface(component, v3_edit_controller_iid, &controller) != V3_OK)
            controller = nullptr;

        if (controller != nullptr)
        {
            // got edit controller from casting component, assume they belong to the same object
            shouldTerminateController = false;
        }
        else
        {
            // try to create edit controller from factory
            v3_tuid uid = {};
            if (v3_cpp_obj(component)->get_controller_class_id(component, uid) == V3_OK)
            {
                instance = nullptr;
                if (v3_cpp_obj(factory1)->create_instance(factory1, uid, v3_edit_controller_iid, &instance) == V3_OK)
                    controller = static_cast<v3_edit_controller**>(instance);
            }

            if (controller == nullptr)
            {
                WARNING("Plugin %s does not have an edit controller", classInfo.v1.name);
               // DISCOVERY_OUT("warning", "Plugin '" << classInfo.v1.name << "' does not have an edit controller");
                v3_cpp_obj_terminate(component);
                v3_cpp_obj_unref(component);
                continue;
            }

            // component is separate from controller, needs its dedicated initialize and terminate
            shouldTerminateController = true;
            v3_cpp_obj_initialize(controller, hostContext);
        }

        // connect component to controller
        v3_connection_point** connComponent = nullptr;
        if (v3_cpp_obj_query_interface(component, v3_connection_point_iid, &connComponent) != V3_OK)
            connComponent = nullptr;

        v3_connection_point** connController = nullptr;
        if (v3_cpp_obj_query_interface(controller, v3_connection_point_iid, &connController) != V3_OK)
            connController = nullptr;

        if (connComponent != nullptr && connController != nullptr)
        {
            v3_cpp_obj(connComponent)->connect(connComponent, connController);
            v3_cpp_obj(connController)->connect(connController, connComponent);
        }

        // fill in all the details
        uint hints = 0x0;
        int audioIns = 0;
        int audioOuts = 0;
        int cvIns = 0;
        int cvOuts = 0;
        int parameterIns = 0;
        int parameterOuts = 0;

        const int32_t numAudioInputBuses = v3_cpp_obj(component)->get_bus_count(component, V3_AUDIO, V3_INPUT);
        const int32_t numEventInputBuses = v3_cpp_obj(component)->get_bus_count(component, V3_EVENT, V3_INPUT);
        const int32_t numAudioOutputBuses = v3_cpp_obj(component)->get_bus_count(component, V3_AUDIO, V3_OUTPUT);
        const int32_t numEventOutputBuses = v3_cpp_obj(component)->get_bus_count(component, V3_EVENT, V3_OUTPUT);
        const int32_t numParameters = v3_cpp_obj(controller)->get_parameter_count(controller);

        CARLA_SAFE_ASSERT(numAudioInputBuses >= 0);
        CARLA_SAFE_ASSERT(numEventInputBuses >= 0);
        CARLA_SAFE_ASSERT(numAudioOutputBuses >= 0);
        CARLA_SAFE_ASSERT(numEventOutputBuses >= 0);
        CARLA_SAFE_ASSERT(numParameters >= 0);

        for (int32_t b=0; b<numAudioInputBuses; ++b)
        {
            v3_bus_info busInfo = {};
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->get_bus_info(component,
                                                                        V3_AUDIO, V3_INPUT, b, &busInfo) == V3_OK);

            if (busInfo.flags & V3_IS_CONTROL_VOLTAGE)
                cvIns += busInfo.channel_count;
            else
                audioIns += busInfo.channel_count;
        }

        for (int32_t b=0; b<numAudioOutputBuses; ++b)
        {
            v3_bus_info busInfo = {};
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->get_bus_info(component,
                                                                        V3_AUDIO, V3_OUTPUT, b, &busInfo) == V3_OK);

            if (busInfo.flags & V3_IS_CONTROL_VOLTAGE)
            {
                //DMESSAGE("Have V3_IS_CONTROL_VOLTAGE");
                cvOuts += busInfo.channel_count;
            }
            else
                audioOuts += busInfo.channel_count;
        }

        for (int32_t p=0; p<numParameters; ++p)
        {
            v3_param_info paramInfo = {};
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(controller)->get_parameter_info(controller, p, &paramInfo) == V3_OK);

            if (paramInfo.flags & (V3_PARAM_IS_BYPASS|V3_PARAM_IS_HIDDEN|V3_PARAM_PROGRAM_CHANGE))
                continue;

            if (paramInfo.flags & V3_PARAM_READ_ONLY)
                ++parameterOuts;
            else
                ++parameterIns;
        }

        CARLA_SAFE_ASSERT_CONTINUE(audioIns <= MAX_DISCOVERY_AUDIO_IO);
        CARLA_SAFE_ASSERT_CONTINUE(audioOuts <= MAX_DISCOVERY_AUDIO_IO);
        CARLA_SAFE_ASSERT_CONTINUE(cvIns <= MAX_DISCOVERY_CV_IO);
        CARLA_SAFE_ASSERT_CONTINUE(cvOuts <= MAX_DISCOVERY_CV_IO);

       #ifdef V3_VIEW_PLATFORM_TYPE_NATIVE
        if (v3_plugin_view** const view = v3_cpp_obj(controller)->create_view(controller, "editor"))
        {
            if (v3_cpp_obj(view)->is_platform_type_supported(view, V3_VIEW_PLATFORM_TYPE_NATIVE) == V3_TRUE)
            {
                hints |= PLUGIN_HAS_CUSTOM_UI;
               #ifndef BUILD_BRIDGE
                hints |= PLUGIN_HAS_CUSTOM_EMBED_UI;
               #endif
            }

            v3_cpp_obj_unref(view);
        }
       #endif

        if (factory2 != nullptr && std::strstr(classInfo.v2.sub_categories, "Instrument") != nullptr)
            hints |= PLUGIN_IS_SYNTH;

        if (doInit)
        {
            v3_audio_processor** processor = nullptr;
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj_query_interface(component, v3_audio_processor_iid, &processor) == V3_OK);
            CARLA_SAFE_ASSERT_BREAK(processor != nullptr);

            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(processor)->can_process_sample_size(processor, V3_SAMPLE_32) == V3_OK);

            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->set_active(component, true) == V3_OK);

            v3_process_setup setup = { V3_REALTIME, V3_SAMPLE_32, kBufferSize, kSampleRate };
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK);

            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->set_active(component, false) == V3_OK);

            v3_audio_bus_buffers* const inputsBuffers = numAudioInputBuses > 0
                                                      ? new v3_audio_bus_buffers[numAudioInputBuses]
                                                      : nullptr;

            v3_audio_bus_buffers* const outputsBuffers = numAudioOutputBuses > 0
                                                       ? new v3_audio_bus_buffers[numAudioOutputBuses]
                                                       : nullptr;

            for (int32_t b=0; b<numAudioInputBuses; ++b)
            {
                v3_bus_info busInfo = {};
                carla_zeroStruct(inputsBuffers[b]);
                CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->get_bus_info(component,
                                                                            V3_AUDIO, V3_INPUT, b, &busInfo) == V3_OK);

                if ((busInfo.flags & V3_DEFAULT_ACTIVE) == 0x0) {
                    CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->activate_bus(component,
                                                                                V3_AUDIO, V3_INPUT, b, true) == V3_OK);
                }

                inputsBuffers[b].num_channels = busInfo.channel_count;
            }

            for (int32_t b=0; b<numAudioOutputBuses; ++b)
            {
                v3_bus_info busInfo = {};
                carla_zeroStruct(outputsBuffers[b]);
                CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->get_bus_info(component,
                                                                            V3_AUDIO, V3_OUTPUT, b, &busInfo) == V3_OK);

                if ((busInfo.flags & V3_DEFAULT_ACTIVE) == 0x0) {
                    CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->activate_bus(component,
                                                                                V3_AUDIO, V3_OUTPUT, b, true) == V3_OK);
                }

                outputsBuffers[b].num_channels = busInfo.channel_count;
            }

            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->set_active(component, true) == V3_OK);
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(processor)->set_processing(processor, true) == V3_OK);

            float* bufferAudioIn[MAX_DISCOVERY_AUDIO_IO + MAX_DISCOVERY_CV_IO];
            float* bufferAudioOut[MAX_DISCOVERY_AUDIO_IO + MAX_DISCOVERY_CV_IO];
          //  float* bufferAudioIn[MAX_DISCOVERY_AUDIO_IO];
         //   float* bufferAudioOut[MAX_DISCOVERY_AUDIO_IO];

            for (int j=0; j < audioIns + cvIns; ++j)
            {
                bufferAudioIn[j] = new float[kBufferSize];
                carla_zeroFloats(bufferAudioIn[j], kBufferSize);
            }

            for (int j=0; j < audioOuts + cvOuts; ++j)
            {
                bufferAudioOut[j] = new float[kBufferSize];
                carla_zeroFloats(bufferAudioOut[j], kBufferSize);
            }

            for (int32_t b = 0, j = 0; b < numAudioInputBuses; ++b)
            {
                inputsBuffers[b].channel_buffers_32 = bufferAudioIn + j;
                j += inputsBuffers[b].num_channels;
            }

            for (int32_t b = 0, j = 0; b < numAudioOutputBuses; ++b)
            {
                outputsBuffers[b].channel_buffers_32 = bufferAudioOut + j;
                j += outputsBuffers[b].num_channels;
            }

            carla_v3_event_list eventList;
            carla_v3_event_list* eventListPtr = &eventList;

            carla_v3_param_changes paramChanges;
            carla_v3_param_changes* paramChangesPtr = &paramChanges;

            v3_process_context processContext = {};
            processContext.sample_rate = kSampleRate;

            v3_process_data processData = {
                V3_REALTIME,
                V3_SAMPLE_32,
                kBufferSize,
                numAudioInputBuses,
                numAudioOutputBuses,
                inputsBuffers,
                outputsBuffers,
                (v3_param_changes**)&paramChangesPtr,
                (v3_param_changes**)&paramChangesPtr,
                (v3_event_list**)&eventListPtr,
                (v3_event_list**)&eventListPtr,
                &processContext
            };
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(processor)->process(processor, &processData) == V3_OK);

            delete[] inputsBuffers;
            delete[] outputsBuffers;

            for (int j=0; j < audioIns + cvIns; ++j)
                delete[] bufferAudioIn[j];
            for (int j=0; j < audioOuts + cvOuts; ++j)
                delete[] bufferAudioOut[j];

            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(processor)->set_processing(processor, false) == V3_OK);
            CARLA_SAFE_ASSERT_BREAK(v3_cpp_obj(component)->set_active(component, false) == V3_OK);

            v3_cpp_obj_unref(processor);
        }

        // disconnect and unref connection points
        if (connComponent != nullptr && connController != nullptr)
        {
            v3_cpp_obj(connComponent)->disconnect(connComponent, connController);
            v3_cpp_obj(connController)->disconnect(connController, connComponent);
        }

        if (connComponent != nullptr)
            v3_cpp_obj_unref(connComponent);

        if (connController != nullptr)
            v3_cpp_obj_unref(connController);

        if (shouldTerminateController)
            v3_cpp_obj_terminate(controller);

        v3_cpp_obj_unref(controller);

        v3_cpp_obj_terminate(component);
        v3_cpp_obj_unref(component);
        
        Plugin_Module::Plugin_Info pi("VST3");
        
        pi.name = classInfo.v1.name;
        pi.author = (factory2 != nullptr ? classInfo.v2.vendor : factoryInfo.vendor);
        pi.category = getPluginCategoryAsString(factory2 != nullptr ? getPluginCategoryFromV3SubCategories(classInfo.v2.sub_categories)
                                                                                : getPluginCategoryFromName(classInfo.v1.name));
        pi.audio_inputs = audioIns;
        pi.audio_outputs = audioOuts;
        pi.s_unique_id = tuid2str(classInfo.v1.class_id);
        pi.id = 0;
        
        pr.push_back(pi);
        
       // DMESSAGE("pi.name =%s: in = %d: out = %d", pi.name.c_str(), pi.audio_inputs, pi.audio_outputs);
#if 0
        DISCOVERY_OUT("init", "------------");
        DISCOVERY_OUT("build", BINARY_NATIVE);
        DISCOVERY_OUT("hints", hints);
        DISCOVERY_OUT("category", getPluginCategoryAsString(factory2 != nullptr ? getPluginCategoryFromV3SubCategories(classInfo.v2.sub_categories)
                                                                                : getPluginCategoryFromName(classInfo.v1.name)));
        DISCOVERY_OUT("name", classInfo.v1.name);
        DISCOVERY_OUT("label", tuid2str(classInfo.v1.class_id));
        DISCOVERY_OUT("maker", (factory2 != nullptr ? classInfo.v2.vendor : factoryInfo.vendor));
        DISCOVERY_OUT("audio.ins", audioIns);
        DISCOVERY_OUT("audio.outs", audioOuts);
        DISCOVERY_OUT("cv.ins", cvIns);
        DISCOVERY_OUT("cv.outs", cvOuts);
        DISCOVERY_OUT("midi.ins", numEventInputBuses);
        DISCOVERY_OUT("midi.outs", numEventOutputBuses);
        DISCOVERY_OUT("parameters.ins", parameterIns);
        DISCOVERY_OUT("parameters.outs", parameterOuts);
        DISCOVERY_OUT("end", "------------");
#endif
    }

    // unref interfaces
    if (factory3 != nullptr)
        v3_cpp_obj_unref(factory3);

    if (factory2 != nullptr)
        v3_cpp_obj_unref(factory2);

    v3_cpp_obj_unref(factory1);

    v3_exit();
    return false;
}

#endif	// CONFIG_VST3 - travesty

}   // namespace vst3_discovery

#endif  // VST3_SUPPORT