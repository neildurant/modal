// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace modal::core
{

// An editable controller ramp (designed 17 November 2026): a gesture from one value to another
// across a span of a track, kept as its settings so it can be changed after listening, and
// played as the ordinary events it makes. While it's there it masks the events of its
// controller beneath it, from its start to its end, both included; those events are kept, and
// come back when it moves away, goes or is converted.
struct ControllerRamp
{
    enum class Shape : std::uint8_t
    {
        Linear,
        Curve,      // by an amount: positive stays near the start, then changes quickly
        SCurve,     // easing in and out
        EqualPower, // for crossfades
    };

    static constexpr int kMaxAmount = 100;
    static constexpr int kDefaultDensity = 16;
    static constexpr int kMaxDensity = 96;

    Lane lane;       // a continuous controller's, pitch bend's or channel aftertouch's
    Tick start = 0;  // where it starts, from its section's start or the song's
    Tick length = 1; // from its start to its last point, at its end
    int from = 0;
    int to = 0;
    Shape shape = Shape::Linear;
    int amount = 0;                // for a curve, from -kMaxAmount to kMaxAmount
    int density = kDefaultDensity; // points a quarter note

    Tick end() const { return start + length; }

    // Whether its settings make sense: a lane it can ramp, values in the lane's range, and a
    // length, amount and density in theirs.
    bool valid() const;

    // Whether the event is its controller's, from its start to its end.
    bool owns(const Event& event) const;

    // Whether it and another are on one controller, and touch or overlap, which isn't allowed.
    bool collides(const ControllerRamp& other) const;

    // The value the shape gives at a tick from its start to its end, unrounded.
    double valueAt(Tick tick) const;

    friend bool operator==(const ControllerRamp&, const ControllerRamp&) = default;
};

// Whether a lane's events can be ramped: a continuous controller (not a switch or an action
// such as bank select), pitch bend or channel aftertouch.
bool rampable(const Lane& lane);

// A shape as a song file writes it, linear, curve, scurve or equalpower, and read back.
std::string_view toString(ControllerRamp::Shape shape);
std::optional<ControllerRamp::Shape> parseRampShape(std::string_view text);

// The events a ramp makes, in order: one at its start, then one at each step of its density
// from its start, each value rounded to the nearest and left out if it repeats the one before,
// and its last value at its end.
std::vector<Event> rampEvents(const ControllerRamp& ramp, Ppqn ppqn);

// What plays of a track's events with its ramps: those the ramps own taken out, and theirs put
// in.
EventList withRamps(const EventList& source, std::span<const ControllerRamp> ramps, Ppqn ppqn);

// Whether one of the ramps owns the event.
bool ownedByRamp(const Event& event, std::span<const ControllerRamp> ramps);

// An ordinary event edit must keep every ramp-owned point intact, including its count;
// otherwise moving a point into a ramp could silently hide it. Returns the first ramp changed.
std::optional<std::size_t> changedRampPoints(const EventList& before, const EventList& after,
                                             std::span<const ControllerRamp> ramps);

} // namespace modal::core
