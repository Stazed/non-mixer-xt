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

#include "PresetIndexer.h"
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
        nullptr,
        nullptr
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

    if (ft->file_extension && *ft->file_extension)
        self->extensions_.emplace_back(ft->file_extension);

    return true;
}

bool PresetIndexer::declare_location(
    const clap_preset_discovery_indexer_t* i,
    const clap_preset_discovery_location_t* loc) {

    auto* self =
        static_cast<PresetIndexer*>(i->indexer_data);

    if (loc->kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE &&
        loc->location) {
        self->locations_.emplace_back(loc->location);
    }
    return true;
}

/* ---------- crawl ---------- */

void PresetIndexer::crawl(
    const clap_preset_discovery_provider_t* provider) {

    for (const auto& root : locations_) {
        if (!fs::exists(root))
            continue;

        for (const auto& e :
             fs::recursive_directory_iterator(root)) {

            if (!e.is_regular_file())
                continue;

            receiver_.setCurrentLocation(e.path().string());

            provider->get_metadata(
                provider,
                CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                e.path().string().c_str(),
                receiver_.receiver());
        }
    }
}
