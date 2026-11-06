// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace modal::ui
{

// Each view's state, as an undo step keeps it to go back to (named after a code review,
// November 2026): stored in an EditPlace tagged with the view's kind, and read back only by
// the view it's for.

// The arrangement's: the track at the top, the time at the left, the zoom, and the section
// the cursor is on if it's on the strip.
struct ArrangeState
{
    std::size_t topTrack = 0;
    core::Tick leftTick = 0;
    std::size_t zoom = 0;
    std::optional<std::size_t> strip = std::nullopt;

    friend bool operator==(const ArrangeState&, const ArrangeState&) = default;
};

// The piano roll's: the cursor's pitch, the pitch at the top, the page size's index, and the
// page's start and its length, if it's other than the page size's.
struct PianoRollState
{
    std::uint8_t pitch = 60;
    std::uint8_t topPitch = 72;
    std::size_t pageSize = 0;
    core::Tick viewStart = 0;
    std::optional<core::Tick> viewLength = std::nullopt;

    friend bool operator==(const PianoRollState&, const PianoRollState&) = default;
};

// The event list's: the top row, and how far down the rows at the cursor's time it is.
struct EventListState
{
    std::size_t top = 0;
    std::size_t down = 0;

    friend bool operator==(const EventListState&, const EventListState&) = default;
};

struct TracksState
{
    std::size_t top = 0;
    std::size_t column = 1;
    int left = 0;
    std::optional<std::uint8_t> pitch;
};

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const TracksState& state);
std::optional<TracksState> tracksStateOf(const core::EditPlace& place);

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const ArrangeState& state);
core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const PianoRollState& state);
core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const EventListState& state);

// The state an undo step kept, if it's that view's.
std::optional<ArrangeState> arrangeStateOf(const core::EditPlace& place);
std::optional<PianoRollState> pianoRollStateOf(const core::EditPlace& place);
std::optional<EventListState> eventListStateOf(const core::EditPlace& place);

} // namespace modal::ui
