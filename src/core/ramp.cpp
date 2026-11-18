// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/ramp.hpp>
#include <modal/core/transforms.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>
#include <utility>

namespace modal::core
{

namespace
{

constexpr std::array<std::pair<ControllerRamp::Shape, std::string_view>, 4> kShapes{{
    {ControllerRamp::Shape::Linear, "linear"},
    {ControllerRamp::Shape::Curve, "curve"},
    {ControllerRamp::Shape::SCurve, "scurve"},
    {ControllerRamp::Shape::EqualPower, "equalpower"},
}};

EventData dataOf(const Lane& lane, int value)
{
    switch (lane.kind)
    {
    case Lane::Kind::PitchBend:
        return PitchBend{static_cast<std::int16_t>(value)};
    case Lane::Kind::Pressure:
        return ChannelPressure{static_cast<std::uint8_t>(value)};
    case Lane::Kind::Controller:
    case Lane::Kind::Velocity:
        break;
    }
    return ControlChange{lane.controller, static_cast<std::uint8_t>(value)};
}

} // namespace

bool rampable(const Lane& lane)
{
    switch (lane.kind)
    {
    case Lane::Kind::Controller:
        return isThinnable(lane.controller);
    case Lane::Kind::PitchBend:
    case Lane::Kind::Pressure:
        return true;
    case Lane::Kind::Velocity:
        break;
    }
    return false;
}

bool ControllerRamp::valid() const
{
    const auto inLane = [this](int value)
    { return value >= lane.lowest() && value <= lane.highest(); };
    return rampable(lane) && start >= 0 && length > 0 && inLane(from) && inLane(to) &&
           amount >= -kMaxAmount && amount <= kMaxAmount && density >= 1 && density <= kMaxDensity;
}

bool ControllerRamp::owns(const Event& event) const
{
    return event.tick >= start && event.tick <= end() && lane.shows(event.data);
}

bool ControllerRamp::collides(const ControllerRamp& other) const
{
    const bool sameStream =
        lane.kind == other.lane.kind &&
        (lane.kind != Lane::Kind::Controller || lane.controller == other.lane.controller);
    return sameStream && !(end() < other.start || other.end() < start);
}

double ControllerRamp::valueAt(Tick tick) const
{
    const double x =
        std::clamp(static_cast<double>(tick - start) / static_cast<double>(length), 0.0, 1.0);
    double f = x;
    switch (shape)
    {
    case Shape::Linear:
        break;
    case Shape::Curve:
    {
        // Up to the fourth power either way.
        const double power = 1.0 + 3.0 * std::abs(amount) / kMaxAmount;
        f = amount >= 0 ? std::pow(x, power) : 1.0 - std::pow(1.0 - x, power);
        break;
    }
    case Shape::SCurve:
        f = (1.0 - std::cos(std::numbers::pi * x)) / 2.0;
        break;
    case Shape::EqualPower:
        // Rising as a fade in's gain, falling as a fade out's.
        f = to >= from ? std::sin(x * std::numbers::pi / 2.0)
                       : 1.0 - std::cos(x * std::numbers::pi / 2.0);
        break;
    }
    return from + (to - from) * f;
}

std::string_view toString(ControllerRamp::Shape shape)
{
    for (const auto& [each, name] : kShapes)
    {
        if (each == shape)
        {
            return name;
        }
    }
    return "linear";
}

std::optional<ControllerRamp::Shape> parseRampShape(std::string_view text)
{
    for (const auto& [shape, name] : kShapes)
    {
        if (text == name)
        {
            return shape;
        }
    }
    return std::nullopt;
}

std::vector<Event> rampEvents(const ControllerRamp& ramp, Ppqn ppqn)
{
    std::vector<Event> events;
    const auto add = [&](Tick tick, int value, bool always)
    {
        if (!events.empty() && events.back().tick == tick)
        {
            // Steps finer than a tick, as at a low PPQN.
            if (always)
            {
                events.back().data = dataOf(ramp.lane, value);
            }
            return;
        }
        if (!always && !events.empty() && ramp.lane.value(events.back().data) == value)
        {
            return;
        }
        events.push_back({tick, dataOf(ramp.lane, value)});
    };
    const auto rounded = [&](Tick tick)
    {
        return std::clamp(static_cast<int>(std::lround(ramp.valueAt(tick))), ramp.lane.lowest(),
                          ramp.lane.highest());
    };
    const Tick quarter = ppqn.quarter();
    for (std::int64_t k = 0;; ++k)
    {
        const Tick tick = ramp.start + k * quarter / ramp.density;
        if (tick >= ramp.end())
        {
            break;
        }
        add(tick, rounded(tick), k == 0);
    }
    add(ramp.end(), ramp.to, true);
    return events;
}

bool ownedByRamp(const Event& event, std::span<const ControllerRamp> ramps)
{
    return std::ranges::any_of(ramps, [&](const ControllerRamp& ramp) { return ramp.owns(event); });
}

EventList withRamps(const EventList& source, std::span<const ControllerRamp> ramps, Ppqn ppqn)
{
    EventList events;
    for (const auto& event : source.all())
    {
        if (!ownedByRamp(event, ramps))
        {
            events.insert(event);
        }
    }
    for (const auto& ramp : ramps)
    {
        for (auto& event : rampEvents(ramp, ppqn))
        {
            events.insert(std::move(event));
        }
    }
    return events;
}

std::optional<std::size_t> changedRampPoints(const EventList& before, const EventList& after,
                                             std::span<const ControllerRamp> ramps)
{
    for (std::size_t i = 0; i < ramps.size(); ++i)
    {
        const auto owned = [&](const Event& event) { return ramps[i].owns(event); };
        if (!std::ranges::equal(before.all() | std::views::filter(owned),
                                after.all() | std::views::filter(owned)))
        {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace modal::core
