// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pedalling.hpp>

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <vector>

namespace modal::core
{

namespace
{

constexpr std::array<int, 3> kPedals{64, 66, 67};

const ControlChange* pedalOf(const Event& event, int controller)
{
    const auto* change = std::get_if<ControlChange>(&event.data);
    return change != nullptr && change->controller == controller ? change : nullptr;
}

bool isPedalEvent(const Event& event)
{
    const auto* change = std::get_if<ControlChange>(&event.data);
    return change != nullptr && std::ranges::find(kPedals, change->controller) != kPedals.end();
}

Event pedal(Tick tick, int controller, bool down)
{
    return {tick, ControlChange{static_cast<std::uint8_t>(controller),
                                static_cast<std::uint8_t>(down ? 127 : 0)}};
}

EventList listOf(const std::vector<Event>& events)
{
    EventList list;
    for (const auto& event : events)
    {
        list.insert(event);
    }
    return list;
}

// A pedal's value just before a tick, by its last event before it.
std::optional<std::uint8_t> valueBefore(const EventList& events, int controller, Tick tick)
{
    std::optional<std::uint8_t> value;
    for (const auto& event : events.all())
    {
        if (event.tick >= tick)
        {
            break;
        }
        if (const auto* change = pedalOf(event, controller))
        {
            value = change->value;
        }
    }
    return value;
}

// The events but a pedal's in the stretch, and that pedal's state kept at its edges: lifted at
// its start if down there, and set again at its end to its value there, if down, a half
// pedal's too (found in a code review, November 2026).
std::vector<Event> withoutPedal(const EventList& events, Tick from, Tick to, int controller)
{
    std::vector<Event> kept;
    for (const auto& event : events.all())
    {
        if (pedalOf(event, controller) == nullptr || event.tick < from || event.tick >= to)
        {
            kept.push_back(event);
        }
    }
    if (pedalDown(events, controller, from))
    {
        kept.push_back(pedal(from, controller, false));
    }
    if (pedalDown(events, controller, to) &&
        std::ranges::none_of(events.all(), [&](const Event& event)
                             { return event.tick == to && pedalOf(event, controller); }))
    {
        kept.push_back({to, ControlChange{static_cast<std::uint8_t>(controller),
                                          valueBefore(events, controller, to).value_or(127)}});
    }
    return kept;
}

} // namespace

bool pedalDown(const EventList& events, int controller, Tick tick)
{
    std::optional<int> value;
    for (const auto& event : events.all())
    {
        if (event.tick >= tick)
        {
            break;
        }
        if (const auto* change = pedalOf(event, controller))
        {
            value = change->value;
        }
    }
    return value.value_or(0) >= 64;
}

Transformed repedal(const EventList& events, Tick from, Tick to, std::span<const Tick> changes,
                    Tick end, const PedalTiming& timing)
{
    // Each press with the lift after it, before the next change or at the end; a press that
    // wouldn't come before its lift, after a short last note or between changes closer than
    // the lift and press, left out, so the pedal is always lifted again (found in a code
    // review, November 2026).
    auto kept = withoutPedal(events, from, to, kSustain);
    for (std::size_t i = 0; i < changes.size(); ++i)
    {
        const auto press = std::max(timing.press(changes[i]), from);
        const auto lift =
            std::max(i + 1 < changes.size() ? timing.lift(changes[i + 1]) : end, from);
        if (press < lift)
        {
            kept.push_back(pedal(press, kSustain, true));
            kept.push_back(pedal(lift, kSustain, false));
        }
    }
    std::ranges::stable_sort(kept, {}, &Event::tick);
    return listOf(kept);
}

Transformed retimePedal(const EventList& events, Tick from, Tick to, std::span<const Tick> changes,
                        const PedalTiming& timing, const std::function<Tick(Tick)>& near)
{
    std::vector<Event> all{events.all().begin(), events.all().end()};
    for (auto& event : all)
    {
        const auto* change = pedalOf(event, kSustain);
        if (change == nullptr || event.tick < from || event.tick >= to)
        {
            continue;
        }
        const bool down = change->value >= 64;
        std::optional<Tick> best;
        for (const auto at : changes)
        {
            const auto where = down ? timing.press(at) : timing.lift(at);
            if (std::abs(where - event.tick) <= near(event.tick) &&
                (!best || std::abs(where - event.tick) < std::abs(*best - event.tick)))
            {
                best = where;
            }
        }
        if (best)
        {
            event.tick = std::clamp<Tick>(*best, from, std::max(from, to - 1));
        }
    }
    // A lift and a press changing places would leave the pedal down where it was up, or the
    // reverse: refused (found in a code review, November 2026).
    std::vector<std::size_t> order(all.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return all[i].tick; });
    std::optional<std::size_t> last; // the sustain's last event, by index, in its new order
    for (const auto i : order)
    {
        if (pedalOf(all[i], kSustain) == nullptr)
        {
            continue;
        }
        if (last && *last > i)
        {
            return Refusal{Refusal::Reason::Order, all[i], all[*last]};
        }
        last = i;
    }
    std::ranges::stable_sort(all, {}, &Event::tick);
    return listOf(all);
}

Transformed cleanPedals(const EventList& events, Tick from, Tick to,
                        const std::function<Tick(Tick)>& flicker)
{
    std::vector<bool> dropped(events.size(), false);
    for (const auto controller : kPedals)
    {
        std::vector<std::size_t> those;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (pedalOf(events[i], controller) != nullptr && events[i].tick >= from &&
                events[i].tick < to)
            {
                those.push_back(i);
            }
        }
        const auto value = [&](std::size_t n)
        { return pedalOf(events[those[n]], controller)->value; };
        // Flicker first: a lift and a press soon after, neither at the stretch's ends.
        for (std::size_t n = 1; n + 2 < those.size(); ++n)
        {
            // Only where the pedal was down before the lift, so a lift repeated and then a press
            // isn't taken for one (found in a code review, November 2026).
            if (!dropped[those[n]] && value(n - 1) >= 64 && value(n) < 64 && value(n + 1) >= 64 &&
                events[those[n + 1]].tick - events[those[n]].tick <= flicker(events[those[n]].tick))
            {
                dropped[those[n]] = true;
                dropped[those[n + 1]] = true;
            }
        }
        // Then exact repeats of what's left.
        std::optional<int> last;
        for (std::size_t n = 0; n < those.size(); ++n)
        {
            if (dropped[those[n]])
            {
                continue;
            }
            if (last && *last == value(n) && n + 1 < those.size())
            {
                dropped[those[n]] = true;
                continue;
            }
            last = value(n);
        }
    }
    std::vector<Event> kept;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (!dropped[i])
        {
            kept.push_back(events[i]);
        }
    }
    return listOf(kept);
}

Transformed clearPedals(const EventList& events, Tick from, Tick to,
                        std::span<const int> controllers)
{
    auto list = events;
    for (const auto controller : controllers)
    {
        list = listOf(withoutPedal(list, from, to, controller));
    }
    return list;
}

Transformed offsetPedals(const EventList& events, Tick from, Tick to,
                         const std::function<Tick(Tick)>& by, Tick songEnd)
{
    std::vector<Event> all{events.all().begin(), events.all().end()};
    for (auto& event : all)
    {
        if (!isPedalEvent(event) || event.tick < from || event.tick >= to)
        {
            continue;
        }
        const auto was = event;
        event.tick += by(event.tick);
        if (event.tick < 0 || event.tick >= songEnd)
        {
            return Refusal{event.tick < 0 ? Refusal::Reason::BeforeStart : Refusal::Reason::PastEnd,
                           was, std::nullopt};
        }
    }
    // Each pedal's changes keep their order.
    for (const auto controller : kPedals)
    {
        std::optional<std::pair<Event, Event>> before; // as it is, as it was
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            if (pedalOf(all[i], controller) == nullptr)
            {
                continue;
            }
            if (before && all[i].tick < before->first.tick)
            {
                return Refusal{Refusal::Reason::Order, before->first, all[i]};
            }
            before = std::pair{all[i], events[i]};
        }
    }
    std::ranges::stable_sort(all, {}, &Event::tick);
    return listOf(all);
}

Transformed bakePedal(const EventList& events, Tick from, Tick to)
{
    // Every note the sustain holds into the stretch, those starting before it too, held to
    // where the pedal lifts after it, or to the stretch's end if it doesn't, so clearing the
    // sustain from the stretch releases nothing early (found in a code review, November 2026).
    std::vector<Event> all{events.all().begin(), events.all().end()};
    const auto nextLift = [&](Tick after) -> std::optional<Tick>
    {
        for (const auto& event : events.all())
        {
            if (const auto* change = pedalOf(event, kSustain);
                change != nullptr && change->value < 64 && event.tick > after)
            {
                return event.tick;
            }
        }
        return std::nullopt;
    };
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        auto* note = std::get_if<Note>(&all[i].data);
        if (note == nullptr || all[i].tick >= to)
        {
            continue;
        }
        const auto end = all[i].tick + note->duration;
        if (!pedalDown(events, kSustain, end + 1))
        {
            continue;
        }
        const auto held = nextLift(end).value_or(to);
        if (held <= from || held <= end)
        {
            continue; // released before the stretch, which baking leaves alone
        }
        // Not past the next note of its pitch.
        std::optional<Tick> again;
        for (std::size_t j = 0; j < all.size(); ++j)
        {
            if (const auto* other = std::get_if<Note>(&all[j].data);
                other != nullptr && j != i && other->pitch == note->pitch &&
                all[j].tick > all[i].tick && (!again || all[j].tick < *again))
            {
                again = all[j].tick;
            }
        }
        note->duration = std::min(held, again.value_or(held)) - all[i].tick;
    }
    return listOf(withoutPedal(listOf(all), from, to, kSustain));
}

} // namespace modal::core
