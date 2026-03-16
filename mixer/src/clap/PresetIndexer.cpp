/*******************************************************************************/
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

#ifdef CLAP_SUPPORT

#include "PresetIndexer.h"
#include <system_error>
#include <iostream>

namespace fs = std::filesystem;

PresetIndexer::PresetIndexer() {
    indexer_ = {
        CLAP_VERSION,
        "MinimalPresetHost",
        "ExampleVendor",
        "https://example.com",
        "1.0",
        this,
        declare_filetype,
        declare_location,
        declare_soundpack,
        get_extension
    };
}

const clap_preset_discovery_indexer_t*
PresetIndexer::indexer() const {
    return &indexer_;
}

const std::vector<Preset>& PresetIndexer::presets() const {
    return receiver_.presets();
}

/* ---------- callbacks ---------- */

bool PresetIndexer::declare_filetype(
    const clap_preset_discovery_indexer_t* i,
    const clap_preset_discovery_filetype_t* ft) {

    auto* self =
        static_cast<PresetIndexer*>(i->indexer_data);

    if (ft && ft->file_extension && *ft->file_extension)
        self->extensions_.emplace_back(ft->file_extension);

    return true;
}

bool PresetIndexer::declare_location(
    const clap_preset_discovery_indexer_t* i,
    const clap_preset_discovery_location_t* loc) {

    auto* self =
        static_cast<PresetIndexer*>(i->indexer_data);

    if (loc &&
        loc->kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE &&
        loc->location &&
        *loc->location) {
        self->locations_.emplace_back(loc->location);
    }
    return true;
}

bool PresetIndexer::declare_soundpack(
    const clap_preset_discovery_indexer_t*,
    const clap_preset_discovery_soundpack_t*) {

    // Host does not currently index soundpack metadata.
    // Accept and ignore.
    return true;
}

const void* PresetIndexer::get_extension(
    const clap_preset_discovery_indexer_t*,
    const char*) {

    // No indexer extensions supported by this host yet.
    return nullptr;
}

bool PresetIndexer::has_supported_extension(const fs::path& p) const {
    if (extensions_.empty())
        return true;

    std::string ext = p.extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext.erase(0, 1);

    for (const auto& e : extensions_) {
        if (ext == e)
            return true;
    }

    return false;
}

/* ---------- crawl ---------- */

void PresetIndexer::crawl(
    const clap_preset_discovery_provider_t* provider) {

    if (!provider || !provider->get_metadata)
        return;

    for (const auto& root : locations_) {
        std::error_code ec;

        if (!fs::exists(root, ec) || ec)
            continue;

        fs::recursive_directory_iterator it(
            root,
            fs::directory_options::skip_permission_denied,
            ec);
        fs::recursive_directory_iterator end;

        if (ec)
            continue;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || file_ec)
                continue;

            if (!has_supported_extension(it->path()))
                continue;

            receiver_.setCurrentLocation(it->path().string());

            provider->get_metadata(
                provider,
                CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                it->path().string().c_str(),
                receiver_.receiver());
        }
    }
}

#endif  // CLAP_SUPPORT
