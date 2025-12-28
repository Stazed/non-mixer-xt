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

#include "PresetMetadataReceiver.h"
#include <iostream>

static void noop_add_plugin_id(
    const clap_preset_discovery_metadata_receiver*,
    const clap_universal_plugin_id_t*) {}

static void noop_set_soundpack_id(
    const clap_preset_discovery_metadata_receiver*,
    const char*) {}

static void noop_set_flags(
    const clap_preset_discovery_metadata_receiver*,
    uint32_t) {}

static void noop_set_description(
    const clap_preset_discovery_metadata_receiver*,
    const char*) {}

static void noop_set_timestamps(
    const clap_preset_discovery_metadata_receiver*,
    clap_timestamp,
    clap_timestamp) {}

static void noop_add_extra_info(
    const clap_preset_discovery_metadata_receiver*,
    const char*,
    const char*) {}

PresetMetadataReceiver::PresetMetadataReceiver() {
    receiver_ = {
        this,
        on_error,
        begin_preset,
        noop_add_plugin_id,
        noop_set_soundpack_id,
        noop_set_flags,
        add_creator,
        noop_set_description,
        noop_set_timestamps,
        add_feature,
        noop_add_extra_info
    };
}

const clap_preset_discovery_metadata_receiver_t*
PresetMetadataReceiver::receiver() const {
    return &receiver_;
}

const std::vector<Preset>& PresetMetadataReceiver::presets() const {
    return presets_;
}

void PresetMetadataReceiver::setCurrentLocation(
    const std::string& location) {
    current_location_ = location;
}

/* ---------- callbacks ---------- */

void PresetMetadataReceiver::on_error(
    const clap_preset_discovery_metadata_receiver_t*,
    int32_t,
    const char* msg) {

    std::cerr << "Metadata error: "
              << (msg ? msg : "(unknown)") << "\n";
}

bool PresetMetadataReceiver::begin_preset(
    const clap_preset_discovery_metadata_receiver_t* r,
    const char* name,
    const char* load_key) {

    auto* self =
        static_cast<PresetMetadataReceiver*>(r->receiver_data);

    self->presets_.emplace_back();
    self->current_ = &self->presets_.back();

    if (name)     self->current_->name = name;
    if (load_key) self->current_->load_key = load_key;

    self->current_->location = self->current_location_;
    return true;
}

void PresetMetadataReceiver::add_feature(
    const clap_preset_discovery_metadata_receiver_t* r,
    const char* feature) {

    auto* self =
        static_cast<PresetMetadataReceiver*>(r->receiver_data);

    if (self->current_ && feature)
        self->current_->features.emplace_back(feature);
}

void PresetMetadataReceiver::add_creator(
    const clap_preset_discovery_metadata_receiver_t* r,
    const char* creator) {

    auto* self =
        static_cast<PresetMetadataReceiver*>(r->receiver_data);

    if (self->current_ && creator)
        self->current_->creators.emplace_back(creator);
}
