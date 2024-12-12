/*******************************************************************************/
/* Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.    */
/* Copyright (C) 2019-2020 Robin Gareus <robin@gareus.org>                     */
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
#include "VST3_common.H"
#undef WARNING      // Fix redefinition with /nonlib/debug.h"

#define WARNING( fmt, args... ) warnf( W_WARNING, __MODULE__, __FILE__, __FUNCTION__, __LINE__, fmt, ## args )

// Constructor.

VST3PluginHost::VST3PluginHost()
{
    FUNKNOWN_CTOR

    m_plugInterfaceSupport = owned ( NEW PlugInterfaceSupport () );
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

    const std::u16string u16name = nmxt_common::utf8_to_utf16(host_name);

    const int c_size = u16name.size();

    const int nsize = ( c_size * 2 ) < 127 ? ( c_size * 2 ) : 127;

    ::memcpy ( name, u16name.c_str (), nsize - 1 );
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
        *obj = new Message ();
        return kResultOk;
    }
    else if ( classID == Vst::IAttributeList::iid &&
        interfaceID == Vst::IAttributeList::iid )
    {
        *obj = new AttributeList ();
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

/* ****************************************************************************/

VST3PluginHost::RAMStream::RAMStream ()
    : _data (0)
    , _size (0)
    , _alloc (0)
    , _pos (0)
    , _readonly (false)
{
}

VST3PluginHost::RAMStream::RAMStream (uint8_t* data, size_t size)
    : _data (0)
    , _size (size)
    , _alloc (0)
    , _pos (0)
    , _readonly (true)
{
    if (_size > 0 && reallocate_buffer (_size, true))
    {
        memcpy (_data, data, _size);
    }
    else
    {
        _size = 0;
    }
}

VST3PluginHost::RAMStream::RAMStream (std::string const& fn)
    : _data (0)
    , _size (0)
    , _alloc (0)
    , _pos (0)
    , _readonly (true)
{
    char* buf    = NULL;
    size_t  length = 0;

    /* FIXME */
    /*if (!g_file_get_contents (fn.c_str (), &buf, &length, NULL)) {
    	return;
    }*/
    if (length > 0 && reallocate_buffer (length, true))
    {
        _size = length;
        memcpy (_data, buf, _size);
    }
    if ( buf )
        free (buf);
}

VST3PluginHost::RAMStream::~RAMStream ()
{
    free (_data);
}

tresult
VST3PluginHost::RAMStream::queryInterface (const TUID _iid, void** obj)
{
    QUERY_INTERFACE (_iid, obj, FUnknown::iid, IBStream)
    QUERY_INTERFACE (_iid, obj, IBStream::iid, IBStream)
    QUERY_INTERFACE (_iid, obj, FUnknown::iid, ISizeableStream)
    QUERY_INTERFACE (_iid, obj, ISizeableStream::iid, ISizeableStream)
    QUERY_INTERFACE (_iid, obj, FUnknown::iid, Vst::IStreamAttributes)
    QUERY_INTERFACE (_iid, obj, Vst::IStreamAttributes::iid, Vst::IStreamAttributes)

    *obj = nullptr;
    return kNoInterface;
}

bool
VST3PluginHost::RAMStream::reallocate_buffer (int64 size, bool exact)
{
    if (size <= 0)
    {
        free (_data);
        _data  = 0;
        _alloc = 0;
        return true;
    }

    if (size == _alloc)
    {
        assert (_data);
        return true;
    }

    if (!exact)
    {
        if (size <= _alloc)
        {
            /* don't shrink */
            assert (_data);
            return true;
        }
        if (size > _alloc)
        {
            size = (((size - 1) / 8192) + 1) * 8192;
        }
    }

    _data = (uint8_t*)realloc (_data, size);

    if (_data)
    {
        _alloc = size;
        return true;
    }
    else
    {
        _alloc = 0;
        return false;
    }
}

tresult
VST3PluginHost::RAMStream::read (void* buffer, int32 n_bytes, int32* n_read)
{
    assert (_pos >= 0 && _pos <= _size);
    int64 available = _size - _pos;

    if (n_bytes < 0 || available < 0)
    {
        n_bytes = 0;
    }
    else if (n_bytes > available)
    {
        n_bytes = available;
    }

    if (n_bytes > 0)
    {
        memcpy (buffer, &_data[_pos], n_bytes);
        _pos += n_bytes;
    }
    if (n_read)
    {
        *n_read = n_bytes;
    }
    return kResultTrue;
}

tresult
VST3PluginHost::RAMStream::write (void* buffer, int32 n_bytes, int32* n_written)
{
    if (n_written)
    {
        *n_written = 0;
    }
    if (_readonly)
    {
        return kResultFalse;
    }
    if (n_bytes < 0)
    {
        return kInvalidArgument;
    }

    if (!reallocate_buffer (_pos + n_bytes, false))
    {
        return kOutOfMemory;
    }

    if (buffer && _data && _pos >= 0 && n_bytes > 0)
    {
        memcpy (&_data[_pos], buffer, n_bytes);
        _pos += n_bytes;
        _size = _pos;
    }
    else
    {
        n_bytes = 0;
    }

    if (n_written)
    {
        *n_written = n_bytes;
    }
    return kResultTrue;
}

tresult
VST3PluginHost::RAMStream::seek (int64 pos, int32 mode, int64* result)
{
    switch (mode)
    {
        case kIBSeekSet:
            _pos = pos;
            break;
        case kIBSeekCur:
            _pos += pos;
            break;
        case kIBSeekEnd:
            _pos = _size + pos;
            break;
        default:
            return kInvalidArgument;
    }
    if (_pos < 0)
    {
        _pos = 0;
    }
    if (result)
    {
        *result = _pos;
    }
    return kResultTrue;
}

tresult
VST3PluginHost::RAMStream::tell (int64* pos)
{
    if (!pos)
    {
        return kInvalidArgument;
    }
    *pos = _pos;
    return kResultTrue;
}

bool
VST3PluginHost::RAMStream::write_int32 (int32 i)
{
    /* pluginterfaces/base/ftypes.h */
#if BYTEORDER == kBigEndian
    SWAP_32 (i)
#endif
    return write_pod (i);
}

bool
VST3PluginHost::RAMStream::write_int64 (int64 i)
{
    /* pluginterfaces/base/ftypes.h */
#if BYTEORDER == kBigEndian
    SWAP_64 (i)
#endif
    return write_pod (i);
}

bool
VST3PluginHost::RAMStream::write_ChunkID (const ChunkID& id)
{
    return write_pod (id);
}

#if COM_COMPATIBLE
/* pluginterfaces/base/funknown.cpp */
struct GUIDStruct
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
};
#endif

bool
VST3PluginHost::RAMStream::write_TUID (const TUID& tuid)
{
    int   i       = 0;
    int32 n_bytes = 0;
    char  buf[kClassIDSize + 1];

#if COM_COMPATIBLE
    GUIDStruct guid;
    memcpy (&guid, tuid, sizeof (GUIDStruct));
    sprintf (buf, "%08X%04X%04X", guid.data1, guid.data2, guid.data3);
    i += 8;
#endif

    for (; i < (int)sizeof (TUID); ++i)
    {
        sprintf (buf + 2 * i, "%02X", (uint8_t)tuid[i]);
    }
    write (buf, kClassIDSize, &n_bytes);
    return n_bytes == kClassIDSize;
}

bool
VST3PluginHost::RAMStream::read_int32 (int32& i)
{
    if (!read_pod (i))
    {
        return false;
    }
#if BYTEORDER == kBigEndian
    SWAP_32 (i)
#endif
    return true;
}

bool
VST3PluginHost::RAMStream::read_int64 (int64& i)
{
    if (!read_pod (i))
    {
        return false;
    }
#if BYTEORDER == kBigEndian
    SWAP_64 (i)
#endif
    return true;
}

bool
VST3PluginHost::RAMStream::read_ChunkID (ChunkID& id)
{
    return read_pod (id);
}

bool
VST3PluginHost::RAMStream::read_TUID (TUID& tuid)
{
    int   i       = 0;
    int32 n_bytes = 0;
    char  buf[kClassIDSize + 1];

    read ((void*)buf, kClassIDSize, &n_bytes);
    if (n_bytes != kClassIDSize)
    {
        return false;
    }

    buf[kClassIDSize] = '\0';

#if COM_COMPATIBLE
    GUIDStruct guid;
    sscanf (buf,      "%08x", &guid.data1);
    sscanf (buf + 8,  "%04hx", &guid.data2);
    sscanf (buf + 12, "%04hx", &guid.data3);
    memcpy (tuid, &guid, sizeof (TUID) >> 1);
    i += 16;
#endif

    for (; i < kClassIDSize; i += 2)
    {
        uint32_t temp;
        sscanf (buf + i, "%02X", &temp);
        tuid[i >> 1] = temp;
    }

    return true;
}

tresult
VST3PluginHost::RAMStream::getStreamSize (int64& size)
{
    size = _alloc;
    return kResultTrue;
}

tresult
VST3PluginHost::RAMStream::setStreamSize (int64 size)
{
    if (_readonly)
    {
        return kResultFalse;
    }
    return reallocate_buffer (size, true) ? kResultOk : kOutOfMemory;
}

tresult
VST3PluginHost::RAMStream::getFileName (Vst::String128 name)
{
    return kNotImplemented;
}

Vst::IAttributeList*
VST3PluginHost::RAMStream::getAttributes ()
{
    return &attribute_list;
}

#ifndef NDEBUG

#include <iomanip>
#include <iostream>
#include <sstream>

void
VST3PluginHost::RAMStream::hexdump (int64 max_len) const
{
    std::ostringstream out;

    size_t row_size = 16;
    size_t length   = max_len > 0 ? std::min (max_len, _size) : _size;

    out << std::setfill ('0');
    for (size_t i = 0; i < length; i += row_size)
    {
        out << "0x" << std::setw (6) << std::hex << i << ": ";
        for (size_t j = 0; j < row_size; ++j)
        {
            if (i + j < length)
            {
                out << std::hex << std::setw (2) << static_cast<int> (_data[i + j]) << " ";
            }
            else
            {
                out << "   ";
            }
        }
        out << " ";
        if (true)
        {
            for (size_t j = 0; j < row_size; ++j)
            {
                if (i + j < length)
                {
                    if (isprint (_data[i + j]))
                    {
                        out << static_cast<char> (_data[i + j]);
                    }
                    else
                    {
                        out << ".";
                    }
                }
            }
        }
        out << std::endl;
    }
    std::cout << out.str ();
}
#endif

VST3PluginHost::ROMStream::ROMStream (IBStream& src, TSize offset, TSize size)
    : _stream (src)
    , _offset (offset)
    , _size   (size)
    , _pos    (0)
{
    _stream.addRef ();
}

VST3PluginHost::ROMStream::~ROMStream ()
{
    _stream.release ();
}

tresult
VST3PluginHost::ROMStream::queryInterface (const TUID _iid, void** obj)
{
    QUERY_INTERFACE (_iid, obj, FUnknown::iid, IBStream)
    QUERY_INTERFACE (_iid, obj, IBStream::iid, IBStream)

    *obj = nullptr;
    return kNoInterface;
}

tresult
VST3PluginHost::ROMStream::read (void* buffer, int32 n_bytes, int32* n_read)
{
    int64 available = _size - _pos;

    if (n_read)
    {
        *n_read = 0;
    }

    if (n_bytes < 0 || available < 0)
    {
        n_bytes = 0;
        return kResultOk;
    }

    if (n_bytes > available)
    {
        n_bytes = available;
    }

    tresult result = _stream.seek (_offset + _pos, kIBSeekSet);
    if (result != kResultOk)
    {
        return result;
    }

    int32 _n_read = 0;
    result        = _stream.read (buffer, n_bytes, &_n_read);

    if (_n_read > 0)
    {
        _pos += _n_read;
    }
    if (n_read)
    {
        *n_read = _n_read;
    }

    return result;
}

tresult
VST3PluginHost::ROMStream::write (void* buffer, int32 n_bytes, int32* n_written)
{
    if (n_written)
    {
        *n_written = 0;
    }
    return kNotImplemented;
}

tresult
VST3PluginHost::ROMStream::seek (int64 pos, int32 mode, int64* result)
{
    switch (mode)
    {
        case kIBSeekSet:
            _pos = pos;
            break;
        case kIBSeekCur:
            _pos += pos;
            break;
        case kIBSeekEnd:
            _pos = _size + pos;
            break;
        default:
            return kInvalidArgument;
    }
    if (_pos < 0)
    {
        _pos = 0;
    }
    if (_pos > _size)
    {
        _pos = _size;
    }

    if (result)
    {
        *result = _pos;
    }
    return kResultTrue;
}

tresult
VST3PluginHost::ROMStream::tell (int64* pos)
{
    if (!pos)
    {
        return kInvalidArgument;
    }
    *pos = _pos;
    return kResultTrue;
}

#endif // VST3_SUPPORT
