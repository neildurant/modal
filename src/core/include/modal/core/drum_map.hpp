// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

namespace modal::core
{

// Names attached to a track's MIDI pitches, independent of its output channel. An empty
// entry is an unnamed pitch. Value ownership keeps songs and copied tracks self-contained.
struct DrumMap
{
    std::string name;
    std::array<std::string, 128> notes{};

    friend bool operator==(const DrumMap&, const DrumMap&) = default;
};

// The existing General MIDI percussion names, empty outside the defined range 35–81.
std::string_view generalMidiDrumName(int note);
DrumMap generalMidiDrumMap();

} // namespace modal::core
