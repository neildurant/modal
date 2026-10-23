// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace modal::ui
{

// Where the time motions go from a tick, in a track's bars, a count of times (at least 1).
// None goes before tick 0.

// The bar line a count of bars on (]b), or back ([b): back from mid-bar, the start of that
// bar counts as the first.
core::Tick barLineAfter(const core::MeterMap& meter, core::Tick tick, std::int64_t count);
core::Tick barLineBefore(const core::MeterMap& meter, core::Tick tick, std::int64_t count);

// The same position a count of bars on or back (} and {, with a negative count): `offset`
// into the bar, or its last tick if the bar is shorter. Not before bar 1.
core::Tick sameInBar(const core::MeterMap& meter, core::Tick tick, std::int64_t count,
                     core::Tick offset);

// How far into its bar a tick is.
core::Tick offsetInBar(const core::MeterMap& meter, core::Tick tick);

// The next or previous of some ticks in order (section starts, markers), a count of them
// on or back: back from between two, the earlier counts as the first. None if there's none.
std::optional<core::Tick> nextOf(std::span<const core::Tick> ticks, core::Tick tick,
                                 std::int64_t count);
std::optional<core::Tick> previousOf(std::span<const core::Tick> ticks, core::Tick tick,
                                     std::int64_t count);

// Whether a tick is close enough after another to sound together with it, as a chord's notes
// recorded a little apart do: within the window, in time at the song's tempo.
bool together(const core::Song& song, core::Tick first, core::Tick tick,
              std::chrono::milliseconds window);

} // namespace modal::ui
