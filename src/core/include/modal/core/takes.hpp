// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace modal::core
{

// The take playing at a section-relative tick, or zero where none does.
std::uint32_t activeSource(const TrackContent& part, Tick at);

// The active ranges in [from, to), including those where no take plays, in time order.
std::vector<TakeRange> activeTakeRanges(const TrackContent& part, Tick from, Tick to);

std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at);

// Whether the part has comped time, where there's a choice: two takes covering the same time, or
// a range where none is chosen. Only such parts show their takes (decided 27 November 2026); a part
// with one take looks as it did before takes.
bool hasComping(const TrackContent& part);

// Projects the sources into one event list for the editor and playback.
EventList playingEvents(const TrackContent& part);

// Gives a number for a new take, from the song's count.
using NewTakeId = std::function<std::uint32_t()>;

// Applies an edit of the whole projected part, each event going into the take playing at its
// tick (decided 27 November 2026). In a gap no take covers, the take before the gap grows over
// it, or the take after if none comes before; in a part without takes, a new take covers the
// section, `length` long; and where none is chosen, a new take covers that range, chosen there.
// A part left with one take and nothing in it is left empty.
void replacePlayingEvents(TrackContent& part, EventList events, Tick length,
                          const NewTakeId& newId);

// Chooses a source for a range, replacing earlier choices inside it.
void assignTake(TrackContent& part, TakeRange range);

// Selects a take over the span it recorded.
bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

// Keeps one take, moving what's heard from the others into it, over the whole section. An
// interim form, until Restore take and Bake in place replace it.
std::size_t keepOnlyTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

// Discards a take, revealing what it covered: other takes, or silence.
struct Discarded
{
    bool done = false;
    std::optional<std::uint32_t> instead;
};
Discarded discardTake(Song& song, std::size_t section, TrackId track, std::uint32_t take);

} // namespace modal::core
