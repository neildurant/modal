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

bool chooseTake(Song& song, std::size_t section, TrackId track, std::uint32_t take)
{
    if (section >= song.sections().size())
        return false;
    const auto* part = song.sections()[section]->content(track);
    const Take* chosen = part != nullptr ? part->take(take) : nullptr;
    if (chosen == nullptr)
        return false;
    const auto from = chosen->from;
    const auto to = chosen->to;
    const auto events = chosen->events;
    return song.updatePart(section, track,
                           [&](TrackContent& edited)
                           {
                               std::vector<Event> kept;
                               for (auto event : edited.events.all())
                               {
                                   if (event.tick >= from && event.tick < to)
                                       continue;
                                   if (auto* note = std::get_if<Note>(&event.data);
                                       note != nullptr && event.tick < from &&
                                       event.tick + note->duration > from)
                                   {
                                       note->duration = from - event.tick;
                                   }
                                   kept.push_back(std::move(event));
                               }
                               std::vector<Event> added;
                               for (auto event : events.all())
                               {
                                   event.take = take;
                                   added.push_back(std::move(event));
                               }
                               resolveOverlaps(kept, added);
                               EventList result;
                               std::vector<Event> all = std::move(kept);
                               all.insert(all.end(), added.begin(), added.end());
                               std::ranges::stable_sort(all, {}, &Event::tick);
                               for (auto& event : all)
                                   result.insert(std::move(event));
                               edited.events = std::move(result);
                           });
}

} // namespace modal::core
