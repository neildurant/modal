// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/track.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    BankMsb,
    BankLsb,
    Program,
    Grid,
    DefaultLength,
    DefaultVelocity,
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

// The program changes among a track's own events: how many, and when the first is.
struct ProgramEvents
{
    std::size_t count = 0;
    core::Tick first = 0;
};
ProgramEvents programEvents(const core::Song& song, core::TrackId track);

// Whether a track has one of its own program changes before a tick.
bool programBefore(const core::Song& song, core::TrackId track, core::Tick tick);

// The tracks, from the first as 0, sharing a track's port and channel with a different setup, so
// that whichever is sent last wins. A track with no setup asks for nothing, so clashes with none.
std::vector<std::size_t> programClashes(const core::Song& song, const core::Track& track);

} // namespace modal::ui
