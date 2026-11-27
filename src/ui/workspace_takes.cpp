// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/takes.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>
#include <tuple>

namespace modal::ui
{

// ]t and [t choose among the cursor's section and track's takes in stack order.

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

TakeChoice rangeChoice(const core::Song& song, core::TrackId track, std::size_t section,
                       core::Tick from, core::Tick to, bool singleBar)
{
    TakeChoice choice{.track = track,
                      .section = section,
                      .from = from,
                      .to = to,
                      .candidates = {},
                      .singleBar = singleBar};
    const auto* part = song.sections()[section]->content(track);
    if (!part)
        return choice;
    const auto start = song.sectionStart(section);
    const auto active = core::activeSource(*part, from - start);
    for (const auto& take : part->takes)
    {
        if (take.from <= from - start && take.to >= to - start)
        {
            if (take.id == active)
                choice.selected = choice.candidates.size();
            choice.candidates.push_back(take.id);
        }
    }
    return choice;
}

} // namespace

std::span<const core::TrackId> Workspace::expandedTakeTracks() const
{
    const auto found = expandedTakeTracks_.find(document_.underlying().identity());
    return found == expandedTakeTracks_.end() ? std::span<const core::TrackId>{}
                                              : std::span<const core::TrackId>{found->second};
}

CommandResult Workspace::toggleTakeLanes()
{
    const auto track = view().cursor().track;
    if (!track)
        return CommandResult::error("Go to a track to show its takes");
    const auto& song = document_.underlying();
    auto& expanded = expandedTakeTracks_[song.identity()];
    const auto found = std::ranges::find(expanded, *track);
    const bool show = found == expanded.end();
    if (show)
        expanded.push_back(*track);
    else
        expanded.erase(found);
    layOut();
    return {true, "Take lanes " + std::string{show ? "shown on " : "hidden on "} +
                      trackName(song, *track)};
}

CommandResult Workspace::chooseTakeRange()
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    if (shown() != ViewKind::Arrangement && shown() != ViewKind::PianoRoll)
        return CommandResult::error("Choose a take in the arrangement or piano roll");
    const auto cursor = view().cursor();
    if (!cursor.track)
        return CommandResult::error("Go to a track to choose its takes");

    const auto& song = document_.underlying();
    const auto& meter = song.meter(*cursor.track);
    const auto bar = meter.toBarBeatTick(cursor.tick).bar;
    auto from = meter.barStart(bar);
    auto to = std::min(meter.barStart(bar + 1), song.end());
    if (selection_)
    {
        const auto span = view().selectionSpan();
        if (!span)
            return CommandResult::error("Select the time to comp");
        if (shown() == ViewKind::Arrangement && selection_->anchor.track != cursor.track)
            return CommandResult::error("Choose a take on one track at a time");
        std::tie(from, to) = *span;
    }
    const auto section = song.sectionAt(selection_ ? from : cursor.tick);
    const auto start = song.sectionStart(section);
    const auto end = song.sectionStart(section + 1);
    if (!selection_)
    {
        from = std::max(from, start);
        to = std::min(to, end);
    }
    if (from >= to || from < start || to > end)
        return CommandResult::error("Choose a range within one section");
    const auto* part = song.sections()[section]->content(*cursor.track);
    if (!part || part->takes.empty())
        return CommandResult::error("No takes here");
    // Until None can be chosen, a part with nothing to choose between has nothing to offer.
    if (!core::hasComping(*part))
        return CommandResult::error("No other takes here");

    auto choice = rangeChoice(song, *cursor.track, section, from, to, !selection_);
    endVisual();
    takeChoice_ = std::move(choice);
    layOut();
    return {true};
}

bool Workspace::moveTakeBar(int direction)
{
    const auto& song = document_.underlying();
    const auto& current = *takeChoice_;
    const auto& meter = song.meter(current.track);
    const auto next = direction < 0 ? current.from - 1 : current.to;
    if (next < 0 || next >= song.end())
        return false;
    const auto bar = meter.toBarBeatTick(next).bar;
    const auto section = song.sectionAt(next);
    const auto from = std::max(meter.barStart(bar), song.sectionStart(section));
    const auto to = std::min(meter.barStart(bar + 1), song.sectionStart(section + 1));
    const auto track = current.track;
    takeChoice_ = rangeChoice(song, track, section, from, to, true);
    updateTakePreview();
    auto cursor = view().cursor();
    cursor.tick = from;
    view().setCursor(cursor);
    layOut();
    return true;
}

bool Workspace::takeChoiceKey(const Key& key)
{
    const auto binding = keymap_.match(Mode::Normal, std::vector<Key>{key});
    if (binding.binding &&
        (binding.binding->command == "play-toggle" || binding.binding->command == "play-cursor" ||
         binding.binding->command == "panic"))
        return transport(binding.binding->command);
    if (key.modifiers != kNoModifiers)
        return false;
    if (key.named == NamedKey::Escape)
    {
        closeTakeChoice();
        setMessage("Take choice cancelled", MessageKind::Information);
        return true;
    }
    auto& choice = *takeChoice_;
    if (choice.singleBar)
    {
        if (key.character == U'h' || key.named == NamedKey::Left)
            return moveTakeBar(-1);
        if (key.character == U'l' || key.named == NamedKey::Right)
            return moveTakeBar(1);
    }
    if (choice.candidates.empty())
        return false;
    if (key.character == U'j' || key.named == NamedKey::Down)
    {
        choice.selected = std::min(choice.selected + 1, choice.candidates.size() - 1);
    }
    else if (key.character == U'k' || key.named == NamedKey::Up)
    {
        if (choice.selected > 0)
            --choice.selected;
    }
    else if (key.character == U'p')
    {
        choice.preview = !choice.preview;
    }
    else if (key.named == NamedKey::Enter)
    {
        const auto chosen = choice;
        closeTakeChoice(false);
        const auto start = document_.underlying().sectionStart(chosen.section);
        document_.edit("Choose take for range", view().place(),
                       [&](core::Song& song)
                       {
                           song.updatePart(chosen.section, chosen.track,
                                           [&](core::TrackContent& part)
                                           {
                                               core::assignTake(part,
                                                                {chosen.from - start,
                                                                 chosen.to - start, chosen.take()});
                                           });
                       });
        refresh();
        if (player_)
            player_->update(document_.underlying());
        setMessage("Take applied", MessageKind::Information);
        return true;
    }
    else
    {
        return false;
    }
    updateTakePreview();
    return true;
}

void Workspace::updateTakePreview()
{
    const auto& choice = *takeChoice_;
    if (choice.preview)
    {
        const auto id = choice.take();
        const auto start = document_.underlying().sectionStart(choice.section);
        const core::TakeRange range{choice.from - start, choice.to - start, id};
        document_.setPreview(
            [track = choice.track, section = choice.section, range](const core::Song& song)
            {
                auto preview = song;
                preview.updatePart(section, track, [&](core::TrackContent& part)
                                   { core::assignTake(part, range); });
                return preview;
            });
    }
    else if (document_.hasPreview())
    {
        document_.setPreview({});
    }
    else
    {
        return;
    }
    refresh();
    if (player_)
        player_->update(document_.song());
}

void Workspace::closeTakeChoice(bool restorePlayback)
{
    if (!takeChoice_)
        return;
    if (takeChoice_->preview)
    {
        document_.setPreview({});
        if (restorePlayback)
        {
            refresh();
            if (player_)
                player_->update(document_.underlying());
        }
    }
    takeChoice_.reset();
    layOut();
}

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
    if (!core::hasComping(*part))
        return {false, "No other takes on " + name + " in " + section.name(), false, true};

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

    const auto number = std::to_string(chosen.id);
    const auto where = " on " + name + " in " + section.name();
    const auto title = "Choose take " + number + where;
    const auto start = song.sectionStart(index);
    const auto span = barsText(song, start + chosen.from, start + chosen.to);
    const auto id = chosen.id;
    if (document_.edit(title, view().place(),
                       [&](core::Song& edited) { core::chooseTake(edited, index, *track, id); }))
    {
        refresh();
    }
    // Numbered across the song, so its number says nothing of how many there are here.
    const auto message = "Take " + number + " (" + std::to_string(size) +
                         (size == 1 ? " take)" : " takes)") + where + ", " + span;
    return {true, message};
}

CommandResult Workspace::removeTakes(bool keepOnly)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto track = view().cursor().track;
    if (!track)
        return CommandResult::error("Go to a track to manage its takes");
    const auto& song = document_.song();
    const auto tick = view().cursor().tick;
    const auto index = song.sectionAt(tick);
    const auto& section = *song.sections()[index];
    const auto where = " on " + trackName(song, *track) + " in " + section.name();
    const auto* part = section.content(*track);
    if (part == nullptr || part->takes.empty())
        return {false, "No takes" + where, false, true};
    const auto playing = core::playingTake(*part, tick - song.sectionStart(index));
    if (!playing)
        return {false, "No take playing" + where, false, true};

    if (keepOnly)
    {
        if (part->takes.size() == 1)
            return {false, "Only one take" + where, false, true};
        const bool changed =
            document_.edit("Keep only this take" + where, view().place(), [&](core::Song& edited)
                           { core::keepOnlyTake(edited, index, *track, *playing); });
        if (changed)
        {
            refresh();
        }
        return {changed, changed ? "Kept only this take" + where : ""};
    }

    const bool lastTake = part->takes.size() == 1;
    const bool changed =
        document_.edit("Discard take" + where, view().place(), [&](core::Song& edited)
                       { core::discardTake(edited, index, *track, *playing); });
    if (!changed)
        return {false, ""};
    refresh();
    return {true, "Discarded the take" + where + (lastTake ? ". No takes left" : "")};
}

} // namespace modal::ui
