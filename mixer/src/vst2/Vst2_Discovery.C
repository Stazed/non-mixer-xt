/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.
   Copyright (C) 2024- Stazed

   This file is part of Non-Mixer-XT

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 *****************************************************************************/

/*
 * File:   Vst2_Discovery.C
 * Author: sspresto
 *
 * Created on January 12, 2024, 5:41 PM
 */

#ifdef VST2_SUPPORT

#include <cstring>

#include "Vst2_Discovery.H"

namespace vst2_discovery
{

std::vector<std::filesystem::path>
installedVST2s( )
{
    auto sp = validVST2SearchPaths ( );
    std::vector<std::filesystem::path> vst2s;

    for ( const auto &p : sp )
    {
        DMESSAGE ( "VST(2) PLUG PATHS %s", p.u8string ( ).c_str ( ) );
        try
        {
            for ( auto const &dir_entry : std::filesystem::recursive_directory_iterator ( p ) )
            {
                if ( !std::filesystem::is_directory ( dir_entry.path ( ) ) )
                {
                    if ( dir_entry.path ( ).extension ( ).u8string ( ) == ".so" )
                        vst2s.emplace_back ( dir_entry.path ( ) );
                }
            }
        }
        catch (const std::filesystem::filesystem_error &)
        {
            MESSAGE ( "Vst(2) path directory not found - %s", p.u8string ( ).c_str ( ) );
        }
    }
    return vst2s;
}

/**
 * This returns all the search paths for VST(2) plugin locations.
 * @return
 *      vector of filesystem::path of vst(2) locations.
 */

std::vector<std::filesystem::path>
validVST2SearchPaths( )
{
    std::vector<std::filesystem::path> res;

    /* These are the standard locations for linux */
    res.emplace_back ( "/usr/lib/vst" );
    res.emplace_back ( "/usr/lib/x86_64-linux-gnu/vst" );

    // some distros make /usr/lib64 a symlink to /usr/lib so don't include it
    // or we get duplicates.
    if ( std::filesystem::is_symlink ( "/usr/lib64" ) )
    {
        if ( !strcmp ( std::filesystem::read_symlink ( "/usr/lib64" ).c_str ( ), "/usr/lib" ) )
            res.emplace_back ( "/usr/lib64/vst" );
    }
    else
    {
        res.emplace_back ( "/usr/lib64/vst" );
    }

    res.emplace_back ( "/usr/local/lib/vst" );

    // some distros make /usr/local/lib64 a symlink to /usr/local/lib so don't include it
    // or we get duplicates.
    if ( std::filesystem::is_symlink ( "/us/local/lib64" ) )
    {
        if ( !strcmp ( std::filesystem::read_symlink ( "/usr/local/lib64" ).c_str ( ), "/usr/local/lib" ) )
            res.emplace_back ( "/usr/local/lib64/vst" );
    }
    else
    {
        res.emplace_back ( "/usr/local/lib64/vst" );
    }

    res.emplace_back ( std::filesystem::path ( getenv ( "HOME" ) ) / std::filesystem::path ( ".vst" ) );

    return res;
}

#if !defined(__WIN32__) && !defined(_WIN32) && !defined(WIN32)
#define __cdecl
#endif

#if !defined(VST_2_3_EXTENSIONS)
typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;
#define VSTCALLBACK
#endif

typedef AEffect* ( *VST_GetPluginInstance ) ( audioMasterCallback );

static VstIntPtr VSTCALLBACK
vst2_discovery_scan_callback( AEffect *effect,
    VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt );

// Current working VST Shell identifier.
static int g_iVst2ShellCurrentId = 0;

#define STR_MAX 0xFF

#ifdef VST2_CONTROLLER
// Specific extended flags that saves us
// from calling canDo() in audio callbacks.

enum VST_FlagsEx
{
    effFlagsExCanSendVstEvents = 1 << 0,
    effFlagsExCanSendVstMidiEvents = 1 << 1,
    effFlagsExCanSendVstTimeInfo = 1 << 2,
    effFlagsExCanReceiveVstEvents = 1 << 3,
    effFlagsExCanReceiveVstMidiEvents = 1 << 4,
    effFlagsExCanReceiveVstTimeInfo = 1 << 5,
    effFlagsExCanProcessOffline = 1 << 6,
    effFlagsExCanUseAsInsert = 1 << 7,
    effFlagsExCanUseAsSend = 1 << 8,
    effFlagsExCanMixDryWet = 1 << 9,
    effFlagsExCanMidiProgramNames = 1 << 10
};

// Some VeSTige missing opcodes and flags.
const int effSetProgramName = 4;
const int effGetParamLabel = 6;
const int effGetParamDisplay = 7;
const int effGetChunk = 23;
const int effSetChunk = 24;
const int effFlagsProgramChunks = 32;
#endif

//----------------------------------------------------------------------
// class vst2_discovery_scan -- VST2 plugin re bones) interface
//

// Constructor.

vst2_discovery_scan::vst2_discovery_scan( void )
    : m_pLibrary( nullptr ), m_pEffect( nullptr ), m_iFlagsEx( 0 ), m_bEditor( false )
{
}

// destructor.

vst2_discovery_scan::~vst2_discovery_scan( void )
{
    close ( );
}

// File loader.

bool
vst2_discovery_scan::open( const std::string& sFilename )
{
    close ( );

    m_pLibrary = lib_open ( sFilename.c_str ( ) );

    if ( m_pLibrary == nullptr )
    {
        DMESSAGE ( "Cannot Open %s", sFilename.c_str ( ) );
        return false;
    }

    DMESSAGE ( "Open %s", sFilename.c_str ( ) );

    return true;
}

// Plugin loader.

bool
vst2_discovery_scan::open_descriptor( unsigned long iIndex )
{
    if ( m_pLibrary == nullptr )
        return false;

    close_descriptor ( );

    DMESSAGE ( "open_descriptor - iIndex = (%lu)", iIndex );

    VST_GetPluginInstance pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>( m_pLibrary, "VSTPluginMain" );

    if ( pfnGetPluginInstance == nullptr )
        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> ( m_pLibrary, "main" );

    if ( pfnGetPluginInstance == nullptr )
    {
        DMESSAGE ( "error", "Not a VST plugin" );
        return false;
    }

    m_pEffect = ( *pfnGetPluginInstance )( vst2_discovery_scan_callback );

    if ( m_pEffect == nullptr )
    {
        DMESSAGE ( "plugin instance could not be created." );
        return false;
    }

    // Did VST plugin instantiated OK?
    if ( m_pEffect->magic != kEffectMagic )
    {
        DMESSAGE ( "Plugin is not a valid VST." );
        m_pEffect = nullptr;
        return false;
    }

    // Check whether it's a VST Shell...
    const int categ = vst2_dispatch ( effGetPlugCategory, 0, 0, nullptr, 0.0f );
    if ( categ == kPlugCategShell )
    {
        DMESSAGE ( "GOT VST SHELL" );
        int id = 0;
        char buf[40];
        unsigned long i = 0;
        for (; iIndex >= i; ++i )
        {
            buf[0] = (char) 0;
            id = vst2_dispatch ( effShellGetNextPlugin, 0, 0, (void *) buf, 0.0f );
            if ( id == 0 || !buf[0] )
                break;
        }
        // Check if we're actually the intended plugin...
        if ( i < iIndex || id == 0 || !buf[0] )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin is not a valid VST.", iIndex );
            m_pEffect = nullptr;
            return false;
        }
        // Make it known...
        g_iVst2ShellCurrentId = id;
        // Re-allocate the thing all over again...

        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>( m_pLibrary, "VSTPluginMain" );

        if ( pfnGetPluginInstance == nullptr )
            pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> ( m_pLibrary, "main" );

        if ( pfnGetPluginInstance == nullptr )
        {
            DMESSAGE ( "error", "Not a VST plugin" );
            m_pEffect = nullptr;
            return false;
        }

        m_pEffect = ( *pfnGetPluginInstance )( vst2_discovery_scan_callback );

        // Not needed anymore, hopefully...
        g_iVst2ShellCurrentId = 0;
        // Don't go further if failed...
        if ( m_pEffect == nullptr )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin instance could not be created.", iIndex );
            return false;
        }

        if ( m_pEffect->magic != kEffectMagic )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin is not a valid VST.", iIndex );
            m_pEffect = nullptr;
            return false;
        }

        DMESSAGE ( "vst2_shell(%lu) id=0x%x name=\"%s\"", i, id, buf );
    }
    else
        // Not a VST Shell plugin...
        if ( iIndex > 0 )
        {
            m_pEffect = nullptr;
            return false;
        }

    //	vst2_dispatch(effIdentify, 0, 0, nullptr, 0.0f);
    vst2_dispatch ( effOpen, 0, 0, nullptr, 0.0f );

    // Get label name...
    char szName[256];
    ::memset ( szName, 0, sizeof (szName ) );
    if ( vst2_dispatch ( effGetEffectName, 0, 0, (void *) szName, 0.0f ) )
        m_sName = szName;

    char strBuf[STR_MAX + 1];
    carla_zeroChars ( strBuf, STR_MAX + 1 );

    if ( vst2_dispatch ( effGetVendorString, 0, 0, strBuf, 0.0f ) )
        m_sVendor = strBuf;
    else
        m_sVendor.clear ( );

    // get category
    switch ( vst2_dispatch ( effGetPlugCategory, 0, 0, nullptr, 0.0f ) )
    {
        case kPlugCategSynth:
        case kPlugCategGenerator:
            m_sCategory = "Instrument Plugin";
            break;
        case kPlugCategAnalysis:
        case kPlugCategRestoration:
            m_sCategory = "Utilities";
            break;
        case kPlugCategMastering:
            m_sCategory = "Amplitude/Dynamics";
            break;
        case kPlugCategRoomFx:
            m_sCategory = "Time/Delays";
            break;

        default:
            if ( m_pEffect->flags & effFlagsIsSynth )
                m_sCategory = "Instrument Plugin";
            else
                m_sCategory = "Unclassified";
            break;
    }

#ifdef VST2_CONTROLLER
    // Specific inquiries...
    m_iFlagsEx = 0;

    if ( vst2_canDo ( "sendVstEvents" ) ) m_iFlagsEx |= effFlagsExCanSendVstEvents;
    if ( vst2_canDo ( "sendVstMidiEvent" ) ) m_iFlagsEx |= effFlagsExCanSendVstMidiEvents;
    if ( vst2_canDo ( "sendVstTimeInfo" ) ) m_iFlagsEx |= effFlagsExCanSendVstTimeInfo;
    if ( vst2_canDo ( "receiveVstEvents" ) ) m_iFlagsEx |= effFlagsExCanReceiveVstEvents;
    if ( vst2_canDo ( "receiveVstMidiEvent" ) ) m_iFlagsEx |= effFlagsExCanReceiveVstMidiEvents;
    if ( vst2_canDo ( "receiveVstTimeInfo" ) ) m_iFlagsEx |= effFlagsExCanReceiveVstTimeInfo;
    if ( vst2_canDo ( "offline" ) ) m_iFlagsEx |= effFlagsExCanProcessOffline;
    if ( vst2_canDo ( "plugAsChannelInsert" ) ) m_iFlagsEx |= effFlagsExCanUseAsInsert;
    if ( vst2_canDo ( "plugAsSend" ) ) m_iFlagsEx |= effFlagsExCanUseAsSend;
    if ( vst2_canDo ( "mixDryWet" ) ) m_iFlagsEx |= effFlagsExCanMixDryWet;
    if ( vst2_canDo ( "midiProgramNames" ) ) m_iFlagsEx |= effFlagsExCanMidiProgramNames;

    m_bEditor = ( m_pEffect->flags & effFlagsHasEditor );
#endif
    return true;
}

// Plugin unloader.

void
vst2_discovery_scan::close_descriptor( void )
{
    if ( m_pEffect == nullptr )
        return;

    DMESSAGE ( "close_descriptor()" );

    //    vst2_dispatch(effClose, 0, 0, 0, 0.0f);

    m_pEffect = nullptr;
    m_iFlagsEx = 0;
    m_bEditor = false;
    m_sName.clear ( );
}

// File unloader.

void
vst2_discovery_scan::close( void )
{
    if ( m_pLibrary == nullptr )
        return;

    DMESSAGE ( "close()" );

    //    vst2_dispatch(effClose, 0, 0, 0, 0.0f);

    //    lib_close(m_pLibrary);
    m_pLibrary = nullptr;

    m_bEditor = false;
}

// Check wether plugin is loaded.

bool
vst2_discovery_scan::isOpen( void ) const
{
    if ( m_pLibrary == nullptr )
        return false;

    return true;
}

unsigned int
vst2_discovery_scan::uniqueID( ) const
{
    return (m_pEffect ? m_pEffect->uniqueID : 0 );
}

int
vst2_discovery_scan::numInputs( ) const
{
    return (m_pEffect ? m_pEffect->numInputs : 0 );
}

int
vst2_discovery_scan::numOutputs( ) const
{
    return (m_pEffect ? m_pEffect->numOutputs : 0 );
}

#ifdef VST2_CONTROLLER

int
vst2_discovery_scan::numPrograms( ) const
{
    return (m_pEffect ? m_pEffect->numPrograms : 0 );
}

int
vst2_discovery_scan::numParams( ) const
{
    return (m_pEffect ? m_pEffect->numParams : 0 );
}

int
vst2_discovery_scan::numMidiInputs( ) const
{
    return (m_pEffect && (
        ( m_iFlagsEx & effFlagsExCanReceiveVstMidiEvents ) ||
        ( m_pEffect->flags & effFlagsIsSynth ) ? 1 : 0 ) );
}

int
vst2_discovery_scan::numMidiOutputs( ) const
{
    return (( m_iFlagsEx & effFlagsExCanSendVstMidiEvents ) ? 1 : 0 );
}

bool
vst2_discovery_scan::hasEditor( ) const
{
    return m_bEditor;
}

bool
vst2_discovery_scan::hasProgramChunks( ) const
{
    return (m_pEffect && ( m_pEffect->flags & effFlagsProgramChunks ) );
}
#endif

// VST host dispatcher.

int
vst2_discovery_scan::vst2_dispatch(
    long opcode, long index, long value, void *ptr, float opt ) const
{
    if ( m_pEffect == nullptr )
        return 0;

    //   DMESSAGE("vst2_dispatch(%ld, %ld, %ld, %p, %g)",
    //           opcode, index, value, ptr, opt);

    return m_pEffect->dispatcher ( m_pEffect, opcode, index, value, ptr, opt );
}

// VST flag inquirer.

bool
vst2_discovery_scan::vst2_canDo( const char *pszCanDo ) const
{
    return (vst2_dispatch ( effCanDo, 0, 0, (void *) pszCanDo, 0.0f ) > 0 );
}

//----------------------------------------------------------------------
// The magnificient host callback, which every VST plugin will call.

static VstIntPtr VSTCALLBACK
vst2_discovery_scan_callback( AEffect* effect,
    VstInt32 opcode, VstInt32 index, VstIntPtr /*value*/, void */*ptr*/, float opt )
{
    VstIntPtr ret = 0;

    switch ( opcode )
    {
        case audioMasterVersion:
            ret = 2;
            break;
        case audioMasterAutomate:
            effect->setParameter ( effect, index, opt );
            break;
        case audioMasterCurrentId:
            ret = (VstIntPtr) g_iVst2ShellCurrentId;
            break;
        case audioMasterGetSampleRate:
            effect->dispatcher ( effect, effSetSampleRate, 0, 0, nullptr, 44100.0f );
            break;
        case audioMasterGetBlockSize:
            effect->dispatcher ( effect, effSetBlockSize, 0, 1024, nullptr, 0.0f );
            break;
        case audioMasterGetAutomationState:
            ret = 1; // off
            break;
        case audioMasterGetLanguage:
            ret = kVstLangEnglish;
            break;
        default:
            break;
    }

    return ret;
}

//-------------------------------------------------------------------------
// The VST plugin stance scan method.
//

void
vst2_discovery_scan_file( const std::string& sFilename, std::list<Plugin_Info> & vst2pr )
{
    DMESSAGE ( "scan_file(\"%s\")", sFilename.c_str ( ) );

    vst2_discovery_scan plugin;

    if ( !plugin.open ( sFilename ) )
        return;

    unsigned long i = 0;
    while ( plugin.open_descriptor ( i ) )
    {
        Plugin_Info pi ( "VST2" );

        pi.s_unique_id = "(null)"; // (null) since we have to have something for favorites save and scan
        pi.name = plugin.name ( );
        pi.author = plugin.vendor ( );
        pi.category = plugin.category ( );
        pi.audio_inputs = plugin.numInputs ( );
        pi.audio_outputs = plugin.numOutputs ( );
        pi.midi_inputs = plugin.numMidiInputs ( );
        pi.midi_outputs = plugin.numMidiOutputs ( );

        pi.id = plugin.uniqueID ( );
        pi.plug_path = sFilename;

        vst2pr.push_back ( pi );

        DMESSAGE ( "name = %s: category = %s: ID = %u: PATH = %s",
            pi.name.c_str ( ), pi.category.c_str ( ), pi.id, pi.plug_path.c_str ( ) );
        DMESSAGE ( "Vendor = %s", pi.author.c_str ( ) );

        plugin.close_descriptor ( );
        ++i;
    }

    plugin.close ( );
}

} // namespace vst2_discovery

#endif  // VST2_SUPPORT
