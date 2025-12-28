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

#pragma once
#include <clap/factory/preset-discovery.h>
#include <filesystem>
#include <vector>
#include "PresetMetadataReceiver.h"

class PresetIndexer {
public:
    PresetIndexer();

    const clap_preset_discovery_indexer_t* indexer() const;
    void crawl(const clap_preset_discovery_provider_t* provider);

    const std::vector<Preset>& presets() const;

private:
    static bool declare_filetype(
        const clap_preset_discovery_indexer_t*,
        const clap_preset_discovery_filetype_t*);

    static bool declare_location(
        const clap_preset_discovery_indexer_t*,
        const clap_preset_discovery_location_t*);

private:
    clap_preset_discovery_indexer_t indexer_{};

    PresetMetadataReceiver receiver_;
    std::vector<std::string> extensions_;
    std::vector<std::filesystem::path> locations_;
};
