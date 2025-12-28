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
#include "PresetModel.h"
#include <vector>
#include <string>

class PresetMetadataReceiver {
public:
    PresetMetadataReceiver();

    const clap_preset_discovery_metadata_receiver_t* receiver() const;
    const std::vector<Preset>& presets() const;

    void setCurrentLocation(const std::string& location);

private:
    static void on_error(
        const clap_preset_discovery_metadata_receiver_t*,
        int32_t,
        const char*);

    static bool begin_preset(
        const clap_preset_discovery_metadata_receiver_t*,
        const char*,
        const char*);

    static void add_feature(
        const clap_preset_discovery_metadata_receiver_t*,
        const char*);

    static void add_creator(
        const clap_preset_discovery_metadata_receiver_t*,
        const char*);

private:
    clap_preset_discovery_metadata_receiver_t receiver_{};

    std::vector<Preset> presets_;
    Preset* current_ = nullptr;
    std::string current_location_;
};
