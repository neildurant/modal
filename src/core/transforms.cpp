// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/transforms.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace modal::core
{

namespace
{

struct Entry
{
    Event event;
    bool changed = false;
};

Tick endOf(const Event& event)
{
    return event.tick + std::max<Tick>(std::get<Note>(event.data).duration, 1);
}

// The events with those taken replaced by their changed selves, in the same order; refused
// where notes of a pitch would overlap and one of them was changed. With trim, a note run into
// the next of its pitch ends where that starts.
Transformed finish(const EventList& events, std::span<const std::size_t> which,
                   std::span<const Event> changed, bool trim)
{
    std::vector<Entry> all;
    all.reserve(events.size());
    std::size_t taken = 0;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (taken < which.size() && which[taken] == i)
        {
            all.push_back({changed[taken], changed[taken] != events[i]});
            ++taken;
        }
        else
        {
            all.push_back({events[i], false});
        }
    }

    std::vector<std::size_t> notes;
    for (std::size_t k = 0; k < all.size(); ++k)
    {
        if (all[k].event.isNote())
        {
            notes.push_back(k);
        }
    }
    const auto pitch = [&](std::size_t k) { return std::get<Note>(all[k].event.data).pitch; };
    std::ranges::stable_sort(notes,
                             [&](std::size_t a, std::size_t b)
                             {
                                 return std::pair{pitch(a), all[a].event.tick} <
                                        std::pair{pitch(b), all[b].event.tick};
                             });
    for (std::size_t n = 1; n < notes.size(); ++n)
    {
        auto& a = all[notes[n - 1]];
        auto& b = all[notes[n]];
        if (pitch(notes[n - 1]) != pitch(notes[n]) || !(a.changed || b.changed) ||
            endOf(a.event) <= b.event.tick)
        {
            continue;
        }
        if (trim && a.event.tick < b.event.tick)
        {
            std::get<Note>(a.event.data).duration = b.event.tick - a.event.tick;
            a.changed = true;
            continue;
        }
        const auto& [mine, theirs] = b.changed ? std::pair{b, a} : std::pair{a, b};
        return Refusal{Refusal::Reason::Overlap, mine.event, theirs.event};
    }

    std::ranges::stable_sort(all, {}, [](const Entry& e) { return e.event.tick; });
    EventList result;
    for (auto& entry : all)
    {
        result.insert(std::move(entry.event));
    }
    return result;
}

std::vector<Event> takenEvents(const EventList& events, std::span<const std::size_t> which)
{
    std::vector<Event> taken;
    taken.reserve(which.size());
    for (const auto i : which)
    {
        taken.push_back(events[i]);
    }
    return taken;
}

std::uint8_t clampedVelocity(std::int64_t velocity)
{
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(velocity, 1, 127));
}

} // namespace

Transformed transpose(const EventList& events, std::span<const std::size_t> which, int semitones)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const int to = note->pitch + semitones;
        if (to > 127 || to < 0)
        {
            return Refusal{to > 127 ? Refusal::Reason::AboveHighest : Refusal::Reason::BelowLowest,
                           event, std::nullopt};
        }
        note->pitch = static_cast<std::uint8_t>(to);
    }
    return finish(events, which, changed, false);
}

Transformed shift(const EventList& events, std::span<const std::size_t> which, Tick by, Tick end,
                  bool notesOnly)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        if (notesOnly && !event.isNote())
        {
            continue;
        }
        const auto was = event;
        event.tick += by;
        if (event.tick < 0 || event.tick >= end)
        {
            return Refusal{event.tick < 0 ? Refusal::Reason::BeforeStart : Refusal::Reason::PastEnd,
                           was, std::nullopt};
        }
    }
    return finish(events, which, changed, true);
}

bool isPedal(const Event& event)
{
    const auto* change = std::get_if<ControlChange>(&event.data);
    return change != nullptr &&
           (change->controller == 64 || change->controller == 66 || change->controller == 67);
}

bool hasPedals(const EventList& events, std::span<const std::size_t> which)
{
    return std::ranges::any_of(which, [&](std::size_t i) { return isPedal(events[i]); });
}

Transformed quantize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& nearest, bool pedals)
{
    auto changed = takenEvents(events, which);
    // Each note's start, and how far it moved, in tick order.
    std::vector<std::pair<Tick, Tick>> moves;
    for (auto& event : changed)
    {
        if (event.isNote())
        {
            const auto to = nearest(event.tick);
            moves.emplace_back(event.tick, to - event.tick);
            event.tick = to;
        }
    }
    std::ranges::sort(moves);
    if (pedals)
    {
        for (auto& event : changed)
        {
            if (!isPedal(event))
            {
                continue;
            }
            // The latest note at or before it.
            const auto after =
                std::ranges::upper_bound(moves, event.tick, {}, &std::pair<Tick, Tick>::first);
            if (after != moves.begin())
            {
                event.tick = std::max<Tick>(event.tick + std::prev(after)->second, 0);
            }
        }
        // Each pedal's events, the track's whole, keep their order.
        std::array<std::optional<Event>, 3> last; // as each would be
        std::size_t taken = 0;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            auto event = events[i];
            if (taken < which.size() && which[taken] == i)
            {
                event = changed[taken++];
            }
            if (!isPedal(event))
            {
                continue;
            }
            const auto controller = std::get<ControlChange>(event.data).controller;
            auto& before = last[controller == 64 ? 0 : controller == 66 ? 1 : 2];
            if (before && event.tick < before->tick)
            {
                const bool moved = event.tick != events[i].tick;
                return Refusal{Refusal::Reason::Order, moved ? event : *before,
                               moved ? *before : event};
            }
            before = event;
        }
    }
    return finish(events, which, changed, true);
}

Tick nearestGridLine(const MeterMap& meter, Tick tick, Tick step)
{
    step = std::max<Tick>(step, 1);
    const auto bar = meter.toBarBeatTick(tick).bar;
    const auto start = meter.barStart(bar);
    const auto next = meter.barStart(bar + 1);
    const auto before = start + (tick - start) / step * step;
    const auto after = std::min(before + step, next);
    return tick - before >= after - tick ? after : before;
}

std::optional<VelocityChange> parseVelocityChange(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.remove_suffix(1);
    }
    auto kind = VelocityChange::Kind::Set;
    int sign = 1;
    if (text.ends_with('%'))
    {
        kind = VelocityChange::Kind::Percent;
        text.remove_suffix(1);
    }
    else if (text.starts_with('+') || text.starts_with('-'))
    {
        kind = VelocityChange::Kind::Add;
        sign = text.front() == '-' ? -1 : 1;
        text.remove_prefix(1);
    }
    int amount = 0;
    const auto* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, amount);
    if (text.empty() || error != std::errc{} || end != last)
    {
        return std::nullopt;
    }
    const bool fits = kind == VelocityChange::Kind::Set   ? amount >= 1 && amount <= 127
                      : kind == VelocityChange::Kind::Add ? amount <= 126
                                                          : amount <= 1000;
    if (!fits)
    {
        return std::nullopt;
    }
    return VelocityChange{kind, sign * amount};
}

std::uint8_t changedVelocity(std::uint8_t velocity, VelocityChange change)
{
    switch (change.kind)
    {
    case VelocityChange::Kind::Set:
        return clampedVelocity(change.amount);
    case VelocityChange::Kind::Add:
        return clampedVelocity(std::int64_t{velocity} + change.amount);
    case VelocityChange::Kind::Percent:
        return clampedVelocity((std::int64_t{velocity} * change.amount + 50) / 100);
    }
    return velocity;
}

Transformed changeVelocity(const EventList& events, std::span<const std::size_t> which,
                           VelocityChange change)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        if (auto* note = std::get_if<Note>(&event.data))
        {
            note->velocity = changedVelocity(note->velocity, change);
        }
    }
    return finish(events, which, changed, false);
}

Transformed humanize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& maxShift, int maxVelocity, Tick end,
                     const std::function<std::int64_t(std::int64_t, std::int64_t)>& random)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const auto most = std::max<Tick>(maxShift(event.tick), 0);
        event.tick =
            std::clamp<Tick>(event.tick + random(-most, most), 0, std::max<Tick>(end - 1, 0));
        note->velocity =
            clampedVelocity(std::int64_t{note->velocity} + random(-maxVelocity, maxVelocity));
    }
    return finish(events, which, changed, true);
}

EventList cutAt(const EventList& events, Tick tick, const std::function<bool(const Note&)>& which)
{
    EventList cut;
    for (const auto& event : events.all())
    {
        const auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr || event.tick >= tick || event.tick + note->duration <= tick ||
            !which(*note))
        {
            cut.insert(event);
            continue;
        }
        auto before = *note;
        before.duration = tick - event.tick;
        before.releaseVelocity = Note{}.releaseVelocity;
        auto after = *note;
        after.duration = event.tick + note->duration - tick;
        cut.insert({event.tick, before});
        cut.insert({tick, after});
    }
    return cut;
}

} // namespace modal::core
