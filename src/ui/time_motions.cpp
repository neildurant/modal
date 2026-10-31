// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/time_motions.hpp>

#include <algorithm>
#include <iterator>

namespace modal::ui
{

core::Tick barLineAfter(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count; ++i)
    {
        tick = meter.barStart(meter.toBarBeatTick(tick).bar + 1);
    }
    return tick;
}

core::Tick barLineBefore(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count && tick > 0; ++i)
    {
        tick = meter.barStart(meter.toBarBeatTick(tick - 1).bar);
    }
    return tick;
}

core::Tick offsetInBar(const core::MeterMap& meter, core::Tick tick)
{
    return tick - meter.barStart(meter.toBarBeatTick(tick).bar);
}

core::Tick sameInBar(const core::MeterMap& meter, core::Tick tick, std::int64_t count,
                     core::Tick offset)
{
    const auto bar = std::max<std::int64_t>(meter.toBarBeatTick(tick).bar + count, 1);
    const core::Tick start = meter.barStart(bar);
    return start + std::min(offset, meter.barStart(bar + 1) - start - 1);
}

std::optional<core::Tick> nextOf(std::span<const core::Tick> ticks, core::Tick tick,
                                 std::int64_t count)
{
    const auto it = std::ranges::upper_bound(ticks, tick);
    const auto at = std::distance(ticks.begin(), it) + count - 1;
    if (at >= static_cast<std::ptrdiff_t>(ticks.size()))
    {
        return std::nullopt;
    }
    return ticks[static_cast<std::size_t>(at)];
}

std::optional<core::Tick> previousOf(std::span<const core::Tick> ticks, core::Tick tick,
                                     std::int64_t count)
{
    const auto it = std::ranges::lower_bound(ticks, tick);
    const auto at = std::distance(ticks.begin(), it) - count;
    if (at < 0)
    {
        return std::nullopt;
    }
    return ticks[static_cast<std::size_t>(at)];
}

bool together(const core::Song& song, core::Tick first, core::Tick tick,
              std::chrono::milliseconds window)
{
    const auto& tempo = song.tempo();
    return tempo.toTime(tick) - tempo.toTime(first) <= window;
}

std::vector<std::size_t> chordNotes(const core::Song& song, const core::EventList& events,
                                    std::size_t note, int count, std::chrono::milliseconds window)
{
    // Each note's chord, counting from 0; then those from the note's chord on.
    std::vector<std::size_t> taken;
    std::optional<core::Tick> first;
    std::int64_t chord = -1;
    std::optional<std::int64_t> from;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (!events[i].isNote())
        {
            continue;
        }
        if (!first || !together(song, *first, events[i].tick, window))
        {
            first = events[i].tick;
            ++chord;
        }
        if (i == note)
        {
            from = chord;
            // The chord's notes before this one.
            for (auto j = i; j > 0; --j)
            {
                if (events[j - 1].isNote() && events[j - 1].tick >= *first)
                {
                    taken.insert(taken.begin(), j - 1);
                }
            }
        }
        if (from && chord >= *from && chord < *from + std::max(count, 1))
        {
            taken.push_back(i);
        }
    }
    return taken;
}

} // namespace modal::ui
