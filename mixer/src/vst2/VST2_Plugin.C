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
 * File:   VST2_Plugin.C
 * Author: sspresto
 * 
 * Created on January 13, 2024, 5:36 PM
 */

#ifdef VST2_SUPPORT

#include <filesystem>
#include <FL/fl_ask.H>  // fl_alert()
#include "VST2_Plugin.H"
#include "../../../nonlib/dsp.h"
#include "../Chain.H"
#include "../Mixer_Strip.H"
#include "Vst2_Discovery.H"

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
Vst2Plugin_HostCallback( AEffect* effect,
                         VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt );

// Dynamic singleton list of VST2 plugins.
static std::map<AEffect *, VST2_Plugin *> g_vst2Plugins;

// Current working VST2 Shell identifier.
static int g_iVst2ShellCurrentId = 0;

// Specific extended flags that saves us
// from calling canDo() in audio callbacks.

enum qtractorVst2PluginFlagsEx
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
const int effGetProgramNameIndexed = 29;
const int effFlagsProgramChunks = 32;

static const int32_t kVstMidiEventSize = static_cast<int32_t> ( sizeof (VstMidiEvent ) );

struct ERect
{
    int16_t top, left, bottom, right;
};

const float F_DEFAULT_MSECS = 0.03f;

VST2_Plugin::VST2_Plugin( ) :
    Plugin_Module( ),
    _plugin_filename( ),
    _iUniqueID( 0 ),
    _pLibrary( nullptr ),
    _pEffect( nullptr ),
    _iFlagsEx( 0 ),
    _sName( ),
    _project_file( ),
    _found_plugin( false ),
    _fMidiEventCount( 0 ),
    _fTimeInfo( ),
    _fEvents( ),
    _iControlIns( 0 ),
    _iControlOuts( 0 ),
    _iAudioIns( 0 ),
    _iAudioOuts( 0 ),
    _iMidiIns( 0 ),
    _iMidiOuts( 0 ),
    _param_props( ),
    _bRealtime( false ),
    _bConfigure( false ),
    _bEditor( false ),
    _activated( false ),
    _position( 0 ),
    _bpm( 120.0f ),
    _rolling( false ),
    _bEditorCreated( false ),
    _X11_UI( nullptr ),
    _x_is_visible( false ),
    _audio_in_buffers( nullptr ),
    _audio_out_buffers( nullptr )
{
    _plug_type = Type_VST2;

    non_zeroStructs ( _fMidiEvents, kPluginMaxMidiEvents * 2 );
    non_zeroStruct ( _fTimeInfo );

    for ( ushort i = 0; i < kPluginMaxMidiEvents * 2; ++i )
        _fEvents.data[i] = ( VstEvent* ) & _fMidiEvents[i];


    log_create ( );
}

VST2_Plugin::~VST2_Plugin( )
{
    log_destroy ( );

    // close custom ui if it is up
    if ( _x_is_visible )
        hide_custom_ui ( );

    deactivate ( );

    g_vst2Plugins.erase ( _pEffect ); // erasing by key

    if ( _audio_in_buffers )
    {
        delete []_audio_in_buffers;
        _audio_in_buffers = nullptr;
    }

    if ( _audio_out_buffers )
    {
        delete []_audio_out_buffers;
        _audio_out_buffers = nullptr;
    }

    for ( unsigned int i = 0; i < midi_input.size ( ); ++i )
    {
        if ( !( midi_input[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_input[i].jack_port ( ) )
        {
            midi_input[i].disconnect ( );
            midi_input[i].jack_port ( )->shutdown ( );
            delete midi_input[i].jack_port ( );
        }
    }
    for ( unsigned int i = 0; i < midi_output.size ( ); ++i )
    {
        if ( !( midi_output[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_output[i].jack_port ( ) )
        {
            midi_output[i].disconnect ( );
            midi_output[i].jack_port ( )->shutdown ( );
            delete midi_output[i].jack_port ( );
        }
    }

    midi_output.clear ( );
    midi_input.clear ( );

    /* This is the case when the user manually removes a Plugin. We set the
     _is_removed = true, and add any custom data directory to the remove directories
     vector. If the user saves the project then we remove any items in the vector.
     We also clear the vector. If the user abandons any changes on exit, then any
     items added to the vector since the last save will not be removed */
    if ( _is_removed )
    {
        if ( !_project_file.empty ( ) )
        {
            remove_custom_data_directories.push_back ( _project_file );
        }
    }
}

bool
VST2_Plugin::load_plugin( Module::Picked picked )
{
    _plugin_filename = picked.s_plug_path;
    _iUniqueID = picked.unique_id;

    if ( !find_plugin_binary ( ) )
    {
        MESSAGE ( "Could not find plugin binary %s", _plugin_filename.c_str ( ) );
        return false;
    }

    if ( !open_lib ( _plugin_filename ) )
        return false;

    _found_plugin = false;
    unsigned long i = 0;
    while ( open_descriptor ( i ) )
    {
        if ( _found_plugin )
            break;

        ++i;
    }

    if ( !_found_plugin )
    {
        DMESSAGE ( "Could not find %s: ID = (%i)", _plugin_filename.c_str ( ), _iUniqueID );
        return false;
    }

    base_label ( _sName.c_str ( ) );

    std::pair<AEffect *, VST2_Plugin *> efct ( _pEffect, this );
    g_vst2Plugins.insert ( efct );

    initialize_plugin ( );

    create_audio_ports ( );
    create_midi_ports ( );
    create_control_ports ( );

    get_presets ( );
    // get_presets() may have to cycle through the preset to get the name
    // so we re-set to the default here just in case.
    vst2_dispatch ( effSetProgram, 0, 0, 0, 0.0f );

    activate ( );

    if ( !_plugin_ins )
        is_zero_input_synth ( true );

    _use_custom_data = true;

    return true;
}

bool
VST2_Plugin::configure_inputs( int n )
{
    /* The synth case - no inputs and JACK module has one */
    if ( ninputs ( ) == 0 && n == 1 )
    {
        _crosswire = false;
    }
    else if ( ninputs ( ) != n )
    {
        _crosswire = false;

        if ( 1 == n && plugin_ins ( ) > 1 )
        {
            DMESSAGE ( "Cross-wiring plugin inputs" );
            _crosswire = true;

            audio_input.clear ( );

            for ( int i = n; i--; )
                audio_input.push_back ( Port ( this, Port::INPUT, Port::AUDIO ) );
        }

        else if ( n == plugin_ins ( ) )
        {
            DMESSAGE ( "Plugin input configuration is a perfect match" );
        }
        else
        {
            DMESSAGE ( "Unsupported input configuration" );
            return false;
        }
    }

    return true;

}

void
VST2_Plugin::handle_port_connection_change( void )
{
    if ( loaded ( ) )
    {
        if ( _crosswire )
        {
            for ( int i = 0; i < plugin_ins ( ); ++i )
                set_input_buffer ( i, audio_input[0].buffer ( ) );
        }
        else
        {
            for ( unsigned int i = 0; i < audio_input.size ( ); ++i )
                set_input_buffer ( i, audio_input[i].buffer ( ) );
        }

        for ( unsigned int i = 0; i < audio_output.size ( ); ++i )
            set_output_buffer ( i, audio_output[i].buffer ( ) );
    }
}

void
VST2_Plugin::handle_chain_name_changed( void )
{
    Module::handle_chain_name_changed ( );

    if ( !chain ( )->strip ( )->group ( )->single ( ) )
    {
        for ( unsigned int i = 0; i < midi_input.size ( ); i++ )
        {
            if ( !( midi_input[i].type ( ) == Port::MIDI ) )
                continue;

            if ( midi_input[i].jack_port ( ) )
            {
                midi_input[i].jack_port ( )->trackname ( chain ( )->name ( ) );
                midi_input[i].jack_port ( )->rename ( );
            }
        }
        for ( unsigned int i = 0; i < midi_output.size ( ); i++ )
        {
            if ( !( midi_output[i].type ( ) == Port::MIDI ) )
                continue;

            if ( midi_output[i].jack_port ( ) )
            {
                midi_output[i].jack_port ( )->trackname ( chain ( )->name ( ) );
                midi_output[i].jack_port ( )->rename ( );
            }
        }
    }
}

void
VST2_Plugin::handle_sample_rate_change( nframes_t /*sample_rate*/ )
{
    deactivate ( );
    activate ( );
}

void
VST2_Plugin::resize_buffers( nframes_t /*buffer_size*/ )
{
    deactivate ( );
    activate ( );
}

void
VST2_Plugin::bypass( bool v )
{
    if ( v != bypass ( ) )
    {
        if ( v )
            deactivate ( );
        else
            activate ( );
    }
}

void
VST2_Plugin::freeze_ports( void )
{
    Module::freeze_ports ( );

    for ( unsigned int i = 0; i < midi_input.size ( ); ++i )
    {
        if ( !( midi_input[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_input[i].jack_port ( ) )
        {
            midi_input[i].jack_port ( )->freeze ( );
            midi_input[i].jack_port ( )->shutdown ( );
        }
    }

    for ( unsigned int i = 0; i < midi_output.size ( ); ++i )
    {
        if ( !( midi_output[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_output[i].jack_port ( ) )
        {
            midi_output[i].jack_port ( )->freeze ( );
            midi_output[i].jack_port ( )->shutdown ( );
        }
    }
}

void
VST2_Plugin::thaw_ports( void )
{
    Module::thaw_ports ( );

    const char *trackname = chain ( )->strip ( )->group ( )->single ( ) ? NULL : chain ( )->name ( );

    for ( unsigned int i = 0; i < midi_input.size ( ); ++i )
    {
        /* if we're entering a group we need to add the chain name
         * prefix and if we're leaving one, we need to remove it */
        if ( !( midi_input[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_input[i].jack_port ( ) )
        {
            midi_input[i].jack_port ( )->client ( chain ( )->client ( ) );
            midi_input[i].jack_port ( )->trackname ( trackname );
            midi_input[i].jack_port ( )->thaw ( );
        }
    }

    for ( unsigned int i = 0; i < midi_output.size ( ); ++i )
    {
        /* if we're entering a group we won't actually be using our
         * JACK output ports anymore, just mixing into the group outputs */
        if ( !( midi_output[i].type ( ) == Port::MIDI ) )
            continue;

        if ( midi_output[i].jack_port ( ) )
        {
            midi_output[i].jack_port ( )->client ( chain ( )->client ( ) );
            midi_output[i].jack_port ( )->trackname ( trackname );
            midi_output[i].jack_port ( )->thaw ( );
        }
    }
}

void
VST2_Plugin::clear_midi_vectors( )
{
    midi_input.clear ( );
    midi_output.clear ( );
}

void
VST2_Plugin::configure_midi_inputs( )
{
    if ( !midi_input.size ( ) )
        return;

    const char *trackname = chain ( )->strip ( )->group ( )->single ( ) ? NULL : chain ( )->name ( );

    for ( unsigned int i = 0; i < midi_input.size ( ); ++i )
    {
        if ( !( midi_input[i].type ( ) == Port::MIDI ) )
            continue;

        std::string port_name = label ( );

        port_name += " ";
        port_name += midi_input[i].name ( );

        DMESSAGE ( "CONFIGURE MIDI INPUTS = %s", port_name.c_str ( ) );
        JACK::Port *jack_port = new JACK::Port ( chain ( )->client ( ), trackname, port_name.c_str ( ), JACK::Port::Input, JACK::Port::MIDI );
        midi_input[i].jack_port ( jack_port );

        if ( !midi_input[i].jack_port ( )->activate ( ) )
        {
            delete midi_input[i].jack_port ( );
            midi_input[i].jack_port ( NULL );
            WARNING ( "Failed to activate JACK MIDI IN port" );
            return;
        }
    }
}

void
VST2_Plugin::configure_midi_outputs( )
{
    if ( !midi_output.size ( ) )
        return;

    const char *trackname = chain ( )->strip ( )->group ( )->single ( ) ? NULL : chain ( )->name ( );

    for ( unsigned int i = 0; i < midi_output.size ( ); ++i )
    {
        if ( !( midi_output[i].type ( ) == Port::MIDI ) )
            continue;

        std::string port_name = label ( );

        port_name += " ";
        port_name += midi_output[i].name ( );

        DMESSAGE ( "CONFIGURE MIDI OUTPUTS = %s", port_name.c_str ( ) );
        JACK::Port *jack_port = new JACK::Port ( chain ( )->client ( ), trackname, port_name.c_str ( ), JACK::Port::Output, JACK::Port::MIDI );
        midi_output[i].jack_port ( jack_port );

        if ( !midi_output[i].jack_port ( )->activate ( ) )
        {
            delete midi_output[i].jack_port ( );
            midi_output[i].jack_port ( NULL );
            WARNING ( "Failed to activate JACK MIDI OUT port" );
            return;
        }
    }
}

nframes_t
VST2_Plugin::get_module_latency( void ) const
{
    const VstInt32 *pInitialDelay
            = ( VstInt32 * ) &( _pEffect->empty3[0] );

    return *pInitialDelay;
}

void
VST2_Plugin::process( nframes_t nframes )
{
    handle_port_connection_change ( );

    if ( unlikely ( bypass ( ) ) )
    {
        /* If this is a mono to stereo plugin, then duplicate the input channel... */
        /* There's not much we can do to automatically support other configurations. */
        if ( ninputs ( ) == 1 && noutputs ( ) == 2 )
        {
            buffer_copy ( static_cast<sample_t*> ( audio_output[1].buffer ( ) ),
                    static_cast<sample_t*> ( audio_input[0].buffer ( ) ), nframes );
        }

        _latency = 0;
    }
    else
    {
        if ( _pEffect == nullptr )
            return;

        process_jack_transport ( nframes );

        _fMidiEventCount = 0;
        non_zeroStructs ( _fMidiEvents, kPluginMaxMidiEvents * 2 );

        for ( unsigned int i = 0; i < midi_input.size ( ); ++i )
        {
            /* JACK MIDI in to plugin MIDI in */
            process_jack_midi_in ( nframes, i );
        }

        if ( _fMidiEventCount > 0 )
        {
            _fEvents.numEvents = static_cast<int32_t> ( _fMidiEventCount );
            _fEvents.reserved = 0;
            vst2_dispatch ( effProcessEvents, 0, 0, &_fEvents, 0.0f );
        }

        // Make it run audio...
        if ( _pEffect->flags & effFlagsCanReplacing )
        {
            _pEffect->processReplacing (
                    _pEffect, _audio_in_buffers, _audio_out_buffers, nframes );
        }

        for ( unsigned int i = 0; i < midi_output.size ( ); ++i )
        {
            /* Plugin to JACK MIDI out */
            process_jack_midi_out ( nframes, i );
        }

        _fTimeInfo.samplePos += nframes;
    }
}

bool
VST2_Plugin::try_custom_ui( )
{
    if ( !_bEditor )
        return false;

    /* Toggle show and hide */
    if ( _bEditorCreated )
    {
        if ( _x_is_visible )
        {
            hide_custom_ui ( );
            return true;
        }
        else
        {
            show_custom_ui ( );
            return true;
        }
    }

    intptr_t value = 0;

    if ( _X11_UI == nullptr )
    {
        _X11_UI = new X11PluginUI ( this, false, false );
        _X11_UI->setTitle ( label ( ) );

        value = (intptr_t) _X11_UI->getDisplay ( );

        // NOTE: there are far too many broken VST2 plugins, don't bother checking return value
        if ( vst2_dispatch ( effEditOpen, 0, value, _X11_UI->getPtr ( ), 0.0f ) != 0 || true )
        {
            ERect* vstRect = nullptr;
            vst2_dispatch ( effEditGetRect, 0, 0, &vstRect, 0.0f );

            if ( vstRect != nullptr )
            {
                const int width ( vstRect->right - vstRect->left );
                const int height ( vstRect->bottom - vstRect->top );

                // CARLA_SAFE_ASSERT_INT2(width > 1 && height > 1, width, height);

                if ( width > 1 && height > 1 )
                    _X11_UI->setSize ( static_cast<uint> ( width ), static_cast<uint> ( height ), true, true );
            }
        }
        else
        {
            delete _X11_UI;
            _X11_UI = nullptr;

            DMESSAGE ( "Plugin refused to open its own UI" );
            return false;
        }
    }

    _bEditorCreated = show_custom_ui ( );
    return _bEditorCreated;
}

bool
VST2_Plugin::show_custom_ui( )
{
    if ( _X11_UI )
    {
        _X11_UI->show ( );
        _X11_UI->focus ( );

        _x_is_visible = true;
        Fl::add_timeout ( F_DEFAULT_MSECS, &VST2_Plugin::custom_update_ui, this );
        return true;
    }

    return false;
}

/**
 Callback for custom ui idle interface
 */
void
VST2_Plugin::custom_update_ui( void *v )
{
    ( (VST2_Plugin*) v )->custom_update_ui_x ( );
}

/**
 The idle callback to update_custom_ui()
 */
void
VST2_Plugin::custom_update_ui_x( )
{
    vst2_dispatch ( effEditIdle, 0, 0, 0, 0.0f );

    if ( _x_is_visible )
        _X11_UI->idle ( );

    if ( _x_is_visible )
    {
        Fl::repeat_timeout ( F_DEFAULT_MSECS, &VST2_Plugin::custom_update_ui, this );
    }
    else
    {
        hide_custom_ui ( );
    }
}

bool
VST2_Plugin::hide_custom_ui( )
{
    DMESSAGE ( "Closing Custom Interface" );

    Fl::remove_timeout ( &VST2_Plugin::custom_update_ui, this );
    vst2_dispatch ( effEditClose, 0, 0, 0, 0.0f );

    if ( _X11_UI != nullptr )
        _X11_UI->hide ( );

    _x_is_visible = false;
    _bEditorCreated = false;

    if ( _X11_UI != nullptr )
    {
        delete _X11_UI;
        _X11_UI = nullptr;
    }

    return true;
}

bool
VST2_Plugin::find_plugin_binary( )
{
    // First check the path from the snapshot
    if ( std::filesystem::exists ( _plugin_filename ) )
        return true;

    /*  We did not find the plugin from the snapshot path so lets try
        a different path. The case is if the project was copied to a
        different computer in which the plugins are installed in a different
        location - i.e. - /usr/lib vs /usr/local/lib.
     */

    std::string file ( _plugin_filename );
    std::string restore;
    // Find the base plugin name
    std::size_t found = file.find_last_of ( "/\\" );
    restore = file.substr ( found );
    DMESSAGE ( "Restore = %s", restore.c_str ( ) );

    auto sp = vst2_discovery::installedVST2s ( );

    for ( const auto &q : sp )
    {
        std::string path = q.u8string ( );
        found = path.find_last_of ( "/\\" );
        std::string base = path.substr ( found );

        //    DMESSAGE("Restore = %s: Base = %s", restore.c_str(), base.c_str());
        // Compare the base names and if they match, then use the path
        if ( strcmp ( restore.c_str ( ), base.c_str ( ) ) == 0 )
        {
            if ( std::filesystem::exists ( path ) )
            {
                _plugin_filename = path;
                return true; // We found it
            }
            else
                return false; // If it still does not exist then abandon
        }
        else
        {
            // keep trying until all available paths are checked
            continue;
        }
    }

    // We never found it
    return false;
}

bool
VST2_Plugin::open_lib( const std::string& sFilename )
{
    close_lib ( );

    _pLibrary = lib_open ( sFilename.c_str ( ) );

    if ( _pLibrary == nullptr )
    {
        DMESSAGE ( "Cannot Open %s", sFilename.c_str ( ) );
        return false;
    }

    DMESSAGE ( "Open %s", sFilename.c_str ( ) );

    return true;
}

void
VST2_Plugin::close_lib( )
{
    if ( _pLibrary == nullptr )
        return;

    DMESSAGE ( "close()" );

    vst2_dispatch ( effClose, 0, 0, 0, 0.0f );

    _pLibrary = nullptr;
}

bool
VST2_Plugin::open_descriptor( unsigned long iIndex )
{
    if ( _pLibrary == nullptr )
        return false;

    close_descriptor ( );

    DMESSAGE ( "open_descriptor - iIndex = (%lu)", iIndex );

    VST_GetPluginInstance pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>( _pLibrary, "VSTPluginMain" );

    if ( pfnGetPluginInstance == nullptr )
        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> ( _pLibrary, "main" );

    if ( pfnGetPluginInstance == nullptr )
    {
        DMESSAGE ( "error", "Not a VST plugin" );
        return false;
    }

    _pEffect = ( *pfnGetPluginInstance )( Vst2Plugin_HostCallback );

    if ( _pEffect == nullptr )
    {
        DMESSAGE ( "plugin instance could not be created." );
        return false;
    }

    // Did VST plugin instantiated OK?
    if ( _pEffect->magic != kEffectMagic )
    {
        DMESSAGE ( "Plugin is not a valid VST." );
        _pEffect = nullptr;
        return false;
    }

    // Check whether it's a VST Shell...
    const int categ = vst2_dispatch ( effGetPlugCategory, 0, 0, nullptr, 0.0f );
    if ( categ == kPlugCategShell )
    {
        int id = 0;
        char buf[40];
        unsigned long i = 0;
        for (; iIndex >= i; ++i )
        {
            buf[0] = (char) 0;
            id = vst2_dispatch ( effShellGetNextPlugin, 0, 0, (void *) buf, 0.0f );

            if ( _iUniqueID != id )
            {
                continue;
            }
            else
            {
                _found_plugin = true;
                break;
            }

            if ( id == 0 || !buf[0] )
                break;
        }
        // Check if we're actually the intended plugin...
        if ( i < iIndex || id == 0 || !buf[0] )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin is not a valid VST.", iIndex );
            _pEffect = nullptr;
            return false;
        }
        // Make it known...
        g_iVst2ShellCurrentId = id;
        // Re-allocate the thing all over again...

        pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance>( _pLibrary, "VSTPluginMain" );

        if ( pfnGetPluginInstance == nullptr )
            pfnGetPluginInstance = lib_symbol<VST_GetPluginInstance> ( _pLibrary, "main" );

        if ( pfnGetPluginInstance == nullptr )
        {
            DMESSAGE ( "error", "Not a VST plugin" );
            _pEffect = nullptr;
            return false;
        }

        _pEffect = ( *pfnGetPluginInstance )( Vst2Plugin_HostCallback );

        // Not needed anymore, hopefully...
        g_iVst2ShellCurrentId = 0;
        // Don't go further if failed...
        if ( _pEffect == nullptr )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin instance could not be created.", iIndex );
            return false;
        }

        if ( _pEffect->magic != kEffectMagic )
        {
            DMESSAGE ( "vst2_shell(%lu) plugin is not a valid VST.", iIndex );
            _pEffect = nullptr;
            return false;
        }

        DMESSAGE ( "vst2_shell(%lu) id=0x%x name=\"%s\"", i, id, buf );
    }
    else
        // Not a VST Shell plugin...
        if ( iIndex > 0 )
    {
        _pEffect = nullptr;
        return false;
    }

    //	vst2_dispatch(effIdentify, 0, 0, nullptr, 0.0f);
    vst2_dispatch ( effOpen, 0, 0, nullptr, 0.0f );

    // Get label name...
    char szName[256];
    ::memset ( szName, 0, sizeof (szName ) );
    if ( vst2_dispatch ( effGetEffectName, 0, 0, (void *) szName, 0.0f ) )
    {
        _sName = szName;
    }

    if ( _pEffect->uniqueID == _iUniqueID )
        _found_plugin = true;

#if 0
    // Specific inquiries...
    _iFlagsEx = 0;

    if ( vst2_canDo ( "sendVstEvents" ) ) _iFlagsEx |= effFlagsExCanSendVstEvents;
    if ( vst2_canDo ( "sendVstMidiEvent" ) ) _iFlagsEx |= effFlagsExCanSendVstMidiEvents;
    if ( vst2_canDo ( "sendVstTimeInfo" ) ) _iFlagsEx |= effFlagsExCanSendVstTimeInfo;
    if ( vst2_canDo ( "receiveVstEvents" ) ) _iFlagsEx |= effFlagsExCanReceiveVstEvents;
    if ( vst2_canDo ( "receiveVstMidiEvent" ) ) _iFlagsEx |= effFlagsExCanReceiveVstMidiEvents;
    if ( vst2_canDo ( "receiveVstTimeInfo" ) ) _iFlagsEx |= effFlagsExCanReceiveVstTimeInfo;
    if ( vst2_canDo ( "offline" ) ) _iFlagsEx |= effFlagsExCanProcessOffline;
    if ( vst2_canDo ( "plugAsChannelInsert" ) ) _iFlagsEx |= effFlagsExCanUseAsInsert;
    if ( vst2_canDo ( "plugAsSend" ) ) _iFlagsEx |= effFlagsExCanUseAsSend;
    if ( vst2_canDo ( "mixDryWet" ) ) _iFlagsEx |= effFlagsExCanMixDryWet;
    if ( vst2_canDo ( "midiProgramNames" ) ) _iFlagsEx |= effFlagsExCanMidiProgramNames;

    _bEditor = ( _pEffect->flags & effFlagsHasEditor );
#endif
    return true;
}


// Plugin unloader.

void
VST2_Plugin::close_descriptor( void )
{
    if ( _pEffect == nullptr )
        return;

    DMESSAGE ( "close_descriptor()" );

    vst2_dispatch ( effClose, 0, 0, 0, 0.0f );

    _pEffect = nullptr;
    _iFlagsEx = 0;
    _bEditor = false;
}

void
VST2_Plugin::handlePluginUIClosed( )
{
    _x_is_visible = false;
}

void
VST2_Plugin::handlePluginUIResized( const uint width, const uint height )
{
    DMESSAGE ( "Handle Resized W = %d: H = %d", width, height );
    return; // Not used
}


// VST2 flag inquirer.

bool
VST2_Plugin::vst2_canDo( const char *pszCanDo ) const
{
    return (vst2_dispatch ( effCanDo, 0, 0, (void *) pszCanDo, 0.0f ) > 0 );
}

bool
VST2_Plugin::initialize_plugin( )
{
    // Specific inquiries...
    _iFlagsEx = 0;
    //	if (vst2_canDo("sendVstEvents"))       _iFlagsEx |= effFlagsExCanSendVstEvents;
    if ( vst2_canDo ( "sendVstMidiEvent" ) ) _iFlagsEx |= effFlagsExCanSendVstMidiEvents;
    //	if (vst2_canDo("sendVstTimeInfo"))     _iFlagsEx |= effFlagsExCanSendVstTimeInfo;
    //	if (vst2_canDo("receiveVstEvents"))    _iFlagsEx |= effFlagsExCanReceiveVstEvents;
    if ( vst2_canDo ( "receiveVstMidiEvent" ) ) _iFlagsEx |= effFlagsExCanReceiveVstMidiEvents;
    //	if (vst2_canDo("receiveVstTimeInfo"))  _iFlagsEx |= effFlagsExCanReceiveVstTimeInfo;
    //	if (vst2_canDo("offline"))             _iFlagsEx |= effFlagsExCanProcessOffline;
    //	if (vst2_canDo("plugAsChannelInsert")) _iFlagsEx |= effFlagsExCanUseAsInsert;
    //	if (vst2_canDo("plugAsSend"))          _iFlagsEx |= effFlagsExCanUseAsSend;
    //	if (vst2_canDo("mixDryWet"))           _iFlagsEx |= effFlagsExCanMixDryWet;
    //	if (vst2_canDo("midiProgramNames"))    _iFlagsEx |= effFlagsExCanMidiProgramNames;

    // Compute and cache port counts...
    _iControlIns = _pEffect->numParams;
    _iControlOuts = 0;
    _iAudioIns = _pEffect->numInputs;
    _iAudioOuts = _pEffect->numOutputs;
    _iMidiIns = ( ( _iFlagsEx & effFlagsExCanReceiveVstMidiEvents )
                  || ( _pEffect->flags & effFlagsIsSynth ) ? 1 : 0 );
    _iMidiOuts = ( ( _iFlagsEx & effFlagsExCanSendVstMidiEvents ) ? 1 : 0 );

    // Cache flags.
    _bRealtime = true;
    _bConfigure = ( _pEffect->flags & effFlagsProgramChunks );
    _bEditor = ( _pEffect->flags & effFlagsHasEditor );

    // HACK: Some native VST2 plugins with a GUI editor
    // need to skip explicit shared library unloading,
    // on close, in order to avoid mysterious crashes
    // later on session and/or application exit.
    //  if (_bEditor) file()->setAutoUnload(false);

    return true;
}


// VST host dispatcher.

int
VST2_Plugin::vst2_dispatch(
                            long opcode, long index, long value, void *ptr, float opt ) const
{
    if ( _pEffect == nullptr )
        return 0;

    //   DMESSAGE("vst2_dispatch(%ld, %ld, %ld, %p, %g)",
    //           opcode, index, value, ptr, opt);

    return _pEffect->dispatcher ( _pEffect, opcode, index, value, ptr, opt );
}

// Parameter update executive plugin to host

void
VST2_Plugin::updateParamValue(
                               unsigned long iIndex, float fValue, bool bUpdate )
{
    if ( iIndex < control_input.size ( ) )
    {
        float value = fValue;
        if ( control_input[iIndex].hints.type == Port::Hints::LV2_INTEGER )
            value = ( control_input[iIndex].hints.maximum - control_input[iIndex].hints.minimum ) * fValue;

        DMESSAGE ( "Param value = %f", value );

        if ( control_input[iIndex].control_value ( ) != value )
        {
            _is_from_custom_ui = !bUpdate;
            control_input[iIndex].control_value ( value );
        }
    }
}

// All parameters update method plugin to host

void
VST2_Plugin::updateParamValues( bool bUpdate )
{
    if ( _pEffect )
    {
        for ( unsigned int i = 0; i < control_input.size ( ); ++i )
        {
            const float fValue = _pEffect->getParameter ( _pEffect, static_cast<int32_t> ( i ) );
            updateParamValue ( i, fValue, bUpdate );
        }
    }
}

// Host to plugin

void
VST2_Plugin::setParameter( uint32_t iIndex, float value )
{
    if ( _pEffect )
    {
        _pEffect->setParameter ( _pEffect, iIndex, value );
    }
}

void
VST2_Plugin::idleEditor( void )
{
    DMESSAGE ( "IDLE" ); // What are we supposed to do here???

    if ( _x_is_visible )
        vst2_dispatch ( effEditIdle, 0, 0, 0, 0.0f );

}

// Our own editor widget size accessor.

void
VST2_Plugin::resizeEditor( int w, int h )
{
    DMESSAGE ( "W = %d: H = %d", w, h );
    _X11_UI->setSize ( w, h, true, false );
}



// Global VST2 plugin lookup.

VST2_Plugin *
VST2_Plugin::findPlugin( AEffect *pVst2Effect )
{
    std::map<AEffect *, VST2_Plugin *>::const_iterator got
            = g_vst2Plugins.find ( pVst2Effect );

    if ( got == g_vst2Plugins.end ( ) )
        return nullptr;
    else
        return got->second;
}

static VstIntPtr VSTCALLBACK
Vst2Plugin_HostCallback( AEffect *effect,
                         VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt )
{
    VstIntPtr ret = 0;
    VST2_Plugin *pVst2Plugin = nullptr;
    //	static VstTimeInfo s_vst2TimeInfo;

    switch ( opcode )
    {

            // VST 1.0 opcodes...
        case audioMasterVersion:
            DMESSAGE ( "audioMasterVersion" );
            ret = 2; // vst2.x
            break;

        case audioMasterAutomate:
            DMESSAGE ( "audioMasterAutomate" );
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                pVst2Plugin->updateParamValue ( index, opt, false );
            }
            break;

        case audioMasterCurrentId:
            DMESSAGE ( "audioMasterCurrentId" );
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                ret = (VstIntPtr) pVst2Plugin->get_unique_id ( );
            }

            break;

        case audioMasterIdle:
            DMESSAGE ( "audioMasterIdle" );
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                pVst2Plugin->updateParamValues ( false );
                pVst2Plugin->idleEditor ( ); // WTF
            }
            break;

        case audioMasterGetTime:
            //	DMESSAGE("audioMasterGetTime");
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                VstTimeInfo& _fTimeInfo = pVst2Plugin->get_time_info ( );
                ret = ( intptr_t ) & _fTimeInfo;
            }

            break;

        case audioMasterProcessEvents:
            DMESSAGE ( "audioMasterProcessEvents" );

            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                ret = pVst2Plugin->ProcessEvents ( ptr );
            }
            break;

        case audioMasterIOChanged:
            DMESSAGE ( "audioMasterIOChanged" );
            break;

        case audioMasterSizeWindow:
            DMESSAGE ( "audioMasterSizeWindow" );
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                pVst2Plugin->resizeEditor ( int(index ), int(value ) );
                ret = 1; // supported.
            }
            break;

        case audioMasterGetSampleRate:
            DMESSAGE ( "audioMasterGetSampleRate" );

            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                ret = (VstIntPtr) pVst2Plugin->sample_rate ( );
            }

            break;

        case audioMasterGetBlockSize:
            DMESSAGE ( "audioMasterGetBlockSize" );

            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                ret = (VstIntPtr) pVst2Plugin->buffer_size ( );
            }

            break;

        case audioMasterGetInputLatency:
            DMESSAGE ( "audioMasterGetInputLatency" );
            break;

        case audioMasterGetOutputLatency:
            DMESSAGE ( "audioMasterGetOutputLatency" );
            break;

        case audioMasterGetCurrentProcessLevel:
            //	DMESSAGE("audioMasterGetCurrentProcessLevel");
            break;

        case audioMasterGetAutomationState:
            DMESSAGE ( "audioMasterGetAutomationState" );
            ret = 1; // off.
            break;

#if !defined(VST_2_3_EXTENSIONS) 
        case audioMasterGetSpeakerArrangement:
            DMESSAGE ( "audioMasterGetSpeakerArrangement" );
            break;
#endif

        case audioMasterGetVendorString:
            DMESSAGE ( "audioMasterGetVendorString" );
            //::strcpy((char *) ptr, QTRACTOR_DOMAIN);
            ::strcpy ( (char *) ptr, WEBSITE );
            ret = 1; // ok.
            break;

        case audioMasterGetProductString:
            DMESSAGE ( "audioMasterGetProductString" );
            //::strcpy((char *) ptr, QTRACTOR_TITLE);
            ::strcpy ( (char *) ptr, PACKAGE );
            ret = 1; // ok.
            break;

        case audioMasterGetVendorVersion:
            DMESSAGE ( "audioMasterGetVendorVersion" );
            break;

        case audioMasterVendorSpecific:
            DMESSAGE ( "audioMasterVendorSpecific" );
            break;

        case audioMasterCanDo:
            DMESSAGE ( "audioMasterCanDo" );
            if ( ::strcmp ( "receiveVstMidiEvent", (char *) ptr ) == 0 ||
                 ::strcmp ( "sendVstMidiEvent", (char *) ptr ) == 0 ||
                 ::strcmp ( "sendVstTimeInfo", (char *) ptr ) == 0 ||
                 ::strcmp ( "midiProgramNames", (char *) ptr ) == 0 ||
                 ::strcmp ( "sizeWindow", (char *) ptr ) == 0 )
            {
                ret = 1; // can do.
            }
            break;

        case audioMasterGetLanguage:
            DMESSAGE ( "audioMasterGetLanguage" );
            ret = (VstIntPtr) kVstLangEnglish;
            break;

#if 0 // !VST_FORCE_DEPRECATED
        case audioMasterPinConnected:
            DMESSAGE ( "audioMasterPinConnected" );
            break;

            // VST 2.0 opcodes...
        case audioMasterWantMidi:
            DMESSAGE ( "audioMasterWantMidi" );
            break;

        case audioMasterSetTime:
            DMESSAGE ( "audioMasterSetTime" );
            break;

        case audioMasterTempoAt:
            DMESSAGE ( "audioMasterTempoAt" );
            //if (pSession)
            //	ret = (VstIntPtr) (pSession->tempo() * 10000.0f);
            break;

        case audioMasterGetNumAutomatableParameters:
            DMESSAGE ( "audioMasterGetNumAutomatableParameters" );
            break;

        case audioMasterGetParameterQuantization:
            DMESSAGE ( "audioMasterGetParameterQuantization" );
            ret = 1; // full single float precision
            break;

        case audioMasterNeedIdle:
            DMESSAGE ( "audioMasterNeedIdle" );
            break;

        case audioMasterGetPreviousPlug:
            DMESSAGE ( "audioMasterGetPreviousPlug" );
            break;

        case audioMasterGetNextPlug:
            DMESSAGE ( "audioMasterGetNextPlug" );
            break;

        case audioMasterWillReplaceOrAccumulate:
            DMESSAGE ( "audioMasterWillReplaceOrAccumulate" );
            ret = 1;
            break;

        case audioMasterSetOutputSampleRate:
            DMESSAGE ( "audioMasterSetOutputSampleRate" );
            break;

        case audioMasterSetIcon:
            DMESSAGE ( "audioMasterSetIcon" );
            break;

        case audioMasterOpenWindow:
            DMESSAGE ( "audioMasterOpenWindow" );
            break;

        case audioMasterCloseWindow:
            DMESSAGE ( "audioMasterCloseWindow" );
            break;
#endif

        case audioMasterGetDirectory:
            DMESSAGE ( "audioMasterGetDirectory" );
            break;

        case audioMasterUpdateDisplay:
            DMESSAGE ( "audioMasterUpdateDisplay" );
            pVst2Plugin = VST2_Plugin::findPlugin ( effect );
            if ( pVst2Plugin )
            {
                pVst2Plugin->updateParamValues ( false );
                //	QApplication::processEvents();
                ret = 1; // supported.
            }
            break;

        case audioMasterBeginEdit:
            DMESSAGE ( "audioMasterBeginEdit" );
            break;

        case audioMasterEndEdit:
            DMESSAGE ( "audioMasterEndEdit" );
            break;

        default:
            DMESSAGE ( "audioMasterUnknown" );
            break;
    }

    return ret;
}

void
VST2_Plugin::create_audio_ports( )
{
    _plugin_ins = 0;
    _plugin_outs = 0;
    for ( int32_t i = 0; i < _iAudioIns; ++i )
    {
        add_port ( Port ( this, Port::INPUT, Port::AUDIO, "input" ) );
        audio_input[i].hints.plug_port_index = i;
        _plugin_ins++;
    }

    for ( int32_t i = 0; i < _iAudioOuts; ++i )
    {
        add_port ( Port ( this, Port::OUTPUT, Port::AUDIO, "output" ) );
        audio_output[i].hints.plug_port_index = i;
        _plugin_outs++;
    }

    _audio_in_buffers = new float * [_plugin_ins]( );
    _audio_out_buffers = new float * [_plugin_outs]( );

    MESSAGE ( "Plugin has %i inputs and %i outputs", _plugin_ins, _plugin_outs );
}

void
VST2_Plugin::create_midi_ports( )
{
    for ( int32_t i = 0; i < _iMidiIns; ++i )
    {
        add_port ( Port ( this, Port::INPUT, Port::MIDI, "midi_in" ) );
    }

    for ( int32_t i = 0; i < _iMidiOuts; ++i )
    {
        add_port ( Port ( this, Port::OUTPUT, Port::MIDI, "midi_out" ) );
    }

    MESSAGE ( "Plugin has %i MIDI ins and %i MIDI outs", _iMidiIns, _iMidiOuts );
}

void
VST2_Plugin::create_control_ports( )
{
    for ( unsigned long iIndex = 0; iIndex < _iControlIns; ++iIndex )
    {
        Port::Direction d = Port::INPUT;

        char szName[64];
        szName[0] = (char) 0;
        vst2_dispatch ( effGetParamName, iIndex, 0, (void *) szName, 0.0f );

        if ( !szName[0] )
            ::snprintf ( szName, sizeof (szName ), "Param #%lu", iIndex + 1 );

        bool have_props = false;
        ::memset ( &_param_props, 0, sizeof (_param_props ) );
        if ( vst2_dispatch ( effGetParameterProperties, iIndex, 0, (void *) &_param_props, 0.0f ) )
        {
            ::snprintf ( szName, sizeof (szName ), "%s", _param_props.label );
            have_props = true;
        }

        Port p ( this, d, Port::CONTROL, strdup ( szName ) );

        /* Used for OSC path creation unique symbol */
        std::string osc_symbol;

        if ( have_props && ( _param_props.shortLabel[0] != 0 ) )
            osc_symbol = strdup ( _param_props.shortLabel );
        else
            osc_symbol = strdup ( szName );

        osc_symbol.erase ( std::remove ( osc_symbol.begin ( ), osc_symbol.end ( ), ' ' ), osc_symbol.end ( ) );
        osc_symbol += std::to_string ( iIndex );

        p.set_symbol ( osc_symbol.c_str ( ) );

        p.hints.ranged = true;
        p.hints.minimum = (float) 0.0;
        p.hints.maximum = (float) 1.0;

        if ( have_props )
        {
            if ( ( _param_props.flags & kVstParameterUsesIntegerMinMax ) )
            {
                p.hints.type = Port::Hints::LV2_INTEGER;
                p.hints.minimum = (float(_param_props.minInteger ) );
                p.hints.maximum = (float(_param_props.maxInteger ) );
            }
        }

        if ( have_props )
        {
            if ( ( _param_props.flags & kVstParameterIsSwitch ) )
                p.hints.type = Port::Hints::BOOLEAN;
        }

        // ATTN: Set default value as initial one...
        if ( _pEffect )
        {
            float value = (float) _pEffect->getParameter ( _pEffect, iIndex );
            if ( p.hints.type == Port::Hints::LV2_INTEGER )
            {
                value = ( p.hints.maximum - p.hints.minimum ) * value;
            }
            p.hints.default_value = value;
            DMESSAGE ( "Default value %f", value );
        }

        float *control_value = new float;

        *control_value = p.hints.default_value;

        p.connect_to ( control_value );

        p.hints.plug_port_index = iIndex;

        add_port ( p );
    }

    if ( bypassable ( ) )
    {
        Port pb ( this, Port::INPUT, Port::CONTROL, "dsp/bypass" );
        pb.hints.type = Port::Hints::BOOLEAN;
        pb.hints.ranged = true;
        pb.hints.maximum = 1.0f;
        pb.hints.minimum = 0.0f;
        pb.hints.dimensions = 1;
        pb.hints.visible = false;
        pb.hints.invisible_with_signals = true;
        pb.connect_to ( _bypass );
        add_port ( pb );
    }
}

void
VST2_Plugin::get_presets( )
{
    VST2_Preset pPresets ( this );

    if ( !pPresets.get_program_names ( _PresetList ) )
        _PresetList.clear ( );
}

void
VST2_Plugin::setProgram( int choice )
{
    vst2_dispatch ( effSetProgram, 0, choice, 0, 0.0f );
}

void
VST2_Plugin::activate( void )
{
    if ( !loaded ( ) )
        return;

    DMESSAGE ( "Activating plugin \"%s\"", label ( ) );

    if ( !bypass ( ) )
        FATAL ( "Attempt to activate already active plugin" );

    if ( chain ( ) )
        chain ( )->client ( )->lock ( );

    *_bypass = 0.0f;

    if ( !_activated )
    {
        vst2_dispatch ( effSetSampleRate, 0, 0, nullptr, float(sample_rate ( ) ) );
        vst2_dispatch ( effSetBlockSize, 0, buffer_size ( ), nullptr, 0.0f );

        vst2_dispatch ( effMainsChanged, 0, 1, nullptr, 0.0f );
        _activated = true;
    }

    if ( chain ( ) )
        chain ( )->client ( )->unlock ( );
}

void
VST2_Plugin::deactivate( void )
{
    if ( !loaded ( ) )
        return;

    DMESSAGE ( "Deactivating plugin \"%s\"", label ( ) );

    if ( chain ( ) )
        chain ( )->client ( )->lock ( );

    *_bypass = 1.0f;

    if ( _activated )
    {
        _activated = false;
        vst2_dispatch ( effMainsChanged, 0, 0, nullptr, 0.0f );
    }

    if ( chain ( ) )
        chain ( )->client ( )->unlock ( );
}

void
VST2_Plugin::add_port( const Port &p )
{
    Module::add_port ( p );

    if ( p.type ( ) == Port::MIDI && p.direction ( ) == Port::INPUT )
        midi_input.push_back ( p );
    else if ( p.type ( ) == Port::MIDI && p.direction ( ) == Port::OUTPUT )
        midi_output.push_back ( p );
}

void
VST2_Plugin::set_input_buffer( int n, void *buf )
{
    _audio_in_buffers[n] = static_cast<float*> ( buf );
}

void
VST2_Plugin::set_output_buffer( int n, void *buf )
{
    _audio_out_buffers[n] = static_cast<float*> ( buf );
}

bool
VST2_Plugin::loaded( void ) const
{
    if ( _pEffect )
        return true;

    return false;
}

void
VST2_Plugin::process_jack_transport( uint32_t nframes )
{
    // Get Jack transport position
    jack_position_t pos;
    const bool rolling =
            ( chain ( )->client ( )->transport_query ( &pos ) == JackTransportRolling );

    // If transport state is not as expected, then something has changed
    const bool has_bbt = ( pos.valid & JackPositionBBT );
    const bool xport_changed =
            ( rolling != _rolling || pos.frame != _position ||
              ( has_bbt && pos.beats_per_minute != _bpm ) );

    _fTimeInfo.flags = 0;

    if ( xport_changed )
    {
        if ( has_bbt )
        {
            const double positionBeats = static_cast<double> ( pos.frame )
                    / ( sample_rate ( ) * 60 / pos.beats_per_minute );

            const double ppqBar = static_cast<double> ( pos.beats_per_bar ) * ( pos.bar - 1 );

            _fTimeInfo.flags |= kVstTransportChanged;
            _fTimeInfo.samplePos = double(pos.frame );
            _fTimeInfo.sampleRate = sample_rate ( );

            // PPQ Pos
            _fTimeInfo.ppqPos = positionBeats;
            _fTimeInfo.flags |= kVstPpqPosValid;

            // Tempo
            _fTimeInfo.tempo = pos.beats_per_minute;
            _fTimeInfo.flags |= kVstTempoValid;

            // Bars
            _fTimeInfo.barStartPos = ppqBar;
            _fTimeInfo.flags |= kVstBarsValid;

            // Time Signature
            _fTimeInfo.timeSigNumerator = static_cast<int32_t> ( pos.beats_per_bar + 0.5f );
            _fTimeInfo.timeSigDenominator = static_cast<int32_t> ( pos.beat_type + 0.5f );
            _fTimeInfo.flags |= kVstTimeSigValid;
        }
        else
        {
            // Tempo
            _fTimeInfo.tempo = 120.0;
            _fTimeInfo.flags |= kVstTempoValid;

            // Time Signature
            _fTimeInfo.timeSigNumerator = 4;
            _fTimeInfo.timeSigDenominator = 4;
            _fTimeInfo.flags |= kVstTimeSigValid;

            // Missing info
            _fTimeInfo.ppqPos = 0.0;
            _fTimeInfo.barStartPos = 0.0;
        }
    }

    // Update transport state to expected values for next cycle
    _position = rolling ? pos.frame + nframes : pos.frame;
    _bpm = has_bbt ? pos.beats_per_minute : _bpm;
    _rolling = rolling;
}

void
VST2_Plugin::process_jack_midi_in( uint32_t nframes, unsigned int port )
{
    /* Process any MIDI events from jack */
    if ( midi_input[port].jack_port ( ) )
    {
        void *buf = midi_input[port].jack_port ( )->buffer ( nframes );

        for ( uint32_t i = 0; i < jack_midi_get_event_count ( buf ); ++i )
        {
            jack_midi_event_t ev;
            jack_midi_event_get ( &ev, buf, i );

            process_midi_in ( ev.buffer, ev.size, ev.time, port );
        }
    }
}

void
VST2_Plugin::process_midi_in( unsigned char *data, unsigned int size,
                              unsigned long offset, unsigned short /*port*/ )
{
    for ( unsigned int i = 0; i < size; ++i )
    {
        // channel status
        const int channel = ( data[i] & 0x0f ); // + 1;
        const int status = ( data[i] & 0xf0 );

        // all system common/real-time ignored
        if ( status == 0xf0 )
            continue;

        // check data size (#1)
        if ( ++i >= size )
            break;

        // channel key
        const int key = ( data[i] & 0x7f );

        // program change
        // after-touch
        if ( status == 0xc0 || status == 0xd0 )
        {
            if ( _fMidiEventCount >= kPluginMaxMidiEvents * 2 )
                continue;

            VstMidiEvent & vstMidiEvent ( _fMidiEvents[_fMidiEventCount++] );
            non_zeroStruct ( vstMidiEvent );

            vstMidiEvent.type = kVstMidiType;
            vstMidiEvent.byteSize = kVstMidiEventSize;
            vstMidiEvent.deltaFrames = static_cast<int32_t> ( offset );
            vstMidiEvent.midiData[0] = char(status | channel );
            vstMidiEvent.midiData[1] = char(key );
            continue;
        }

        // check data size (#1)
        if ( ++i >= size )
            break;

        // channel value (normalized)
        const int value = ( data[i] & 0x7f );

        // note on
        if ( status == 0x90 )
        {
            if ( _fMidiEventCount >= kPluginMaxMidiEvents * 2 )
                continue;

            VstMidiEvent & vstMidiEvent ( _fMidiEvents[_fMidiEventCount++] );
            non_zeroStruct ( vstMidiEvent );

            vstMidiEvent.type = kVstMidiType;
            vstMidiEvent.byteSize = kVstMidiEventSize;
            vstMidiEvent.midiData[0] = char(status | channel );
            vstMidiEvent.midiData[1] = char(key );
            vstMidiEvent.midiData[2] = char(value );
        }
        else
            // note off
            if ( status == 0x80 )
        {
            if ( _fMidiEventCount >= kPluginMaxMidiEvents * 2 )
                continue;

            VstMidiEvent & vstMidiEvent ( _fMidiEvents[_fMidiEventCount++] );
            non_zeroStruct ( vstMidiEvent );

            vstMidiEvent.type = kVstMidiType;
            vstMidiEvent.byteSize = kVstMidiEventSize;
            vstMidiEvent.midiData[0] = char(status | channel );
            vstMidiEvent.midiData[1] = char(key );
            vstMidiEvent.midiData[2] = char(value );
        }
        else
            // Control Change
            if ( status == 0xb0 )
        {
            if ( _fMidiEventCount >= kPluginMaxMidiEvents * 2 )
                continue;

            VstMidiEvent & vstMidiEvent ( _fMidiEvents[_fMidiEventCount++] );
            non_zeroStruct ( vstMidiEvent );

            vstMidiEvent.type = kVstMidiType;
            vstMidiEvent.byteSize = kVstMidiEventSize;
            vstMidiEvent.deltaFrames = static_cast<int32_t> ( offset );
            vstMidiEvent.midiData[0] = char(status | channel );
            vstMidiEvent.midiData[1] = char(key );
            vstMidiEvent.midiData[2] = char(value );
        }
    }
}

void
VST2_Plugin::process_jack_midi_out( uint32_t nframes, unsigned int port )
{
    void* buf = NULL;

    if ( midi_output[port].jack_port ( ) )
    {
        buf = midi_output[port].jack_port ( )->buffer ( nframes );
        jack_midi_clear_buffer ( buf );

        // reverse lookup MIDI events
        for ( uint32_t k = ( kPluginMaxMidiEvents * 2 ) - 1; k >= _fMidiEventCount; --k )
        {
            if ( _fMidiEvents[k].type == 0 )
                break;

            const VstMidiEvent & vstMidiEvent ( _fMidiEvents[k] );

            CARLA_SAFE_ASSERT_CONTINUE ( vstMidiEvent.deltaFrames >= 0 );
            CARLA_SAFE_ASSERT_CONTINUE ( vstMidiEvent.midiData[0] != 0 );

            uint8_t midiData[3];
            midiData[0] = static_cast<uint8_t> ( vstMidiEvent.midiData[0] );
            midiData[1] = static_cast<uint8_t> ( vstMidiEvent.midiData[1] );
            midiData[2] = static_cast<uint8_t> ( vstMidiEvent.midiData[2] );

            int ret = jack_midi_event_write ( buf, static_cast<uint32_t> ( vstMidiEvent.deltaFrames ),
                    static_cast<jack_midi_data_t*> ( midiData ), 3 );

            if ( ret )
                WARNING ( "Jack MIDI event on error = %d", ret );

            break;
        }
    }
}

int
VST2_Plugin::ProcessEvents( void *ptr )
{
    if ( _fMidiEventCount >= kPluginMaxMidiEvents * 2 - 1 )
        return 0;

    if ( const VstEvents * const vstEvents = (const VstEvents*) ptr )
    {
        for ( int32_t i = 0; i < vstEvents->numEvents && i < kPluginMaxMidiEvents * 2; ++i )
        {
            if ( vstEvents->events[i] == nullptr )
                break;

            const VstMidiEvent * const vstMidiEvent ( (const VstMidiEvent*) vstEvents->events[i] );

            if ( vstMidiEvent->type != kVstMidiType )
                continue;

            // reverse-find first free event, and put it there
            for ( uint32_t j = ( kPluginMaxMidiEvents * 2 ) - 1; j >= _fMidiEventCount; --j )
            {
                if ( _fMidiEvents[j].type == 0 )
                {
                    std::memcpy ( &_fMidiEvents[j], vstMidiEvent, sizeof (VstMidiEvent ) );
                    break;
                }
            }
        }
    }
    return 1;
}

void
VST2_Plugin::save_VST2_plugin_state( const std::string &filename )
{
    VST2_Preset pSetSave ( this );
    pSetSave.save ( filename );
}

void
VST2_Plugin::restore_VST2_plugin_state( const std::string &filename )
{
    VST2_Preset pGetLoad ( this );
    pGetLoad.load ( filename );
}

void
VST2_Plugin::get( Log_Entry &e ) const
{
    e.add ( ":vst2_plugin_path", _plugin_filename.c_str ( ) );
    e.add ( ":vst2_plugin_id", _iUniqueID );

    /* these help us display the module on systems which are missing this plugin */
    e.add ( ":plugin_ins", _plugin_ins );
    e.add ( ":plugin_outs", _plugin_outs );

    if ( _use_custom_data )
    {
        Module *m = ( Module * ) this;
        VST2_Plugin *pm = static_cast<VST2_Plugin *> ( m );

        /* Export directory location */
        if ( !export_import_strip.empty ( ) )
        {
            std::size_t found = export_import_strip.find_last_of ( "/\\" );
            std::string path = ( export_import_strip.substr ( 0, found ) );

            std::string filename = pm->get_custom_data_location ( path );

            pm->save_VST2_plugin_state ( filename );
            DMESSAGE ( "Export location = %s", filename.c_str ( ) );

            std::string base_file = filename.substr ( filename.find_last_of ( "/\\" ) + 1 );
            e.add ( ":custom_data", base_file.c_str ( ) );
        }
        else
        {
            /* If we already have pm->_project_file, it means that we have an existing project
               already loaded. So use that file instead of making a new one */
            std::string file = pm->_project_file;
            if ( file.empty ( ) )
            {
                /* This is a new project */
                file = pm->get_custom_data_location ( project_directory );
            }
            if ( !file.empty ( ) )
            {
                /* This is an existing project */
                pm->_project_file = file;
                pm->save_VST2_plugin_state ( file );

                std::string base_file = file.substr ( file.find_last_of ( "/\\" ) + 1 );
                e.add ( ":custom_data", base_file.c_str ( ) );
            }
        }
    }

    Module::get ( e );
}

void
VST2_Plugin::set( Log_Entry &e )
{
    int n = 0;
    std::string restore = "";

    /* we need to have number() defined before we create the control inputs in load() */
    for ( int i = 0; i < e.size ( ); ++i )
    {
        const char *s, *v;

        e.get ( i, &s, &v );

        if ( !strcmp ( s, ":number" ) )
        {
            n = atoi ( v );
        }
    }

    /* need to call this to set label even for version 0 modules */
    number ( n );

    std::string s_vst2_path = "";
    unsigned long ul_vst2_id = 0;

    for ( int i = 0; i < e.size ( ); ++i )
    {
        const char *s, *v;

        e.get ( i, &s, &v );

        if ( !strcmp ( s, ":vst2_plugin_path" ) )
        {
            s_vst2_path = v;
        }
        else if ( !strcmp ( s, ":vst2_plugin_id" ) )
        {
            ul_vst2_id = atoi ( v );
        }
        else if ( !strcmp ( s, ":plugin_ins" ) )
        {
            _plugin_ins = atoi ( v );
        }
        else if ( !strcmp ( s, ":plugin_outs" ) )
        {
            _plugin_outs = atoi ( v );
        }
        else if ( !strcmp ( s, ":custom_data" ) )
        {
            if ( !export_import_strip.empty ( ) )
            {
                std::string path = export_import_strip;

                std::size_t found = path.find_last_of ( "/\\" );
                restore = ( path.substr ( 0, found ) );
                restore += "/";
                restore += v;
            }
            else
            {
                restore = project_directory;
                restore += "/";
                restore += v;
                _project_file = restore;
            }
        }
    }

    DMESSAGE ( "Path = %s: ID = %ul", s_vst2_path.c_str ( ), ul_vst2_id );

    Module::Picked picked = { Type_CLAP, "", ul_vst2_id, s_vst2_path };

    if ( !load_plugin ( picked ) )
    {
        fl_alert ( "Could not load VST(2) plugin %s", s_vst2_path.c_str ( ) );
        return;
    }

    Module::set ( e );

    if ( !restore.empty ( ) )
    {
        restore_VST2_plugin_state ( restore );
    }
}

#endif  // VST2_SUPPORT