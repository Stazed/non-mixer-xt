/*******************************************************************************/
/* Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.    */
/* Copyright (C) 2024- Stazed                                                  */
/*                                                                             */
/* This file is part of Non-Mixer-XT                                           */
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
 * File:   VST3PluginHost.C
 * Author: sspresto
 *
 * Created on December 29, 2023, 1:36 PM
 */

#ifdef VST3_SUPPORT

#include "VST3PluginHost.H"
#include "../../../nonlib/debug.h"
#include "VST3_Plugin.H"
#undef WARNING      // Fix redefinition with /nonlib/debug.h"

#define WARNING( fmt, args... ) warnf( W_WARNING, __MODULE__, __FILE__, __FUNCTION__, __LINE__, fmt, ## args )

std::string
utf16_to_utf8( const std::u16string& utf16 )
{
    std::wstring_convert < std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string utf8 = convert.to_bytes ( utf16 );
    return utf8;
}

// Constructor.

VST3PluginHost::VST3PluginHost( VST3_Plugin * plug )
{
    FUNKNOWN_CTOR

    m_plugin = plug;
    m_plugInterfaceSupport = owned ( NEW PlugInterfaceSupport ( ) );

    m_pTimer = new Timer ( m_plugin );

    m_timerRefCount = 0;

    m_processRefCount = 0;
}


// Destructor.

VST3PluginHost::~VST3PluginHost( void )
{
    clear ( );

    delete m_pTimer;

    m_plugInterfaceSupport = nullptr;

    FUNKNOWN_DTOR
}

//--- IHostApplication ---
//

tresult PLUGIN_API
VST3PluginHost::getName( Vst::String128 name )
{
    const std::string str ( "VST3PluginHost" );
    const int nsize = str.length ( ) < 127 ? str.length ( ) : 127;

    ::memcpy ( name, str.c_str ( ), nsize - 1 );
    name[nsize] = 0;
    return kResultOk;
}

tresult PLUGIN_API
VST3PluginHost::createInstance(
    TUID cid, TUID _iid, void **obj )
{
    const FUID classID ( FUID::fromTUID ( cid ) );
    const FUID interfaceID ( FUID::fromTUID ( _iid ) );

    if ( classID == Vst::IMessage::iid &&
            interfaceID == Vst::IMessage::iid )
    {
        *obj = new Message ( );
        return kResultOk;
    }
    else if ( classID == Vst::IAttributeList::iid &&
              interfaceID == Vst::IAttributeList::iid )
    {
        *obj = new AttributeList ( );
        return kResultOk;
    }

    *obj = nullptr;
    return kResultFalse;
}

tresult PLUGIN_API
VST3PluginHost::queryInterface(
    const char *_iid, void **obj )
{
    QUERY_INTERFACE ( _iid, obj, FUnknown::iid, IHostApplication )
    QUERY_INTERFACE ( _iid, obj, IHostApplication::iid, IHostApplication )

    if ( m_plugInterfaceSupport &&
            m_plugInterfaceSupport->queryInterface ( _iid, obj ) == kResultOk )
    {
        return kResultOk;
    }

    *obj = nullptr;
    return kResultFalse;
}

uint32 PLUGIN_API
VST3PluginHost::addRef( void )
{
    return 1;
}

uint32 PLUGIN_API
VST3PluginHost::release( void )
{
    return 1;
}


// Timer stuff...
//

void
VST3PluginHost::startTimer( int msecs )
{
    if ( ++m_timerRefCount == 1 ) m_pTimer->start ( msecs );
}

void
VST3PluginHost::stopTimer( void )
{
    if ( m_timerRefCount > 0 && --m_timerRefCount == 0 ) m_pTimer->stop ( );
}

int
VST3PluginHost::timerInterval( void ) const
{
    return m_pTimer->interval ( );
}

// Common host time-keeper context accessor.

Vst::ProcessContext *
VST3PluginHost::processContext( void )
{
    return &m_processContext;
}

void
VST3PluginHost::processAddRef( void )
{
    ++m_processRefCount;
}

void
VST3PluginHost::processReleaseRef( void )
{
    if ( m_processRefCount > 0 ) --m_processRefCount;
}

// Common host time-keeper process context.

void
VST3PluginHost::updateProcessContext(
    jack_position_t &pos, const bool &xport_changed, const bool &has_bbt )
{
    if ( m_processRefCount < 1 )
        return;

    if ( xport_changed )
        m_processContext.state |= Vst::ProcessContext::kPlaying;
    else
        m_processContext.state &= ~Vst::ProcessContext::kPlaying;

    if ( has_bbt )
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;

        m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid;
        m_processContext.projectTimeMusic = pos.beat;
        m_processContext.state |= Vst::ProcessContext::kBarPositionValid;
        m_processContext.barPositionMusic = pos.bar;

        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo = pos.beats_per_minute;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = pos.beats_per_bar;
        m_processContext.timeSigDenominator = pos.beat_type;
    }
    else
    {
        m_processContext.sampleRate = pos.frame_rate;
        m_processContext.projectTimeSamples = pos.frame;
        m_processContext.state |= Vst::ProcessContext::kTempoValid;
        m_processContext.tempo = 120.0;
        m_processContext.state |= Vst::ProcessContext::kTimeSigValid;
        m_processContext.timeSigNumerator = 4;
        m_processContext.timeSigDenominator = 4;
    }
}

// Cleanup.

void
VST3PluginHost::clear( void )
{
    m_timerRefCount = 0;
    m_processRefCount = 0;

    ::memset ( &m_processContext, 0, sizeof (Vst::ProcessContext ) );
}

IMPLEMENT_FUNKNOWN_METHODS( VST3PluginHost::AttributeList, IAttributeList, IAttributeList::iid )

IMPLEMENT_FUNKNOWN_METHODS( VST3PluginHost::Message, IMessage, IMessage::iid )

#endif // VST3_SUPPORT