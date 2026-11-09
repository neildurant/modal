// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace modal::core
{

enum class FreezePart
{
    All,
    Transpose,
    Velocity,
    Quantize,
    TimeOffset,
};

std::optional<FreezePart> freezePart(std::string_view text);
std::string_view freezeName(FreezePart part);

struct FreezeRefusal
{
    TrackId track;
    Tick at;
    std::string reason;
};

// Prepared without changing the song, so a batch can validate every track before editing.
struct FrozenTrack
{
    Track settings;
    EventList events;
    std::size_t leftInPlace = 0;
    bool quantizeFallback = false;
};

// Why this choice cannot be made, independently of the events' values. Used by the chooser
// as well as the operation, so unavailable choices explain themselves before selection.
std::optional<std::string> freezeUnavailable(const Track& track, FreezePart part, bool bypass);
std::variant<FrozenTrack, FreezeRefusal> freezeTrack(const Song& song, TrackId id, FreezePart part,
                                                     bool bypass = false);

} // namespace modal::core
