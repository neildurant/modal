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
    Arm,  // armed to record
    Thru, // MIDI input sent through to it while armed
    Port,
    Input,
    Channel,
    Transpose,
    VelocityOffset,
    TimeOffset,
    ChaseNotes,
    BankMsb,
    BankLsb,
    Program,
    Quantize,        // the Q button
    QuantizeSummary, // the Quantise column, its settings edited in the inspector
    QuantizeGrid,
    QuantizeStrength,
    QuantizeSwing,
    QuantizeMove,
};

struct TrackColumn
{
    TrackField field;
    std::string_view heading;
    int width; // including the blank cells after it
    bool numeric = false;
    int gap = 1; // blank cells after it, within its width
};

// This order drives both drawing and navigation; field identities do not depend on it.
std::span<const TrackColumn> trackColumns();
std::string trackFieldText(TrackField field, const core::Track& track, std::size_t index);

// A button's letter, shown whether it's on or off, as the buttons have no headings.
std::string_view toggleLetter(TrackField field);

// What a field is, in a few words, for the inspector to say about the column under the cursor.
std::string_view trackFieldDescription(TrackField field);
std::optional<std::string> setTrackField(core::Track& track, TrackField field,
                                         std::string_view text, core::Ppqn ppqn);
std::optional<std::string> stepTrackField(core::Track& track, TrackField field, int steps,
                                          bool fine, core::Ppqn ppqn);

// A track's playback quantise in a few words: its grid, then anything not as a new track's.
std::string quantizeSummary(const core::Track& track);

// What to say on arming or disarming tracks: which, and on arming, any others still armed, as
// a warning, since a track left armed by mistake records the take too.
struct ArmedMessage
{
    std::string text;
    bool warning = false;
};
ArmedMessage armedMessage(const core::Song& song, std::span<const core::TrackId> changed,
                          bool armed);

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
