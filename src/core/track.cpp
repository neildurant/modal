// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/track.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace modal::core
{

bool Lane::shows(const EventData& data) const
{
    switch (kind)
    {
    case Kind::Controller:
    {
        const auto* change = std::get_if<ControlChange>(&data);
        return change != nullptr && change->controller == controller;
    }
    case Kind::PitchBend:
        return std::holds_alternative<PitchBend>(data);
    case Kind::Pressure:
        return std::holds_alternative<ChannelPressure>(data);
    case Kind::Velocity:
        return std::holds_alternative<Note>(data);
    }
    return false;
}

int Lane::value(const EventData& data) const
{
    if (const auto* change = std::get_if<ControlChange>(&data))
        return change->value;
    if (const auto* bend = std::get_if<PitchBend>(&data))
        return bend->value;
    if (const auto* pressure = std::get_if<ChannelPressure>(&data))
        return pressure->value;
    if (const auto* note = std::get_if<Note>(&data))
        return note->velocity;
    return 0;
}

void Lane::setValue(EventData& data, int value) const
{
    if (auto* change = std::get_if<ControlChange>(&data))
        change->value = static_cast<std::uint8_t>(std::clamp(value, 0, 127));
    else if (auto* bend = std::get_if<PitchBend>(&data))
        bend->value =
            static_cast<std::int16_t>(std::clamp<int>(value, PitchBend::kMin, PitchBend::kMax));
    else if (auto* pressure = std::get_if<ChannelPressure>(&data))
        pressure->value = static_cast<std::uint8_t>(std::clamp(value, 0, 127));
    else if (auto* note = std::get_if<Note>(&data))
        note->velocity = static_cast<std::uint8_t>(std::clamp(value, 1, 127));
}

std::string toString(const Lane& lane)
{
    switch (lane.kind)
    {
    case Lane::Kind::Controller:
        return "cc" + std::to_string(lane.controller);
    case Lane::Kind::PitchBend:
        return "bend";
    case Lane::Kind::Pressure:
        return "pressure";
    case Lane::Kind::Velocity:
        break;
    }
    return "velocity";
}

std::optional<Lane> parseLane(std::string_view text)
{
    std::string lower;
    for (const char c : text)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "velocity" || lower == "vel")
        return Lane{};
    if (lower == "bend" || lower == "pitchbend")
        return Lane{.kind = Lane::Kind::PitchBend};
    if (lower == "pressure" || lower == "aftertouch" || lower == "pres")
        return Lane{.kind = Lane::Kind::Pressure};
    std::string_view number = lower;
    if (number.starts_with("cc"))
        number.remove_prefix(2);
    if (number.empty() || number.size() > 3 ||
        !std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    const int controller = std::stoi(std::string{number});
    if (controller > 127)
        return std::nullopt;
    return Lane{.kind = Lane::Kind::Controller,
                .controller = static_cast<std::uint8_t>(controller)};
}

namespace
{

Tick endOf(const Event& event)
{
    return event.tick + std::get<Note>(event.data).duration;
}

void setEnd(Event& event, Tick end)
{
    std::get<Note>(event.data).duration = end - event.tick;
}

} // namespace

void resolveOverlaps(std::vector<Event>& old, std::vector<Event>& added)
{
    std::map<std::uint8_t, std::vector<std::size_t>> oldByPitch;
    for (std::size_t i = 0; i < old.size(); ++i)
    {
        if (const auto* note = std::get_if<Note>(&old[i].data))
        {
            oldByPitch[note->pitch].push_back(i);
        }
    }
    std::vector<bool> removed(old.size(), false);
    for (auto& event : added)
    {
        const auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const auto found = oldByPitch.find(note->pitch);
        if (found == oldByPitch.end())
        {
            continue;
        }
        for (const auto i : found->second) // in tick order
        {
            auto& other = old[i];
            if (removed[i])
            {
                continue;
            }
            if (other.tick == event.tick)
            {
                removed[i] = true;
            }
            else if (other.tick < event.tick && event.tick < endOf(other))
            {
                setEnd(other, event.tick);
            }
            else if (event.tick < other.tick && other.tick < endOf(event))
            {
                setEnd(event, other.tick);
            }
        }
    }
    std::vector<Event> kept;
    for (std::size_t i = 0; i < old.size(); ++i)
    {
        if (!removed[i])
        {
            kept.push_back(std::move(old[i]));
        }
    }
    old = std::move(kept);
}

EventList merged(const EventList& old, std::vector<Event> added)
{
    std::vector<Event> kept{old.all().begin(), old.all().end()};
    resolveOverlaps(kept, added);
    // Each at its tick after what's there: the old events first, then those added.
    EventList result;
    for (auto& event : kept)
    {
        result.insert(std::move(event));
    }
    std::ranges::stable_sort(added, {}, &Event::tick);
    for (auto& event : added)
    {
        result.insert(std::move(event));
    }
    return result;
}

std::span<const Event> EventList::startingIn(Tick from, Tick to) const
{
    const auto first = std::ranges::lower_bound(events_, from, {}, &Event::tick);
    const auto last = std::ranges::lower_bound(first, events_.end(), to, {}, &Event::tick);
    return {first, last};
}

std::size_t EventList::insert(Event event)
{
    assert(isValid(event));
    const auto tick = event.tick;
    auto position = std::ranges::upper_bound(events_, tick, {}, &Event::tick);
    if (!event.isNote())
    {
        // Back past the notes at this tick.
        while (position != events_.begin() && std::prev(position)->tick == tick &&
               std::prev(position)->isNote())
        {
            --position;
        }
    }
    const auto index = static_cast<std::size_t>(std::distance(events_.begin(), position));
    events_.insert(position, std::move(event));
    return index;
}

void EventList::erase(std::size_t index)
{
    assert(index < events_.size());
    events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(index));
}

Track::Track(TrackId id)
    : id_(id)
{
}

namespace
{
constexpr std::array<std::pair<std::string_view, QuantizeMove>, 4> kMoves{{
    {"starts", QuantizeMove::Starts},
    {"ends", QuantizeMove::Ends},
    {"both", QuantizeMove::Both},
    {"lengths", QuantizeMove::Lengths},
}};
} // namespace

std::optional<QuantizeMove> quantizeMove(std::string_view name)
{
    const auto found =
        std::ranges::find(kMoves, name, &std::pair<std::string_view, QuantizeMove>::first);
    return found == kMoves.end() ? std::nullopt : std::optional{found->second};
}

std::string_view quantizeMoveName(QuantizeMove move)
{
    return std::ranges::find(kMoves, move, &std::pair<std::string_view, QuantizeMove>::second)
        ->first;
}

bool Track::hasValidSettings(Ppqn ppqn) const
{
    return channel >= kMinChannel && channel <= kMaxChannel &&
           std::abs(transpose) <= kMaxTranspose && std::abs(velocityOffset) <= kMaxVelocityOffset &&
           timeOffset.valid(ppqn) && quantize.valid();
}

} // namespace modal::core
