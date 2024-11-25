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

std::u16string
utf8_to_utf16( const std::string & utf8String )
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    std::u16string utf16String = converter.from_bytes( utf8String );
    return utf16String;
}

// Constructor.

VST3PluginHost::VST3PluginHost( )
{
    FUNKNOWN_CTOR

    m_plugInterfaceSupport = owned ( NEW PlugInterfaceSupport ( ) );
}

// Destructor.

VST3PluginHost::~VST3PluginHost( void )
{
    m_plugInterfaceSupport = nullptr;

    FUNKNOWN_DTOR
}

//--- IHostApplication ---
//

tresult PLUGIN_API
VST3PluginHost::getName( Vst::String128 name )
{
    const std::string host_name ( PACKAGE );
    
    const std::u16string u16name = utf8_to_utf16(host_name);
    
    const int c_size = u16name.size();

    const int nsize = ( c_size * 2 ) < 127 ? ( c_size * 2 ) : 127;

    ::memcpy ( name, u16name.c_str ( ), nsize - 1 );
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

IMPLEMENT_FUNKNOWN_METHODS( VST3PluginHost::AttributeList, IAttributeList, IAttributeList::iid )

IMPLEMENT_FUNKNOWN_METHODS( VST3PluginHost::Message, IMessage, IMessage::iid )

#endif // VST3_SUPPORT
