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

std::vector<TakeRange> activeTakeRanges(const TrackContent& part, Tick from, Tick to)
{
    if (from >= to)
    {
        return {};
    }
    std::vector<Tick> boundaries{from, to};
    const auto addBoundaries = [&](const auto& ranges)
    {
        for (const auto& range : ranges)
        {
            if (range.from > from && range.from < to)
            {
                boundaries.push_back(range.from);
            }
            if (range.to > from && range.to < to)
            {
                boundaries.push_back(range.to);
            }
        }
    };
    addBoundaries(part.takes);
    addBoundaries(part.comp);
    std::ranges::sort(boundaries);
    const auto duplicates = std::ranges::unique(boundaries);
    boundaries.erase(duplicates.begin(), duplicates.end());

    std::vector<TakeRange> result;
    for (std::size_t i = 1; i < boundaries.size(); ++i)
    {
        const auto start = boundaries[i - 1];
        const auto source = activeSource(part, start);
        if (!result.empty() && result.back().take == source)
        {
            result.back().to = boundaries[i];
        }
        else
        {
            result.push_back({start, boundaries[i], source});
        }
    }
    return result;
}

std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at)
{
    const auto source = activeSource(part, at);
    return source == 0 ? std::nullopt : std::optional{source};
}

bool hasComping(const TrackContent& part)
{
    // A range chosen silent is a choice; one choosing a take is only where takes overlap.
    if (std::ranges::any_of(part.comp, [](const TakeRange& range) { return range.take == 0; }))
    {
        return true;
    }
    for (std::size_t i = 0; i < part.takes.size(); ++i)
    {
        for (std::size_t j = i + 1; j < part.takes.size(); ++j)
        {
            const auto& a = part.takes[i];
            const auto& b = part.takes[j];
            if (a.from < b.to && b.from < a.to)
            {
                return true;
            }
        }
    }
    return false;
}

EventList playingEvents(const TrackContent& part)
{
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
    for (const auto& take : part.takes)
    {
        append(take.events, take.id);
    }
    return playing;
}

namespace
{

// The take an event at a tick goes into, made or grown if need be.
Take& takeFor(TrackContent& part, Tick at, Tick length, const NewTakeId& newId)
{
    if (const auto id = activeSource(part, at); id != 0)
    {
        return *std::ranges::find(part.takes, id, &Take::id);
    }
    const auto added = [&](Tick from, Tick to) -> Take&
    {
        const auto colour = part.newTakeColour();
        return part.takes.emplace_back(Take{newId(), from, to, 0, {}, colour});
    };
    const auto none = std::ranges::find_if(part.comp, [&](const TakeRange& range)
                                           { return at >= range.from && at < range.to; });
    if (none != part.comp.end())
    {
        const auto range = *none;
        auto& made = added(range.from, range.to);
        assignTake(part, {range.from, range.to, made.id});
        return made;
    }
    if (part.takes.empty())
    {
        return added(0, length);
    }
    // A gap: no take covers it, so it lies between where one ends and the next starts.
    Tick gapFrom = 0;
    Tick gapTo = length;
    for (const auto& take : part.takes)
    {
        if (take.to <= at)
        {
            gapFrom = std::max(gapFrom, take.to);
        }
        else
        {
            gapTo = std::min(gapTo, take.from);
        }
    }
    const auto newest = [&](auto matches) -> Take*
    {
        const auto it = std::ranges::find_if(part.takes.rbegin(), part.takes.rend(), matches);
        return it == part.takes.rend() ? nullptr : &*it;
    };
    if (auto* before = newest([&](const Take& take) { return take.to == gapFrom; }))
    {
        before->to = gapTo;
        return *before;
    }
    auto* after = newest([&](const Take& take) { return take.from == gapTo; });
    after->from = gapFrom;
    return *after;
}

} // namespace

void replacePlayingEvents(TrackContent& part, EventList events, Tick length, const NewTakeId& newId)
{
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
    for (auto& take : part.takes)
    {
        hide(take.events, take.id);
    }
    for (auto event : events.all())
    {
        const auto at = event.tick;
        takeFor(part, at, length, newId).events.insert(std::move(event));
    }
    // A part whose only take has lost all it held is empty again, as it was before entering
    // material made the take.
    if (part.takes.size() == 1 && part.comp.empty() && part.takes[0].events.empty())
    {
        part.takes.clear();
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
    const auto length = song.sections()[section]->length();
    const bool changed = song.updatePart(section, track,
                                         [&](TrackContent& edited)
                                         {
                                             auto playing = playingEvents(edited);
                                             edited.comp.clear();
                                             std::erase_if(edited.takes, [&](const Take& other)
                                                           { return other.id != take; });
                                             auto& kept = edited.takes.front();
                                             kept.from = 0;
                                             kept.to = length;
                                             kept.events = std::move(playing);
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
