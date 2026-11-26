// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/takes.hpp>

#include <algorithm>
#include <utility>

namespace modal::core
{

std::uint32_t activeSource(const TrackContent& part, Tick at)
{
    auto choice = std::ranges::upper_bound(part.comp, at, {}, &TakeRange::from);
    if (choice != part.comp.begin() && at < std::prev(choice)->to)
    {
        return std::prev(choice)->take;
    }
    for (auto take = part.takes.rbegin(); take != part.takes.rend(); ++take)
    {
        if (at >= take->from && at < take->to)
        {
            return take->id;
        }
    }
    return 0;
}

std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at)
{
    const auto source = activeSource(part, at);
    return source == 0 ? std::nullopt : std::optional{source};
}

EventList playingEvents(const TrackContent& part)
{
    if (part.takes.empty())
    {
        return part.events;
    }
    EventList playing;
    const auto append = [&](const EventList& events, std::uint32_t source)
    {
        for (const auto& event : events.all())
        {
            if (activeSource(part, event.tick) == source)
            {
                playing.insert(event);
            }
        }
    };
    append(part.events, 0);
    for (const auto& take : part.takes)
    {
        append(take.events, take.id);
    }
    return playing;
}

void replacePlayingEvents(TrackContent& part, EventList events)
{
    if (part.takes.empty())
    {
        part.events = std::move(events);
        return;
    }

    const auto hide = [&](EventList& source, std::uint32_t id)
    {
        EventList hidden;
        for (const auto& event : source.all())
        {
            if (activeSource(part, event.tick) != id)
            {
                hidden.insert(event);
            }
        }
        source = std::move(hidden);
    };
    hide(part.events, 0);
    for (auto& take : part.takes)
    {
        hide(take.events, take.id);
    }
    for (auto event : events.all())
    {
        const auto id = activeSource(part, event.tick);
        const auto at = std::ranges::find(part.takes, id, &Take::id);
        auto& source = at == part.takes.end() ? part.events : at->events;
        source.insert(std::move(event));
    }
}

void assignTake(TrackContent& part, TakeRange range)
{
    std::vector<TakeRange> choices;
    for (auto old : part.comp)
    {
        if (old.to <= range.from || old.from >= range.to)
        {
            choices.push_back(old);
        }
        else
        {
            if (old.from < range.from)
            {
                choices.push_back({old.from, range.from, old.take});
            }
            if (old.to > range.to)
            {
                choices.push_back({range.to, old.to, old.take});
            }
        }
    }
    choices.push_back(range);
    std::ranges::sort(choices, {}, &TakeRange::from);
    part.comp.clear();
    for (const auto& choice : choices)
    {
        if (!part.comp.empty() && part.comp.back().to == choice.from &&
            part.comp.back().take == choice.take)
        {
            part.comp.back().to = choice.to;
        }
        else
        {
            part.comp.push_back(choice);
        }
    }
}

bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
    {
        return false;
    }
    const auto* part = song.sections()[section]->content(track);
    const auto* chosen = part ? part->take(take) : nullptr;
    return chosen && song.updatePart(section, track, [&](TrackContent& edited)
                                     { assignTake(edited, {chosen->from, chosen->to, take}); });
}

std::size_t keepOnlyTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
    {
        return 0;
    }
    const auto* part = song.sections()[section]->content(track);
    const auto* chosen = part ? part->take(take) : nullptr;
    if (!chosen || part->takes.size() == 1)
    {
        return 0;
    }

    const auto removed = part->takes.size() - 1;
    const bool changed = song.updatePart(
        section, track,
        [&](TrackContent& edited)
        {
            auto playing = playingEvents(edited);
            std::vector<Tick> boundaries{chosen->from, chosen->to};
            const auto addBoundaries = [&](const auto& ranges)
            {
                for (const auto& range : ranges)
                {
                    boundaries.push_back(std::clamp(range.from, chosen->from, chosen->to));
                    boundaries.push_back(std::clamp(range.to, chosen->from, chosen->to));
                }
            };
            addBoundaries(edited.takes);
            addBoundaries(edited.comp);
            std::ranges::sort(boundaries);
            const auto duplicates = std::ranges::unique(boundaries);
            boundaries.erase(duplicates.begin(), duplicates.end());

            edited.comp.clear();
            for (std::size_t i = 1; i < boundaries.size(); ++i)
            {
                if (activeSource(*part, boundaries[i - 1]) != take)
                {
                    assignTake(edited, {boundaries[i - 1], boundaries[i], 0});
                }
            }
            std::erase_if(edited.takes, [&](const Take& other) { return other.id != take; });
            edited.events = {};
            replacePlayingEvents(edited, std::move(playing));
        });
    return changed ? removed : 0;
}

Discarded discardTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
    {
        return {};
    }
    const auto* part = song.sections()[section]->content(track);
    const auto* discarded = part ? part->take(take) : nullptr;
    if (!discarded)
    {
        return {};
    }
    std::optional<std::uint32_t> replacement;
    const bool changed = song.updatePart(
        section, track,
        [&](TrackContent& edited)
        {
            std::erase_if(edited.comp, [&](const TakeRange& range) { return range.take == take; });
            std::erase_if(edited.takes, [&](const Take& other) { return other.id == take; });
            if (edited.takes.empty())
            {
                edited.comp.clear();
            }
            replacement = playingTake(edited, discarded->from);
        });
    return {changed, replacement};
}

} // namespace modal::core
