// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/takes.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>

namespace modal::ui
{

// Takes (decided 25 November 2026): each replace recording is kept as a take of its section on
// the track, and ]t and [t choose among the cursor's section and track's takes, in the order
// recorded with Before first, going round, heard at once while playing.

namespace
{

std::string trackName(const core::Song& song, core::TrackId id)
{
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i]->id() == id)
            return tracks[i]->name.empty() ? "track " + std::to_string(i + 1) : tracks[i]->name;
    }
    return "the track";
}

// "bar 5" or "bars 5–6", in the song's bars.
std::string barsText(const core::Song& song, core::Tick from, core::Tick to)
{
    const auto first = song.meter().toBarBeatTick(from).bar;
    const auto last = song.meter().toBarBeatTick(to - 1).bar;
    return first == last ? "bar " + std::to_string(first)
                         : "bars " + std::to_string(first) + "–" + std::to_string(last);
}

} // namespace

CommandResult Workspace::stepTake(int count, bool forward)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto track = view().cursor().track;
    if (!track)
        return CommandResult::error("Go to a track to choose its takes");
    const auto& song = document_.song();
    const auto tick = view().cursor().tick;
    const auto index = song.sectionAt(tick);
    const auto& section = *song.sections()[index];
    const auto name = trackName(song, *track);
    const auto* part = section.content(*track);
    if (part == nullptr || part->takes.empty())
        return {false, "No takes on " + name + " in " + section.name(), false, true};

    // From the take playing, or before the first going forward (after the last going back).
    const auto& takes = part->takes;
    const auto size = static_cast<std::ptrdiff_t>(takes.size());
    const auto playing = core::playingTake(*part, tick - song.sectionStart(index));
    std::ptrdiff_t at = forward ? -1 : size;
    if (playing)
        at = std::ranges::find(takes, *playing, &core::Take::id) - takes.begin();
    const auto steps = std::max(count, 1);
    at = ((at + (forward ? steps : -steps)) % size + size) % size;
    const auto& chosen = takes[static_cast<std::size_t>(at)];

    const auto performances =
        std::ranges::count_if(takes, [](const core::Take& take) { return !take.before; });
    const auto number = std::ranges::count_if(takes.begin(), takes.begin() + at + 1,
                                              [](const core::Take& take) { return !take.before; });
    const auto where = " on " + name + " in " + section.name();
    const auto title =
        chosen.before ? "Choose Before" + where : "Choose take " + std::to_string(number) + where;
    const auto start = song.sectionStart(index);
    const auto span = barsText(song, start + chosen.from, start + chosen.to);
    const auto id = chosen.id;
    document_.edit(title, view().place(),
                   [&](core::Song& edited) { core::chooseTake(edited, index, *track, id); });
    const auto message = chosen.before ? "Before the takes" + where + ", " + span
                                       : "Take " + std::to_string(number) + " of " +
                                             std::to_string(performances) + where + ", " + span;
    return {true, message};
}

} // namespace modal::ui
