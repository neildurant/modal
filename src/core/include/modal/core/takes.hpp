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

// Choosing takes (decided 25 November 2026): the part that plays is ordinary events, each
// noting the take it came from, and choosing a take puts its events in place of what starts in
// its span, as recording it again would.

// The take playing in a part, by its id: of the takes some event of the part notes, the last
// recorded whose span holds the tick (from the section's start), or failing that the last
// recorded. None if no event notes a take.
std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at);

// Puts a take of a track's part of a section in place of everything starting in its span, its
// events noting it; an earlier note running into the span is cut where it starts, as a
// recording's would be. Returns false, changing nothing, if there's no such take.
bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

} // namespace modal::core
