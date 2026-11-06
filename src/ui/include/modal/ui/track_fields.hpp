// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/track.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace modal::ui
{

enum class TrackField
{
    Number,
    Name,
    Mute,
    Solo,
    Port,
    Channel,
    Transpose,
    VelocityOffset,
    TimeOffset,
    ChaseNotes,
};

struct TrackColumn
{
    TrackField field;
    std::string_view heading;
    int width;
    bool numeric = false;
};

// This order drives both drawing and navigation; field identities do not depend on it.
std::span<const TrackColumn> trackColumns();
std::string trackFieldText(TrackField field, const core::Track& track, std::size_t index);
std::optional<std::string> setTrackField(core::Track& track, TrackField field,
                                         std::string_view text, core::Ppqn ppqn);
std::optional<std::string> stepTrackField(core::Track& track, TrackField field, int steps,
                                          bool fine, core::Ppqn ppqn);

} // namespace modal::ui
