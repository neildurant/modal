// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/track.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <map>
#include <utility>

namespace modal::core
{

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

bool Track::hasValidSettings() const
{
    return channel >= kMinChannel && channel <= kMaxChannel &&
           std::abs(transpose) <= kMaxTranspose && std::abs(velocityOffset) <= kMaxVelocityOffset &&
           timeOffset >= -kMaxTimeOffset && timeOffset <= kMaxTimeOffset;
}

} // namespace modal::core
