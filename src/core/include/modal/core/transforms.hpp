// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace modal::core
{

// The operators' changes to a track's events (decided for milestone 6). Each takes the track's
// events and the indices of those a range takes, in order, and gives the events after the
// change, or why it can't be made: the whole change is refused rather than made in part.
//
// Notes of a pitch never come to overlap: two at the same start are refused; and a note run
// into the next of its pitch is refused when pitches move, but when times move (shift,
// quantise, humanise) ends where the next starts, as notes played legato and quantised need.

struct Refusal
{
    enum class Reason : std::uint8_t
    {
        AboveHighest, // a note would go above G9 (127)
        BelowLowest,  // or below C-1 (0)
        BeforeStart,  // an event would go before the song's start
        PastEnd,      // or at or after its end
        Overlap,      // a note would overlap another of its pitch
        Order,        // a pedal would move past the next of its pedal
    };

    Reason reason;
    Event event;                // the event, as it would be
    std::optional<Event> other; // what it would overlap or move past, as that would be
};

using Transformed = std::variant<EventList, Refusal>;

// Notes up or down by semitones; other events stay as they are.
Transformed transpose(const EventList& events, std::span<const std::size_t> which, int semitones);

// Events later or earlier by some ticks, only the notes if notesOnly is set; none at or after
// the end.
Transformed shift(const EventList& events, std::span<const std::size_t> which, Tick by, Tick end,
                  bool notesOnly);

// The pedals that follow quantised notes: sustain, sostenuto and soft (CC 64, 66 and 67).
bool isPedal(const Event& event);

// Whether any of the events taken is a pedal.
bool hasPedals(const EventList& events, std::span<const std::size_t> which);

// Notes' starts moved to the grid line nearest, their lengths kept; and with pedals set, each
// pedal taken moved as far as the latest note at or before it moved, so it stays as far from
// the note (decided October 2026), those before any note staying put. A pedal moved past the
// next of its pedal is refused.
Transformed quantize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& nearest, bool pedals);

// The grid line nearest a tick, the lines a step apart from each bar line, which is one too.
// Halfway between two, the later.
Tick nearestGridLine(const MeterMap& meter, Tick tick, Tick step);

// A change to velocities, as typed after ^: a value (80), a change (+10, -10) or a percentage
// (80%). Velocities stay between 1 and 127.
struct VelocityChange
{
    enum class Kind : std::uint8_t
    {
        Set,
        Add,
        Percent,
    };

    Kind kind;
    int amount;

    friend bool operator==(const VelocityChange&, const VelocityChange&) = default;
};

// None if the text isn't one: a value outside 1 to 127, a change beyond 126 either way, or a
// percentage outside 0 to 1000.
std::optional<VelocityChange> parseVelocityChange(std::string_view text);

std::uint8_t changedVelocity(std::uint8_t velocity, VelocityChange change);

Transformed changeVelocity(const EventList& events, std::span<const std::size_t> which,
                           VelocityChange change);

// Small random changes to notes: each start moved by up to maxShift(its tick) either way, not
// before 0 nor at or after the end, and each velocity by up to maxVelocity either way, between
// 1 and 127. random(low, high) gives a whole number from low to high, both included.
Transformed humanize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& maxShift, int maxVelocity, Tick end,
                     const std::function<std::int64_t(std::int64_t, std::int64_t)>& random);

} // namespace modal::core
