/*
 * Carla CLAP utils
 * Copyright (C) 2022 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the doc/GPL.txt file.
 */
#pragma once

#ifdef CLAP_SUPPORT
// --------------------------------------------------------------------------------------------------------------------

struct clap_istream_impl : clap_istream_t
{
    const void* buffer;
    const uint64_t size;
    uint64_t readPos;

    clap_istream_impl(const void* const buf, const uint64_t bufsize) noexcept
        : buffer(buf),
          size(bufsize),
          readPos(0)
    {
        ctx = this;
        read = read_impl;
    }
    /**
     *  Host provided function used by clap_plugin_state->load.
     *  This gets called by the plugin to restore a plugin state from the host.
     *  The buffer and size are sent and and the host is supposed to copy to
     *  the buffer maximum of size bytes. On each call, the host adjusts the
     *  read position for the copied bytes so that the next call from the plugin will
     *  get the next size bytes. The plugin will loop call until all the host
     *  buffer has been read. The plugin will check on each call, the validity
     *  and stop reading if any errors are encountered.
     */
    static int64_t read_impl(const clap_istream_t* const stream, void* const buffer, const uint64_t size) noexcept
    {
        clap_istream_impl* const self = static_cast<clap_istream_impl*>(stream->ctx);

        if (const uint64_t bytesRead = std::min(self->size - self->readPos, size))
        {
            std::memcpy(buffer, static_cast<const uint8_t*>(self->buffer) + self->readPos, bytesRead);
            self->readPos += bytesRead;
            return bytesRead;
        }

        return 0;
    }
};

// --------------------------------------------------------------------------------------------------------------------

struct clap_ostream_impl : clap_ostream_t
{
    void* buffer;
    uint64_t size;

    clap_ostream_impl() noexcept
        : buffer(nullptr),
          size(0)
    {
        ctx = this;
        write = write_impl;
    }
    /**
     *  Host provided function called by clap_plugin_state->save.
     *  The plugin will call this function which copies from the plugin provided
     *  buffer and size bytes to the host buffer (self->buffer). The plugin will
     *  loop over and write until completed. The host buffer is appended (realloc)
     *  on each cycle. The total size of bytes copied is returned upon completion.
     */
    static int64_t  write_impl(const clap_ostream* const stream, const void* const buffer, const uint64_t size) noexcept
    {
        NON_SAFE_ASSERT_RETURN(size != 0, 0);

        clap_ostream_impl* const self = static_cast<clap_ostream_impl*>(stream->ctx);

        void* const oldBuffer = self->buffer;
        self->buffer = std::realloc(self->buffer, self->size + size);   // To append data

        if (self->buffer == nullptr)
        {
            std::free(oldBuffer);
            return -1;
        }

        std::memcpy(static_cast<uint8_t*>(self->buffer) + self->size, buffer, size);
        self->size += size;
        return size;
    }
};

#endif // CLAP_SUPPORT
