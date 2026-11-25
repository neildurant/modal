// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/metronome.hpp>
#include <modal/ui/bar.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace modal::ui
{

// A value snapshot for presentation; no document, transport or timer is owned here.
struct StatusBarState
{
    enum class Mode
    {
        Normal,
        Visual,
        Edit,
        Move,
        Freeze
    };
    enum class TransportState
    {
        Stopped,
        Playing,
        Looping,
        PlaybackCountIn,
        PlaybackPreRoll,
        CountIn,
        PreRoll,
        Recording,
        Overdubbing
    };
    struct Transport
    {
        TransportState state = TransportState::Stopped;
        // Position/time are accented only when Player supplies a play position, independently
        // of the recording phase. Musical fields already have their song-dependent padding.
        bool positionActive = false;
        std::string position;
        std::string time;
        std::string meter;
        std::string tempo;
        std::string section;
        std::string shortSection;
        // Recording a loop, the pass being recorded, shown after REC (decided 25 November
        // 2026).
        std::optional<std::uint32_t> pass;
    };

    Mode mode = Mode::Normal;
    std::optional<Transport> transport;
    std::size_t armed = 0;
    bool overdub = false;
    bool punch = false;            // a punch range armed
    bool loop = false;             // a loop armed and on, for Space
    core::CountInSettings countIn; // before recording
    core::CountInSettings playbackLeadIn = core::kDefaultPlaybackLeadIn; // shown when not armed
    core::MetronomeSettings::When clickWhen = core::MetronomeSettings::When::Recording;
    bool clickAvailable = true;
    bool thru = true;
    bool audition = true;
    bool rewind = true;
    bool quantizeBypassed = false;
    bool inputActive = false;
    bool inputLost = false;
    bool modified = false; // unsaved changes, the file's name being on the title bar and Ctrl-g
};

// Builds the permanent bottom line, preserving fixed field widths and the agreed order
// of shortening/hiding fields. Layout into a Region remains layoutBar's responsibility.
Bar statusBar(const StatusBarState& state, int width, CellSize cell);

} // namespace modal::ui
