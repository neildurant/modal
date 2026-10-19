#include <modal/core/track.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace modal::core
{

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
