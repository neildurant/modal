// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace modal::core
{

// The source chosen at a section-relative tick. Zero denotes the part's ordinary events.
std::uint32_t activeSource(const TrackContent& part, Tick at);

std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at);

// Projects the sources into one event list for the editor and playback.
EventList playingEvents(const TrackContent& part);

// Applies an edit of the whole projected part to the active sources at each event's tick.
void replacePlayingEvents(TrackContent& part, EventList events);

// Chooses a source for a range, replacing earlier choices inside it.
void assignTake(TrackContent& part, TakeRange range);

// Selects a take over the span it recorded.
bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

// Keeps one take and preserves the audible material from the others as ordinary events.
std::size_t keepOnlyTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

// Discards a source and reveals the material underneath it.
struct Discarded
{
    bool done = false;
    std::optional<std::uint32_t> instead;
};
Discarded discardTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

} // namespace modal::core
