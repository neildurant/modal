// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/event_list.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace modal::ui
{

// An event changed, or why it can't be.
using EditResult = std::variant<core::Event, std::string>;

// Sets a field of a track's event from text typed into it, in the track's bars: an absolute
// value, or a change starting with + or -.
//
//   Position  3:2:120, 3:2 or 3; or a change of bars:beats:ticks or a note value, +1/16, -0:0:010
//   Data      a note's pitch as a name or number, C#4 or 61, or a change in semitones, +12; a
//             controller or program, 0-127; text as it's typed; SysEx as hex bytes
//   Value     a velocity, 1-127; a value, 0-127; a bend, -8192 to 8191; or a change, +10
//   Length    bars:beats:ticks in the meter where the note starts, 0:1:000, or a note value,
//             1/8., or a change of either
//   Release   0-127, or a change
EditResult setField(const core::Event& event, Column field, std::string_view text,
                    const core::MeterMap& meter);

// Steps a field a number of times, up or down (+ and -): a value or release by 1, a pitch by a
// semitone, a position by a beat of the meter where the event is, a length by a 1/16 note,
// each kept within its range. Text and SysEx can't be stepped.
EditResult stepField(const core::Event& event, Column field, int steps,
                     const core::MeterMap& meter);

// Why a note would overlap another of its pitch among a track's events, which the editor
// never lets happen (decided in milestone 4), naming the other; or nothing if it wouldn't.
// The note's own index, if it's already among them, is left out.
std::optional<std::string> overlap(const core::EventList& events, std::optional<std::size_t> self,
                                   const core::Event& note, const core::MeterMap& meter);

} // namespace modal::ui
