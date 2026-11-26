// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/takes.hpp>

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

namespace modal::core
{

std::optional<std::uint32_t> playingTake(const TrackContent& part, Tick at)
{
    const auto noted = [&](const Take& take)
    {
        return std::ranges::any_of(part.events.all(),
                                   [&](const Event& event) { return event.take == take.id; });
    };
    std::optional<std::uint32_t> latest;
    std::optional<std::uint32_t> here;
    for (const auto& take : part.takes)
    {
        if (!noted(take))
            continue;
        latest = take.id;
        if (at >= take.from && at < take.to)
            here = take.id;
    }
    return here ? here : latest;
}

namespace
{

void applyTake(TrackContent& part, const Take& chosen, std::uint32_t discarded = 0)
{
    std::vector<Event> kept;
    for (auto event : part.events.all())
    {
        if ((discarded != 0 && event.take == discarded) ||
            (event.tick >= chosen.from && event.tick < chosen.to))
        {
            continue;
        }
        if (auto* note = std::get_if<Note>(&event.data); note != nullptr &&
                                                         event.tick < chosen.from &&
                                                         event.tick + note->duration > chosen.from)
        {
            note->duration = chosen.from - event.tick;
        }
        kept.push_back(std::move(event));
    }
    std::vector<Event> added;
    for (auto event : chosen.events.all())
    {
        event.take = chosen.id;
        added.push_back(std::move(event));
    }
    resolveOverlaps(kept, added);
    EventList result;
    kept.insert(kept.end(), added.begin(), added.end());
    std::ranges::stable_sort(kept, {}, &Event::tick);
    for (auto& event : kept)
        result.insert(std::move(event));
    part.events = std::move(result);
}

} // namespace

bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
        return false;
    const auto* part = song.sections()[section]->content(track);
    const Take* chosen = part != nullptr ? part->take(take) : nullptr;
    if (chosen == nullptr)
        return false;
    return song.updatePart(section, track,
                           [&](TrackContent& edited) { applyTake(edited, *chosen); });
}

std::size_t keepOnlyTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
        return 0;
    const auto* part = song.sections()[section]->content(track);
    if (part == nullptr || part->take(take) == nullptr)
        return 0;
    const auto others = part->takes.size() - 1;
    if (others == 0)
        return 0;
    const bool updated = song.updatePart(
        section, track, [&](TrackContent& edited)
        { std::erase_if(edited.takes, [&](const Take& other) { return other.id != take; }); });
    return updated ? others : 0;
}

Discarded discardTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
        return {};
    const auto* part = song.sections()[section]->content(track);
    if (part == nullptr || part->take(take) == nullptr)
        return {};
    const auto& takes = part->takes;
    const auto at =
        static_cast<std::size_t>(std::ranges::find(takes, take, &Take::id) - takes.begin());
    const Take* replacement = nullptr;
    if (takes.size() > 1)
        replacement = &takes[at > 0 ? at - 1 : at + 1];
    const auto replacementId = replacement ? std::optional{replacement->id} : std::nullopt;
    const bool updated = song.updatePart(section, track,
                                         [&](TrackContent& edited)
                                         {
                                             if (replacement != nullptr)
                                             {
                                                 applyTake(edited, *replacement, take);
                                             }
                                             else
                                             {
                                                 EventList kept;
                                                 for (const auto& event : edited.events.all())
                                                 {
                                                     if (event.take != take)
                                                         kept.insert(event);
                                                 }
                                                 edited.events = std::move(kept);
                                             }
                                             std::erase_if(edited.takes, [&](const Take& other)
                                                           { return other.id == take; });
                                         });
    return updated ? Discarded{true, replacementId} : Discarded{};
}

} // namespace modal::core
