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
 * File:   VST3_Plugin.C
 * Author: sspresto
 * 
 * Created on December 20, 2023, 9:24 AM
 */

#ifdef VST3_SUPPORT

#include <filesystem>
#include <dlfcn.h>      // dlopen, dlerror, dlsym

#include "VST3_Plugin.H"

using namespace Steinberg;

VST3_Plugin::VST3_Plugin() :
    Plugin_Module(),
    m_module(nullptr),
    m_component(nullptr),
    m_controller(nullptr),
    m_unitInfos(nullptr),
    _plugin_filename()
{
    _plug_type = VST3;

    log_create();
}

VST3_Plugin::~VST3_Plugin()
{
    log_destroy();
}

bool
VST3_Plugin::load_plugin ( Module::Picked picked )
{
    _plugin_filename = picked.s_unique_id;
    
    if ( ! std::filesystem::exists(_plugin_filename) )
    {
        // FIXME check different location
        WARNING("Failed to find a suitable VST3 bundle binary %s", _plugin_filename.c_str());
        return false;
    }

    if ( !open_descriptor(0) )
    {
        DMESSAGE("Could not open descriptor %s", _plugin_filename.c_str());
        return false;
    }

    return false;
}

bool
VST3_Plugin::configure_inputs ( int )
{
    return false;
}

void
VST3_Plugin::handle_port_connection_change ( void )
{
    
}

void
VST3_Plugin::handle_chain_name_changed ( void )
{
    
}

void
VST3_Plugin::handle_sample_rate_change ( nframes_t sample_rate )
{
    
}

void
VST3_Plugin::resize_buffers ( nframes_t buffer_size )
{
    
}

void
VST3_Plugin::bypass ( bool v )
{
    
}

void
VST3_Plugin::freeze_ports ( void )
{
    
}

void
VST3_Plugin::thaw_ports ( void )
{
    
}

void
VST3_Plugin::configure_midi_inputs ()
{
    
}

void
VST3_Plugin::configure_midi_outputs ()
{
    
}

nframes_t
VST3_Plugin::get_module_latency ( void ) const
{
    return 0;
}

void
VST3_Plugin::process ( nframes_t )
{
    
}

void
VST3_Plugin::handlePluginUIClosed()
{

}

void
VST3_Plugin::handlePluginUIResized(const uint width, const uint height)
{
    
}

// File loader.
bool
VST3_Plugin::open ( const std::string& sFilename )
{
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

bool
VST3_Plugin::open_descriptor(unsigned long iIndex)
{
    close_descriptor();
    
    if (!open(_plugin_filename))
    {
        DMESSAGE("Could not open %s", _plugin_filename.c_str());
        return false;
    }
    #if 0
    typedef bool (PLUGIN_API *VST3_ModuleEntry)(void *);
    const VST3_ModuleEntry module_entry
            = reinterpret_cast<VST3_ModuleEntry> (m_pFile->resolve("ModuleEntry"));
    if (module_entry)
            module_entry(m_pFile->module());
    #endif
    typedef IPluginFactory *(PLUGIN_API *VST3_GetPluginFactory)();
    const VST3_GetPluginFactory get_plugin_factory
            = reinterpret_cast<VST3_GetPluginFactory> (::dlsym(m_module, "GetPluginFactory"));
    //	= reinterpret_cast<VST3_GetPluginFactory> (m_pFile->resolve("GetPluginFactory"));
    if (!get_plugin_factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to resolve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory *factory = get_plugin_factory();
    if (!factory)
    {
        DMESSAGE("[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    PFactoryInfo factoryInfo;
    if (factory->getFactoryInfo(&factoryInfo) != kResultOk)
    {
        DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
                " *** Failed to retrieve plug-in factory information.", this,
                _plugin_filename.c_str(), iIndex);

        return false;
    }

    IPluginFactory2 *factory2 = FUnknownPtr<IPluginFactory2> (factory);
    IPluginFactory3 *factory3 = FUnknownPtr<IPluginFactory3> (factory);
    #if 0
    if (factory3)
    {
        factory3->setHostContext(g_hostContext.get());   // FIXME
    }
    #endif
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
#if 0
            PClassInfoW classInfoW;
            if (factory3 && factory3->getClassInfoUnicode(n, &classInfoW) == kResultOk) {
                    m_sName = fromTChar(classInfoW.name);
                    m_sCategory = QString::fromLocal8Bit(classInfoW.category);
                    m_sSubCategories = QString::fromLocal8Bit(classInfoW.subCategories);
                    m_sVendor = fromTChar(classInfoW.vendor);
                    m_sVersion = fromTChar(classInfoW.version);
                    m_sSdkVersion = fromTChar(classInfoW.sdkVersion);
            } else {
                    PClassInfo2 classInfo2;
                    if (factory2 && factory2->getClassInfo2(n, &classInfo2) == kResultOk) {
                            m_sName = QString::fromLocal8Bit(classInfo2.name);
                            m_sCategory = QString::fromLocal8Bit(classInfo2.category);
                            m_sSubCategories = QString::fromLocal8Bit(classInfo2.subCategories);
                            m_sVendor = QString::fromLocal8Bit(classInfo2.vendor);
                            m_sVersion = QString::fromLocal8Bit(classInfo2.version);
                            m_sSdkVersion = QString::fromLocal8Bit(classInfo2.sdkVersion);
                    } else {
                            m_sName = QString::fromLocal8Bit(classInfo.name);
                            m_sCategory = QString::fromLocal8Bit(classInfo.category);
                            m_sSubCategories.clear();
                            m_sVendor.clear();
                            m_sVersion.clear();
                            m_sSdkVersion.clear();
                    }
            }

            if (m_sVendor.isEmpty())
                    m_sVendor = QString::fromLocal8Bit(factoryInfo.vendor);
            if (m_sEmail.isEmpty())
                    m_sEmail = QString::fromLocal8Bit(factoryInfo.email);
            if (m_sUrl.isEmpty())
                    m_sUrl = QString::fromLocal8Bit(factoryInfo.url);

            m_iUniqueID = qHash(QByteArray(classInfo.cid, sizeof(TUID)));
#endif
            Vst::IComponent *component = nullptr;
            if (factory->createInstance(
                    classInfo.cid, Vst::IComponent::iid,
                    (void **) &component) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to create plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                return false;
            }

            m_component = owned(component);
#if 0   // FIXME
            if (m_component->initialize(g_hostContext.get()) != kResultOk)
            {
                DMESSAGE("[%p]::open(\"%s\", %lu)"
                        " *** Failed to initialize plug-in component.", this,
                        _plugin_filename.c_str(), iIndex);

                close();
                return false;
            }
#endif
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
                        DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
                                " *** Failed to create plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                    }
#if 0   // FIXME
                    if (controller &&
                            controller->initialize(g_hostContext.get()) != kResultOk)
                    {
                        DMESSAGE("[%p]::open(\"%s\", %lu)"
                                " *** Failed to initialize plug-in controller.", this,
                                _plugin_filename.c_str(), iIndex);

                        controller = nullptr;
                    }
#endif
                }
            }

            if (controller) m_controller = owned(controller);

            Vst::IUnitInfo *unitInfos = nullptr;
            if (m_component->queryInterface(
                            Vst::IUnitInfo::iid,
                            (void **) &unitInfos) != kResultOk)
            {
                if (m_controller &&
                        m_controller->queryInterface(
                                Vst::IUnitInfo::iid,
                                (void **) &unitInfos) != kResultOk)
                {
                    DMESSAGE("qtractorVst3PluginType::Impl[%p]::open(\"%s\", %lu)"
                            " *** Failed to create plug-in units information.", this,
                            _plugin_filename.c_str(), iIndex);
                }
            }

            if (unitInfos) m_unitInfos = owned(unitInfos);

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

void
VST3_Plugin::close_descriptor()
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

    m_unitInfos = nullptr;

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
        typedef bool (PLUGIN_API *VST3_ModuleExit)();
        const VST3_ModuleExit module_exit
                = reinterpret_cast<VST3_ModuleExit> (::dlsym(m_module, "ModuleExit"));
              //  = reinterpret_cast<VST3_ModuleExit> (m_pFile->resolve("ModuleExit"));

        if (module_exit)
            module_exit();
    }
}

void
VST3_Plugin::get ( Log_Entry &e ) const
{
    
}

void
VST3_Plugin::set ( Log_Entry &e )
{
    
}

#endif  // VST3_SUPPORT