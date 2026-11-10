// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/musical_time.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/time_motions.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/view_state.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr std::array kCommands{
    CommandInfo{.name = "octave-up", .description = "Up an octave, {count} times", .count = true},
    CommandInfo{
        .name = "octave-down", .description = "Down an octave, {count} times", .count = true},
    CommandInfo{.name = "note-lengthen",
                .description = "Lengthen the current note to the next grid step after its end, "
                               "{count} times",
                .count = true},
    CommandInfo{.name = "note-shorten",
                .description = "Shorten the current note to the grid step before its end, "
                               "{count} times",
                .count = true},
    CommandInfo{.name = "note-lengthen-fine",
                .description = "Lengthen the current note by a nudge (the nudge setting), "
                               "{count} times",
                .count = true},
    CommandInfo{.name = "note-shorten-fine",
                .description = "Shorten the current note by a nudge (the nudge setting), "
                               "{count} times",
                .count = true},
    CommandInfo{.name = "velocity-up",
                .description = "The current note's velocity up, {count} at a time",
                .count = true},
    CommandInfo{.name = "velocity-down",
                .description = "The current note's velocity down, {count} at a time",
                .count = true},
    CommandInfo{.name = "cursor-note-next",
                .description = "Next note's start, at any pitch",
                .count = true},
    CommandInfo{.name = "cursor-note-previous",
                .description = "Note's start before, at any pitch",
                .count = true},
    CommandInfo{.name = "cursor-bar-next-scroll",
                .description = "Same place, next bar, scrolling, {count} times",
                .count = true},
    CommandInfo{.name = "cursor-bar-previous-scroll",
                .description = "Same place, bar before, scrolling, {count} times",
                .count = true},
    CommandInfo{.name = "scroll-page-left",
                .description = "A page earlier, scrolling, {count} times",
                .count = true},
    CommandInfo{.name = "scroll-page-right",
                .description = "A page later, scrolling, {count} times",
                .count = true},
    CommandInfo{.name = "grid-coarser",
                .description = "Make the track's grid coarser, {count} times",
                .count = true},
    CommandInfo{.name = "grid-finer",
                .description = "Make the track's grid finer, {count} times",
                .count = true},
    CommandInfo{.name = "grid-triplets",
                .description = "Turn triplets in the track's grid on or off"},
    CommandInfo{.name = "default-note-set",
                .description = "Take this note's length and velocity as the track's default note"},
    CommandInfo{.name = "note-add-advance",
                .description = "Add {count} notes one after another, moving on past each",
                .count = true},
    CommandInfo{.name = "move-up",
                .description = "Move this note up a semitone, {count} times",
                .count = true},
    CommandInfo{.name = "move-down",
                .description = "Move this note down a semitone, {count} times",
                .count = true},
    CommandInfo{.name = "move-octave-up",
                .description = "Move this note up an octave, {count} times",
                .count = true},
    CommandInfo{.name = "move-octave-down",
                .description = "Move this note down an octave, {count} times",
                .count = true},
    CommandInfo{.name = "move-bar-next",
                .description = "Move this note a bar later, {count} times",
                .count = true},
    CommandInfo{.name = "move-bar-previous",
                .description = "Move this note a bar earlier, {count} times",
                .count = true},
    CommandInfo{.name = "move-right-fine",
                .description = "Nudge this note later, {count} times",
                .count = true},
    CommandInfo{.name = "move-left-fine",
                .description = "Nudge this note earlier, {count} times",
                .count = true},
    CommandInfo{.name = "grid",
                .description = "Set the track's grid, or show it with no length",
                .usage = "[LENGTH]",
                .keys = false,
                .choices = {Choices::Length}},
    CommandInfo{.name = "len",
                .description = "Set the length of the track's default note, or grid to follow "
                               "the grid; or show it with no length",
                .usage = "[LENGTH|grid]",
                .keys = false,
                .choices = {Choices::Length}},
};

// The grid's limits, without triplets: from a 1/64 note to a whole note.
constexpr core::NoteFraction kFinestGrid{1, 64};
constexpr std::uint8_t kQuantizeMark = 140; // how strongly a quantised note's mark shows
constexpr core::NoteFraction kCoarsestGrid{1, 1};

bool isTriplet(core::NoteFraction grid)
{
    return grid.denominator() % 3 == 0;
}

// The grid without its triplets: 1/8t is 1/8.
core::NoteFraction straight(core::NoteFraction grid)
{
    return isTriplet(grid) ? core::NoteFraction{grid.numerator() * 3, grid.denominator() * 2}
                           : grid;
}

// The default note, as the view's bar shows it: its length, or grid while it follows the
// grid, and its velocity.
std::string noteText(const core::Track& track)
{
    return (track.noteLength ? core::toString(*track.noteLength) : std::string{"grid"}) + " v" +
           std::to_string(track.noteVelocity);
}

constexpr std::uint8_t kBand = 80;      // the opacity of the crosshair's row and column
constexpr std::uint8_t kBlackKeys = 64; // and of the black keys' rows, just apart
constexpr std::uint8_t kFaint = 96;     // and of labels' lines, as in the arrangement
constexpr std::uint8_t kDimmed = 150;   // and of the shade over a note's part outside a range

bool isBlackKey(int pitch)
{
    const int key = pitch % 12;
    return key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
}

// A note's opacity, from its velocity: faint for the softest, solid for the loudest; and its
// body's after its start, lighter.
std::uint8_t velocityAlpha(std::uint8_t velocity)
{
    return static_cast<std::uint8_t>(96 + velocity * 159 / 127);
}

std::uint8_t bodyAlpha(std::uint8_t velocity)
{
    return static_cast<std::uint8_t>(velocityAlpha(velocity) * 3 / 5);
}

} // namespace

std::span<const CommandInfo> PianoRollView::commands()
{
    return kCommands;
}

PianoRollView::PianoRollView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
{
}

std::vector<int> PianoRollView::topHeights(CellSize cell) const
{
    return {cell.height, inspectorHeight(cell) + 2 * cell.height};
}

void PianoRollView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = {std::max(cell.width, 1), std::max(cell.height, 1)};
    follow();
}

std::vector<std::pair<core::Tick, std::uint8_t>>
PianoRollView::matchPlaces(const core::EventQuery& query) const
{
    std::vector<std::pair<core::Tick, std::uint8_t>> places;
    const auto id = trackId();
    if (!id)
    {
        return places;
    }
    const auto& song = document_.song();
    for (const auto& event : song.events(*id).all())
    {
        if (core::matches(query, event.data, song.ppqn()))
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            places.emplace_back(event.tick, note != nullptr ? note->pitch : pitch_);
        }
    }
    std::ranges::sort(places);
    places.erase(std::ranges::unique(places).begin(), places.end());
    return places;
}

std::optional<core::EventQuery> PianoRollView::queryHere() const
{
    // The cursor's pitch, the note's when it's on one.
    core::EventQuery query;
    query.pitch = pitch_;
    return query;
}

std::optional<OperatorTarget::Part> PianoRollView::matchesHere(const core::EventQuery& query) const
{
    // At the cursor's time: a matching note at its pitch, or any other matching event.
    const auto id = trackId();
    if (!id)
    {
        return std::nullopt;
    }
    const auto& song = document_.song();
    const auto& events = song.events(*id);
    OperatorTarget::Part part{*id, {}};
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto* note = std::get_if<core::Note>(&events[i].data);
        if (events[i].tick == time_ && (note == nullptr || note->pitch == pitch_) &&
            core::matches(query, events[i].data, song.ppqn()))
        {
            part.events.push_back(i);
        }
    }
    return part.events.empty() ? std::nullopt : std::optional{std::move(part)};
}

std::string PianoRollView::matchStatus(const core::EventQuery& query) const
{
    return matchCount(matchPlaces(query), std::pair{time_, pitch_});
}

std::optional<core::TrackId> PianoRollView::trackId() const
{
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return std::nullopt;
    }
    return tracks[std::min(track_, tracks.size() - 1)]->id();
}

const core::MeterMap& PianoRollView::meter() const
{
    const auto id = trackId();
    return id ? document_.song().meter(*id) : document_.song().meter();
}

core::Tick PianoRollView::lastTick() const
{
    return std::max<core::Tick>(document_.song().end() - 1, 0);
}

core::NoteFraction PianoRollView::grid() const
{
    const auto tracks = document_.song().tracks();
    return tracks.empty() ? core::Track::kDefaultGrid
                          : tracks[std::min(track_, tracks.size() - 1)]->grid;
}

core::Tick PianoRollView::gridTicks() const
{
    return std::max<core::Tick>(grid().toTicks(document_.song().ppqn()), 1);
}

std::string PianoRollView::trackName() const
{
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return {};
    }
    const auto& track = *tracks[std::min(track_, tracks.size() - 1)];
    return track.name.empty() ? "track " + std::to_string(track_ + 1) : track.name;
}

void PianoRollView::changeTrack(std::string title, std::string_view mergeKey,
                                const std::function<void(core::Track&)>& change)
{
    const auto id = trackId();
    if (!id)
    {
        return;
    }
    document_.edit(
        std::move(title), place(), [&](core::Song& song) { song.updateTrack(*id, change); },
        mergeKey);
    refresh();
}

void PianoRollView::setGrid(core::NoteFraction grid)
{
    // Steps one after another undo together, on each track.
    const auto id = trackId();
    if (!id)
    {
        return;
    }
    changeTrack("Grid " + core::toString(grid) + " in " + trackName(),
                "grid " + std::to_string(id->value),
                [&](core::Track& track) { track.grid = grid; });
}

void PianoRollView::editEvents(std::string title,
                               const std::function<void(core::EventList&)>& change,
                               std::string_view mergeKey)
{
    const auto id = trackId();
    if (!id)
    {
        return;
    }
    document_.edit(
        std::move(title), place(), [&](core::Song& song) { song.updateEvents(*id, change); },
        mergeKey);
    refresh();
}

bool PianoRollView::selecting() const
{
    const auto selection = host_.selection();
    return selection && selection->where == Where::PianoRoll;
}

std::optional<std::pair<core::Tick, core::Tick>> PianoRollView::selected() const
{
    const auto selection = host_.selection();
    if (!selection || selection->where != Where::PianoRoll || !trackId())
    {
        return std::nullopt;
    }
    std::pair<core::Tick, core::Tick> span;
    if (selection->kind == SelectionKind::Rows)
    {
        // V: the page's time when it started, or an object's.
        span = selection->span.value_or(std::pair{page().start, page().end});
    }
    else if (selection->kind == SelectionKind::Box && selection->span)
    {
        // A note's or a chord's, as an object selected it, its time; takes() keeps to its
        // starts.
        span = *selection->span;
    }
    else
    {
        const auto from = std::min(selection->anchor.tick, time_);
        const auto last = std::max(selection->anchor.tick, time_);
        span = {from, std::max(gridAfter(last), last + 1)};
    }
    if (const auto& moved = selection->movedNotes;
        moved && moved->track == *trackId() && !moved->positions.empty())
    {
        // Snapping off-grid notes needn't translate them all by the same amount. Keep the
        // visible/editable span around every retained start, without collecting its neighbours.
        span.first = std::min(span.first, moved->positions.front().first);
        const auto last = moved->positions.back().first;
        if (last >= span.second)
            span.second = gridAfter(last);
    }
    return span;
}

std::optional<std::pair<std::uint8_t, std::uint8_t>> PianoRollView::selectedPitches() const
{
    const auto selection = host_.selection();
    if (!selection || selection->where != Where::PianoRoll ||
        selection->kind == SelectionKind::Range)
    {
        return std::nullopt;
    }
    // From the pitch where it started to the cursor's, both in.
    const auto anchor = selection->anchor.pitch.value_or(pitch_);
    return std::pair{std::min(anchor, pitch_), std::max(anchor, pitch_)};
}

bool PianoRollView::takes(core::Tick tick, std::uint8_t pitch) const
{
    const auto span = selected();
    const auto pitches = selectedPitches();
    const auto selection = host_.selection();
    if (selection && selection->movedNotes)
    {
        const auto id = trackId();
        return id && selection->movedNotes->contains(*id, tick, pitch);
    }
    const auto starts = selection && selection->starts ? selection->starts : span;
    return starts && tick >= starts->first && tick < starts->second &&
           (!pitches || (pitch >= pitches->first && pitch <= pitches->second));
}

std::optional<CutRange> PianoRollView::cutRange(std::string_view motion, int count)
{
    // The selection's time and its pitches, or the time a range or object takes.
    const auto id = trackId();
    if (!id)
    {
        return std::nullopt;
    }
    std::optional<std::pair<core::Tick, core::Tick>> span;
    if (motion == "visual")
    {
        span = selected();
    }
    else if (motion == "operator-line" || motion.starts_with("object-"))
    {
        span = objectSpan(motion, count);
    }
    else
    {
        span = timeSpan(motion, count);
    }
    if (!span)
    {
        return std::nullopt;
    }
    return CutRange{.tracks = {*id},
                    .from = span->first,
                    .to = span->second,
                    .pitches = motion == "visual" ? selectedPitches() : std::nullopt};
}

std::optional<std::pair<core::Tick, core::Tick>> PianoRollView::rowsSpan() const
{
    // The whole track, as v takes every pitch, rather than the page's, which would make what V
    // takes depend on what's shown (decided November 2026); , s v selects what's shown.
    return std::pair{core::Tick{0}, document_.song().end()};
}

std::optional<std::pair<core::Tick, core::Tick>> PianoRollView::selectionSpan() const
{
    return selected();
}

std::optional<std::string> PianoRollView::selectionStatus() const
{
    const auto span = selected();
    if (!span)
    {
        return std::nullopt;
    }
    const auto& m = meter();
    std::string rows;
    if (const auto pitches = selectedPitches())
    {
        rows =
            core::pitchName(pitches->first) +
            (pitches->second == pitches->first ? "" : " to " + core::pitchName(pitches->second)) +
            ", ";
    }
    return rows + positionText(m, span->first) + " to " + positionText(m, span->second) + " in " +
           trackName();
}

std::optional<std::pair<core::Tick, core::Tick>> PianoRollView::objectSpan(std::string_view object,
                                                                           int count)
{
    // The time an object takes: a note's or a chord's from its start to its last end, or the
    // span of bars, sections or the track.
    const auto id = trackId();
    if (!id)
    {
        return std::nullopt;
    }
    const auto& song = document_.song();
    const auto& events = song.events(*id);
    std::vector<std::size_t> taken;
    if (object == "object-note")
    {
        if (const auto index = noteHere("select"))
        {
            taken.push_back(*index);
        }
    }
    else if (object == "object-chord")
    {
        taken = chordHere(count);
    }
    else if (object == "object-track" || object == "object-song")
    {
        return std::pair{core::Tick{0}, song.end()};
    }
    else if (object == "object-visible")
    {
        return std::pair{viewStart_, std::min(viewEnd(viewStart_), song.end())};
    }
    else if (isSectionObject(object))
    {
        const auto range = sectionsFrom(song, time_, count);
        return std::pair{range.from, range.to};
    }
    else
    {
        const auto& m = meter();
        const auto bar = m.toBarBeatTick(time_).bar;
        return std::pair{m.barStart(bar),
                         std::min(m.barStart(bar + std::max(count, 1)), song.end())};
    }
    if (taken.empty())
    {
        return std::nullopt;
    }
    core::Tick end = 0;
    for (const auto i : taken)
    {
        end = std::max(end, events[i].tick + std::get<core::Note>(events[i].data).duration);
    }
    return std::pair{events[taken.front()].tick, end};
}

void PianoRollView::selectObject(std::string_view object, int count, Selection& selection)
{
    // in and ic select what an operator takes with them, their notes and only those: a box of
    // their pitches over their starts (decided November 2026, so vind is dind); the other
    // objects their time, on every pitch, as their operators take it.
    if (object != "object-note" && object != "object-chord")
    {
        View::selectObject(object, count, selection);
        return;
    }
    const auto id = trackId();
    if (!id)
    {
        return;
    }
    std::vector<std::size_t> taken;
    if (object == "object-note")
    {
        if (const auto index = noteHere("select"))
        {
            taken.push_back(*index);
        }
    }
    else
    {
        taken = chordHere(count);
    }
    if (taken.empty())
    {
        return;
    }
    const auto& events = document_.song().events(*id);
    auto low = std::uint8_t{127};
    auto high = std::uint8_t{0};
    auto first = events[taken.front()].tick;
    auto last = first;
    core::Tick end = 0;
    for (const auto i : taken)
    {
        const auto& note = std::get<core::Note>(events[i].data);
        low = std::min(low, note.pitch);
        high = std::max(high, note.pitch);
        first = std::min(first, events[i].tick);
        last = std::max(last, events[i].tick);
        end = std::max(end, events[i].tick + std::max<core::Tick>(note.duration, 1));
    }
    selection.kind = SelectionKind::Box;
    selection.anchor = SongCursor{*id, first, low};
    selection.span = std::pair{first, end};
    selection.starts = std::pair{first, last + 1};
    setCursor(SongCursor{*id, first, high});
}

std::vector<std::size_t> PianoRollView::chordHere(int count) const
{
    const auto index = currentNote();
    if (!index)
    {
        host_.refuse("No chord here", MessageKind::Warning);
        return {};
    }
    const auto& song = document_.song();
    return chordNotes(song, song.events(*trackId()), *index, count, chordWindow_);
}

std::optional<std::size_t> PianoRollView::noteHere(std::string_view what) const
{
    const auto index = currentNote();
    if (!index)
    {
        host_.refuse("No note here to " + std::string{what}, MessageKind::Warning);
    }
    return index;
}

bool PianoRollView::addNote(int count, bool advance)
{
    // The track's default note at the cursor, unless there's a note there or it would run into
    // one. o takes no count; O adds one after another, each where the last ends, or with
    // gridadvance at the next grid step, as far as the song's end or a note in the way.
    const auto id = trackId();
    if (!id)
    {
        return false;
    }
    if (currentNote())
    {
        host_.refuse("There's a note here already", MessageKind::Warning);
        return true;
    }
    const auto& song = document_.song();
    const auto& track = *song.tracks()[track_];
    const auto length = std::max<core::Tick>(track.defaultLength().toTicks(song.ppqn()), 1);
    const int n = advance ? std::max(count, 1) : 1;
    auto events = song.events(*id);
    auto tick = time_;
    int added = 0;
    for (; added < n && tick <= lastTick(); ++added)
    {
        const core::Event note{tick, core::Note{pitch_, track.noteVelocity, length}};
        if (auto why = overlap(events, std::nullopt, note, meter()))
        {
            host_.refuse(std::move(*why), MessageKind::Warning);
            break;
        }
        events.insert(note);
        tick = gridAdvance_ ? gridAfter(tick) : tick + length;
    }
    if (added == 0)
    {
        return true;
    }
    editEvents("Add " + (added == 1 ? core::pitchName(pitch_) : std::to_string(added) + " notes") +
                   " in " + trackName(),
               [&](core::EventList& list) { list = events; });
    if (advance)
    {
        moveTo(tick);
    }
    return true;
}

bool PianoRollView::operateOn(std::string_view op, const std::vector<std::size_t>& notes,
                              core::Tick from, core::Tick span)
{
    const auto id = *trackId();
    const auto& events = document_.song().events(id);
    auto& reg = host_.eventRegister();
    reg = EventRegister{};
    auto& taken = reg.tracks.emplace_back();
    for (const auto i : notes)
    {
        auto event = events[i];
        event.tick -= from;
        taken.push_back(std::move(event));
    }
    reg.span = span;
    reg.pitch = cursor().pitch;
    const auto what = notes.size() == 1
                          ? core::pitchName(std::get<core::Note>(events[notes.front()].data).pitch)
                          : std::to_string(notes.size()) + " notes";
    if (op == "operator-yank")
    {
        host_.setMessage(what + " yanked", MessageKind::Information);
        return true;
    }
    if (notes.empty())
    {
        host_.refuse("No notes there to delete", MessageKind::Information);
        return true;
    }
    editEvents("Delete " + what + " in " + trackName(),
               [&](core::EventList& list)
               {
                   for (auto it = notes.rbegin(); it != notes.rend(); ++it)
                   {
                       list.erase(*it);
                   }
               });
    return true;
}

bool PianoRollView::put(bool insert, bool ending, bool atPitch)
{
    // At the cursor's pitch too (Alt-p, Alt-P): its notes moved by as many semitones as take
    // the pitch the cursor was on when they were taken, or their lowest, to the cursor's,
    // refused where one would go out of range.
    auto moved = host_.eventRegister();
    const auto here = cursor().pitch;
    if (atPitch && here && !moved.empty() && moved.sections.empty())
    {
        auto from = moved.pitch;
        for (const auto& events : moved.tracks)
        {
            for (const auto& event : events)
            {
                if (const auto* note = std::get_if<core::Note>(&event.data); note && !moved.pitch)
                {
                    from = std::min(from.value_or(note->pitch), note->pitch);
                }
            }
        }
        const int by = from ? int{*here} - int{*from} : 0;
        for (auto& events : moved.tracks)
        {
            for (auto& event : events)
            {
                if (auto* note = std::get_if<core::Note>(&event.data))
                {
                    const int pitch = note->pitch + by;
                    if (pitch < 0 || pitch > 127)
                    {
                        host_.refuse(core::pitchName(note->pitch) + " would go out of range, " +
                                         (by > 0 ? "above G9" : "below C-1"),
                                     MessageKind::Warning);
                        return true;
                    }
                    note->pitch = static_cast<std::uint8_t>(pitch);
                }
            }
        }
    }
    const auto& reg = moved;
    if (reg.empty())
    {
        host_.refuse("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    if (!reg.wholeTracks.empty())
    {
        host_.refuse("Put whole tracks in the tracks view");
        return true;
    }
    if (!reg.sections.empty())
    {
        host_.refuse("The register holds sections: put them on the section strip",
                     MessageKind::Warning);
        return true;
    }
    // At the cursor's time, or taken as bars at the start of the cursor's bar, at the notes'
    // own pitches, onto this track and those after it for a register of several; over what's
    // there, refused where a note would run into one of its pitch, or with P inserted, moving
    // what's after it in the section later, asking first if the section must grow.
    const auto& song = document_.song();
    const auto tracks = song.tracks();
    const auto& m = meter();
    const auto bar = reg.bars ? m.barStart(m.toBarBeatTick(time_).bar) : time_;
    const auto starts = ending ? startToEnd(reg, bar) : std::optional{bar};
    if (!starts)
    {
        host_.refuse("Ending at " + putPlace(m, bar, reg.bars) + ", it would start before the song",
                     MessageKind::Warning);
        return true;
    }
    const auto at = *starts;
    std::size_t total = 0;
    for (std::size_t i = 0; i < reg.tracks.size() && track_ + i < tracks.size(); ++i)
    {
        const auto id = tracks[track_ + i]->id();
        for (auto event : reg.tracks[i])
        {
            event.tick += at;
            const auto why = overlap(song.events(id), std::nullopt, event, song.meter(id));
            if (why && !insert)
            {
                host_.refuse(*why, MessageKind::Warning);
                return true;
            }
            ++total;
        }
    }
    const auto where =
        reg.tracks.size() > 1 ? trackName() + " and the tracks after it" : trackName();
    const auto said = "Put " + eventCount(total) + " in " + where +
                      (ending ? ", ending at " + putPlace(m, bar, reg.bars)
                              : " at " + putPlace(m, at, reg.bars)) +
                      (atPitch && here ? ", from " + core::pitchName(*here) : std::string{});
    if (!insert)
    {
        document_.edit("Put " + eventCount(total) + " in " + where, place(),
                       [&](core::Song& s) { putRegister(s, reg, track_, at); });
        refresh();
        host_.setMessage(said + ", over what's there", MessageKind::Information);
        return true;
    }
    const auto bars = barsToInsert(song, reg, track_, at);
    const auto section = song.sectionAt(at);
    const auto title = "Insert " + eventCount(total) + " in " + where;
    const auto moving =
        said + ", moving the rest of " + song.sections()[section]->name() + " later";
    const auto insertIt = [this, reg, bars, section, title, moving, at, track = track_]
    {
        document_.edit(title, place(),
                       [&](core::Song& s)
                       {
                           if (bars > 0)
                           {
                               s.resizeSection(section, bars);
                           }
                           rippleInsert(s, reg, track, at);
                       });
        refresh();
        host_.setMessage(moving, MessageKind::Information);
    };
    if (bars == 0)
    {
        insertIt();
        return true;
    }
    host_.ask("Lengthen " + song.sections()[section]->name() + " by " +
                  (bars == 1 ? std::string{"a bar"} : std::to_string(bars) + " bars") +
                  " to make room?",
              insertIt);
    return true;
}

std::optional<bool> PianoRollView::runTyping(std::string_view command)
{
    // The current note's fields in the inspector, with e.
    const auto& m = meter();
    const auto noteNow = [this]() -> std::optional<core::Event>
    {
        const auto index = currentNote();
        return index ? std::optional{document_.song().events(*trackId())[*index]} : std::nullopt;
    };
    const auto set = [&](const std::string& heading,
                         const EditResult& result) -> std::optional<std::string>
    {
        if (const auto* why = std::get_if<std::string>(&result))
        {
            return *why;
        }
        return setNote(heading, std::get<core::Event>(result));
    };
    InspectorEditor::Hooks hooks{
        .inspection = [this] { return inspection(); },
        .set = [&](const std::string& heading,
                   const std::string& text) -> std::optional<std::string>
        {
            const auto note = noteNow();
            if (!note)
            {
                return "No note here to edit";
            }
            return set(heading,
                       isMusicalTimingField(heading)
                           ? setMusicalTiming(*note, heading, text, m, host_.timingUnit(grid()))
                           : setInspectorField(*note, heading, text, m));
        },
        .step = [&](const std::string& heading, int steps) -> std::optional<std::string>
        {
            const auto note = noteNow();
            if (!note)
            {
                return std::nullopt;
            }
            const auto result =
                isMusicalTimingField(heading)
                    ? stepMusicalTiming(*note, heading, steps, m, host_.timingUnit(grid()))
                    : stepInspectorField(*note, heading, steps, m);
            return std::holds_alternative<std::string>(result) ? std::nullopt
                                                               : set(heading, result);
        }};
    return editor_.run(command, host_, hooks);
}

std::optional<std::string> PianoRollView::setNote(const std::string& heading,
                                                  const core::Event& changed)
{
    // The current note made what an inspector field says, unless it would run into another of
    // its pitch or past the song's end; the cursor stays on it.
    const auto index = currentNote();
    const auto id = *trackId();
    const auto& song = document_.song();
    const auto old = song.events(id)[*index];
    if (changed == old)
    {
        return std::nullopt;
    }
    if (changed.tick > lastTick())
    {
        return "That's past the song's end";
    }
    if (auto why = overlap(song.events(id), *index, changed, meter()))
    {
        return why;
    }
    auto field = heading.starts_with("Ends") ? "end" + heading.substr(4) : heading;
    field[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(field[0])));
    editEvents("Set " + field + " of " + core::pitchName(std::get<core::Note>(old.data).pitch) +
                   " in " + trackName(),
               [&](core::EventList& events)
               {
                   events.erase(*index);
                   events.insert(changed);
               });
    moveTo(time_ + changed.tick - old.tick);
    moveToPitch(std::get<core::Note>(changed.data).pitch);
    return std::nullopt;
}

bool PianoRollView::changeSelected(std::string_view verb,
                                   const std::function<bool(core::Event&)>& change)
{
    const auto id = *trackId();
    const auto& song = document_.song();
    const auto& events = song.events(id);
    const auto selection = host_.selection();
    std::vector<std::size_t> which;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto* note = std::get_if<core::Note>(&events[i].data);
        if (note != nullptr && takes(events[i].tick, note->pitch) &&
            (!selection || !selection->filter || selection->movedNotes ||
             core::matches(*selection->filter, events[i].data, song.ppqn())))
        {
            which.push_back(i);
        }
    }
    if (which.empty())
    {
        host_.refuse("No notes in the selection", MessageKind::Warning);
        return true;
    }
    std::vector<std::pair<core::Event, core::Event>> moves; // each as it was and would be
    for (const auto i : which)
    {
        auto changed = events[i];
        if (!change(changed) || changed.tick < 0 || changed.tick > lastTick())
        {
            host_.refuse("Not all the selected notes can go there", MessageKind::Warning);
            return true;
        }
        moves.emplace_back(events[i], changed);
    }
    const auto pitchOf = [](const core::Event& e) { return std::get<core::Note>(e.data).pitch; };
    const auto& [first, firstTo] = moves.front();
    const auto by = firstTo.tick - first.tick;
    const auto up = int{pitchOf(firstTo)} - int{pitchOf(first)};
    if (std::ranges::all_of(moves, [](const auto& m) { return m.first == m.second; }))
    {
        return true;
    }
    // Each in turn, those ahead first, so none runs into one still to move out of its way.
    std::ranges::sort(moves,
                      [&](const auto& a, const auto& b)
                      {
                          if (by != 0 && a.first.tick != b.first.tick)
                          {
                              return by > 0 ? a.first.tick > b.first.tick
                                            : a.first.tick < b.first.tick;
                          }
                          return up > 0 ? pitchOf(a.first) > pitchOf(b.first)
                                        : pitchOf(a.first) < pitchOf(b.first);
                      });
    auto working = events;
    for (const auto& [was, to] : moves)
    {
        const auto at = std::ranges::find(working.all(), was) - working.all().begin();
        working.erase(static_cast<std::size_t>(at));
        if (auto why = overlap(working, std::nullopt, to, meter()))
        {
            host_.refuse(std::move(*why), MessageKind::Warning);
            return true;
        }
        working.insert(to);
    }
    const auto count = moves.size();
    editEvents(
        std::string{verb} + " " + std::to_string(count) + (count == 1 ? " note" : " notes") +
            " in " + trackName(),
        [&](core::EventList& list) { list = working; }, "selected-" + std::string{verb});
    moveTo(time_ + by);
    moveToPitch(std::clamp(int{pitch_} + up, 0, 127));
    std::vector<Selection::NotePosition> notes;
    notes.reserve(moves.size());
    for (const auto& [was, to] : moves)
    {
        notes.emplace_back(to.tick, pitchOf(to));
    }
    host_.moveSelection(by, up, std::move(notes));
    return true;
}

bool PianoRollView::changeNote(std::string_view verb,
                               const std::function<bool(core::Event&)>& change)
{
    if (selecting())
    {
        return changeSelected(verb, change);
    }
    const auto index = noteHere(verb == "Move" || verb == "Nudge" ? "move" : "change");
    if (!index)
    {
        return true;
    }
    const auto id = *trackId();
    const auto& song = document_.song();
    const auto old = song.events(id)[*index];
    auto changed = old;
    if (!change(changed) || changed == old || changed.tick < 0 || changed.tick > lastTick())
    {
        return true;
    }
    if (auto why = overlap(song.events(id), *index, changed, meter()))
    {
        host_.refuse(std::move(*why), MessageKind::Warning);
        return true;
    }
    const auto pitch = std::get<core::Note>(changed.data).pitch;
    editEvents(std::string{verb} + " " + core::pitchName(std::get<core::Note>(old.data).pitch) +
                   " in " + trackName(),
               [&](core::EventList& events)
               {
                   events.erase(*index);
                   events.insert(changed);
               });
    // The cursor at the same place in the note.
    moveTo(time_ + changed.tick - old.tick);
    moveToPitch(pitch);
    return true;
}

core::Tick PianoRollView::nudged(core::Tick tick, int nudges) const
{
    const auto& tempo = document_.song().tempo();
    const auto to = tempo.toTickExtended(tempo.toTime(tick) + nudge_ * nudges);
    if (nudges > 0)
    {
        return std::max(to, tick + 1);
    }
    return nudges < 0 ? std::min(to, tick - 1) : tick;
}

core::Tick PianoRollView::gridAfter(core::Tick tick) const
{
    const auto& m = meter();
    const auto bar = m.toBarBeatTick(tick).bar;
    const auto start = m.barStart(bar);
    const auto g = gridTicks();
    return std::min(start + ((tick - start) / g + 1) * g, m.barStart(bar + 1));
}

core::Tick PianoRollView::gridBefore(core::Tick tick) const
{
    if (tick <= 0)
    {
        return 0;
    }
    const auto& m = meter();
    const auto start = m.barStart(m.toBarBeatTick(tick - 1).bar);
    return start + (tick - 1 - start) / gridTicks() * gridTicks();
}

core::Tick PianoRollView::viewEnd(core::Tick start) const
{
    if (viewLength_)
    {
        return start + *viewLength_;
    }
    const auto& m = meter();
    return std::max(sameInBar(m, start, pageBars(), offsetInBar(m, start)), start + 1);
}

core::Tick PianoRollView::pageStart(core::Tick tick) const
{
    // Pages of whole bars, counted from the section's first bar, in the track's bars.
    const auto& song = document_.song();
    const auto& m = meter();
    const auto section = song.sectionAt(tick);
    const auto sectionStart = song.sectionStart(section);
    const auto first = m.toBarBeatTick(sectionStart).bar;
    const auto bar = std::max(m.toBarBeatTick(tick).bar, first);
    return std::max(m.barStart(first + (bar - first) / pageBars() * pageBars()), sectionStart);
}

void PianoRollView::align()
{
    viewLength_.reset();
    viewStart_ = pageStart(time_);
}

void PianoRollView::turn(core::Tick tick)
{
    const auto& m = meter();
    for (;;)
    {
        const auto end = viewEnd(viewStart_);
        if (tick >= viewStart_ && tick < end)
        {
            return;
        }
        viewLength_.reset();
        if (tick >= end)
        {
            viewStart_ = end;
        }
        else
        {
            const auto back = sameInBar(m, viewStart_, -pageBars(), offsetInBar(m, viewStart_));
            viewStart_ = back < viewStart_ ? back : 0; // not before bar 1
        }
    }
}

void PianoRollView::zoomTo(std::size_t size)
{
    openedBars_.reset(); // pages back in line with the section
    // The cursor kept at the same place across the view, so zooming back returns to the same
    // view; as far as the view can't start before bar 1.
    const auto across = time_ - viewStart_;
    const auto width = viewEnd(viewStart_) - viewStart_;
    viewLength_.reset();
    pageSize_ = size;
    // The new view's length depends on where it starts, over bars of different lengths; a
    // few rounds settle it.
    auto start = time_;
    for (int round = 0; round < 3; ++round)
    {
        const auto length = viewEnd(start) - start;
        start = std::max<core::Tick>(time_ - across * length / width, 0);
    }
    viewStart_ = start;
    turn(time_);
}

PianoRollView::Page PianoRollView::page() const
{
    const auto& m = meter();
    const auto end = viewEnd(viewStart_);
    return {.firstBar = m.toBarBeatTick(viewStart_).bar,
            .lastBar = m.toBarBeatTick(end - 1).bar,
            .start = viewStart_,
            .end = end,
            .section = document_.song().sectionAt(time_)};
}

int PianoRollView::noteRows() const
{
    return std::max((layout_.main.height - (kRulerRows + kLaneRows) * cell_.height) / cell_.height,
                    1);
}

void PianoRollView::follow()
{
    const int rows = noteRows();
    int top = std::clamp(int{topPitch_}, std::min(rows - 1, 127), 127);
    top = std::max(top, int{pitch_});
    top = std::min(top, pitch_ + rows - 1);
    topPitch_ = static_cast<std::uint8_t>(top);
}

void PianoRollView::centre()
{
    const int rows = noteRows();
    topPitch_ =
        static_cast<std::uint8_t>(std::clamp(pitch_ + rows / 2, std::min(rows - 1, 127), 127));
    follow();
}

std::uint8_t PianoRollView::middlePitch() const
{
    const auto id = trackId();
    if (!id)
    {
        return 60;
    }
    std::optional<std::uint8_t> low;
    std::optional<std::uint8_t> high;
    for (const auto& event : document_.song().events(*id).all())
    {
        if (const auto* note = std::get_if<core::Note>(&event.data))
        {
            low = std::min(low.value_or(note->pitch), note->pitch);
            high = std::max(high.value_or(note->pitch), note->pitch);
        }
    }
    return low ? static_cast<std::uint8_t>((*low + *high) / 2) : std::uint8_t{60};
}

void PianoRollView::moveTo(core::Tick tick)
{
    time_ = std::clamp<core::Tick>(tick, 0, lastTick());
    turn(time_);
}

void PianoRollView::moveToPitch(int pitch)
{
    pitch_ = static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
    follow();
}

void PianoRollView::showTrack(std::size_t track, std::optional<std::uint8_t> pitch)
{
    openedBars_.reset(); // another track's pages
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return;
    }
    if (const auto id = trackId(); id && opened_)
    {
        pitches_[*id] = pitch_;
        pageSizes_[*id] = pageSize_;
    }
    opened_ = true;
    track_ = std::min(track, tracks.size() - 1);
    const auto id = tracks[track_]->id();
    const auto size = pageSizes_.find(id);
    pageSize_ = size != pageSizes_.end() ? size->second : kDefaultPageSize;
    const auto known = pitches_.find(id);
    pitch_ = pitch ? *pitch : known != pitches_.end() ? known->second : middlePitch();
    // The view centred on the track's notes, as far as the cursor's pitch stays in it.
    const int rows = noteRows();
    topPitch_ = static_cast<std::uint8_t>(
        std::clamp(middlePitch() + rows / 2, std::min(rows - 1, 127), 127));
    follow();
}

std::optional<std::size_t> PianoRollView::currentNote() const
{
    return noteAt(time_);
}

std::optional<std::size_t> PianoRollView::noteAt(core::Tick tick) const
{
    const auto id = trackId();
    if (!id)
    {
        return std::nullopt;
    }
    const auto events = document_.song().events(*id).all();
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < events.size() && events[i].tick <= tick; ++i)
    {
        const auto* note = std::get_if<core::Note>(&events[i].data);
        if (note != nullptr && note->pitch == pitch_ &&
            tick < events[i].tick + std::max<core::Tick>(note->duration, 1))
        {
            found = i;
        }
    }
    return found;
}

core::Tick PianoRollView::stepLeft(core::Tick tick) const
{
    // Off a note at the cursor's pitch in one step, to the grid step before its start, rather
    // than a step at a time through it (decided October 2026, after use); but a grid step at a
    // time while selecting, where each step changes what's selected.
    if (const auto index = selecting() ? std::nullopt : noteAt(tick))
    {
        return gridBefore(document_.song().events(*trackId())[*index].tick);
    }
    return gridBefore(tick);
}

core::Tick PianoRollView::stepRight(core::Tick tick) const
{
    // And to the first grid step at or after its end.
    if (const auto index = selecting() ? std::nullopt : noteAt(tick))
    {
        const auto& event = document_.song().events(*trackId())[*index];
        const auto end =
            event.tick + std::max<core::Tick>(std::get<core::Note>(event.data).duration, 1);
        return std::max(gridAfter(end - 1), gridAfter(tick));
    }
    return gridAfter(tick);
}

std::pair<core::Tick, core::Tick> PianoRollView::cursorSpan() const
{
    const auto& song = document_.song();
    if (const auto index = currentNote())
    {
        const auto& event = song.events(*trackId())[*index];
        const auto& note = std::get<core::Note>(event.data);
        return {event.tick, event.tick + std::max<core::Tick>(note.duration, 1)};
    }
    const auto tracks = song.tracks();
    const auto length =
        tracks.empty() ? grid() : tracks[std::min(track_, tracks.size() - 1)]->defaultLength();
    return {time_, time_ + std::max<core::Tick>(length.toTicks(song.ppqn()), 1)};
}

std::vector<core::Tick> PianoRollView::chordStarts() const
{
    std::vector<core::Tick> starts;
    const auto id = trackId();
    if (!id)
    {
        return starts;
    }
    const auto& song = document_.song();
    std::optional<core::Tick> first;
    for (const auto& event : song.events(*id).all())
    {
        if (event.isNote() && (!first || !together(song, *first, event.tick, chordWindow_)))
        {
            starts.push_back(event.tick);
            first = event.tick;
        }
    }
    return starts;
}

std::optional<bool> PianoRollView::run(std::string_view command, int count)
{
    const bool keepsBarOffset = command == "cursor-bar-next" || command == "cursor-bar-previous" ||
                                command == "cursor-bar-next-scroll" ||
                                command == "cursor-bar-previous-scroll";
    if (!keepsBarOffset)
    {
        barOffset_.reset();
    }
    const int n = std::max(count, 1);
    const auto& song = document_.song();
    const auto& m = meter();
    // Goes to a time, unless it's past the song's end, as a Vim motion fails there.
    const auto go = [&](core::Tick to)
    {
        if (to > lastTick())
        {
            return false;
        }
        moveTo(to);
        return true;
    };
    const auto scroll = [&](int by) // down if positive
    {
        topPitch_ = static_cast<std::uint8_t>(std::clamp(topPitch_ - by, 0, 127));
        moveToPitch(pitch_ - by);
    };

    // Moves the cursor and the view together, so the cursor stays put on screen, the view
    // keeping its length so its scale stays put too; at the song's start the view stops and
    // the cursor goes on.
    const auto scrollTo = [&](core::Tick to)
    {
        if (!viewLength_)
        {
            viewLength_ = viewEnd(viewStart_) - viewStart_;
        }
        viewStart_ = std::max<core::Tick>(viewStart_ + (to - time_), 0);
        moveTo(to);
    };

    if (command == "cursor-left" || command == "cursor-left-scroll")
    {
        auto tick = time_;
        for (int i = 0; i < n; ++i)
        {
            tick = stepLeft(tick);
        }
        if (command == "cursor-left-scroll")
        {
            scrollTo(tick);
        }
        else
        {
            moveTo(tick);
        }
    }
    else if (command == "cursor-right" || command == "cursor-right-scroll")
    {
        auto tick = time_;
        for (int i = 0; i < n && stepRight(tick) <= lastTick(); ++i)
        {
            tick = stepRight(tick);
        }
        if (command == "cursor-right-scroll")
        {
            scrollTo(tick);
        }
        else
        {
            moveTo(tick);
        }
    }
    else if (command == "cursor-bar-next-scroll" || command == "cursor-bar-previous-scroll")
    {
        // A bar on or back, keeping the cursor's place in the bar, the view moving with it.
        const auto by = command == "cursor-bar-next-scroll" ? n : -n;
        const auto offset = barOffset_.value_or(offsetInBar(m, time_));
        const auto to = sameInBar(m, time_, by, offset);
        barOffset_ = offset;
        if (to > lastTick())
        {
            return false;
        }
        scrollTo(to);
    }
    else if (keepsBarOffset)
    {
        const auto offset = barOffset_.value_or(offsetInBar(m, time_));
        const bool moved = go(sameInBar(m, time_, command == "cursor-bar-next" ? n : -n, offset));
        barOffset_ = offset;
        return moved;
    }
    else if (command == "cursor-bar-line-next")
    {
        return go(barLineAfter(m, time_, n));
    }
    else if (command == "cursor-bar-line-previous")
    {
        moveTo(barLineBefore(m, time_, n));
    }
    else if (command == "cursor-first" || command == "cursor-last")
    {
        // The start of the bar, or its last grid step.
        const auto bar = m.toBarBeatTick(time_).bar;
        const auto start = m.barStart(bar);
        const auto end = std::min(m.barStart(bar + 1), song.end());
        moveTo(command == "cursor-first"
                   ? start
                   : start + std::max<core::Tick>(end - 1 - start, 0) / gridTicks() * gridTicks());
    }
    else if (command == "go-first" || command == "go-last")
    {
        // The start or the last grid step of the track, or bar {count}.
        const auto last = m.toBarBeatTick(lastTick()).bar;
        if (count > 0)
        {
            moveTo(m.barStart(std::clamp<std::int64_t>(count, 1, last)));
        }
        else
        {
            moveTo(command == "go-first" ? 0 : gridBefore(song.end()));
        }
    }
    else if (command == "cursor-up" || command == "cursor-down")
    {
        moveToPitch(pitch_ + (command == "cursor-up" ? n : -n));
    }
    else if (command == "octave-up" || command == "octave-down")
    {
        moveToPitch(pitch_ + 12 * (command == "octave-up" ? n : -n));
    }
    else if (command == "cursor-up-scroll" || command == "cursor-down-scroll")
    {
        scroll(command == "cursor-down-scroll" ? n : -n);
    }
    else if (command == "scroll-page-left" || command == "scroll-page-right")
    {
        // By the view's length, as turning the page does, the cursor staying put on screen;
        // no further than the song's start or the last step.
        const auto length = viewEnd(viewStart_) - viewStart_;
        const auto by = length * (command == "scroll-page-left" ? -n : n);
        scrollTo(std::clamp<core::Tick>(time_ + by, 0, std::max(lastTick(), time_)));
    }
    else if (command.starts_with("scroll-half-") || command.starts_with("scroll-page-"))
    {
        const int rows = noteRows();
        const int page = std::max(command.starts_with("scroll-half-") ? rows / 2 : rows - 2, 1);
        scroll(n * page * (command.ends_with("down") ? 1 : -1));
    }
    else if (command == "scroll-centre")
    {
        // In time and in pitch.
        viewStart_ = std::max<core::Tick>(time_ - (viewEnd(viewStart_) - viewStart_) / 2, 0);
        turn(time_);
        centre();
    }
    else if (command.starts_with("cursor-find-") || command.starts_with("cursor-match-"))
    {
        // The next or previous match of a find (fE) or a search (/C1), in time and then low to
        // high, a search going round past the ends.
        const bool search = command.starts_with("cursor-match-");
        const auto query = search ? host_.searchQuery() : host_.findQuery();
        const bool forward = command.ends_with("-next");
        const auto to =
            query ? nthPlace(matchPlaces(*query), std::pair{time_, pitch_}, forward, n, search)
                  : std::nullopt;
        if (!to)
        {
            return false;
        }
        if (to->second)
        {
            host_.searchWrapped(forward);
        }
        moveTo(to->first.first);
        moveToPitch(to->first.second);
    }
    else if (command == "cursor-note-next" || command == "cursor-note-previous")
    {
        // Every note's start, in time and then low to high, landing on its pitch.
        const auto id = trackId();
        if (!id)
        {
            return false;
        }
        std::vector<std::pair<core::Tick, std::uint8_t>> starts;
        for (const auto& event : song.events(*id).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                starts.emplace_back(event.tick, note->pitch);
            }
        }
        std::ranges::sort(starts);
        const std::pair here{time_, pitch_};
        std::ptrdiff_t at = 0;
        if (command == "cursor-note-next")
        {
            at = std::ranges::upper_bound(starts, here) - starts.begin() + n - 1;
        }
        else
        {
            at = std::ranges::lower_bound(starts, here) - starts.begin() - n;
        }
        if (at < 0 || at >= static_cast<std::ptrdiff_t>(starts.size()))
        {
            return false;
        }
        const auto& [tick, notePitch] = starts[static_cast<std::size_t>(at)];
        moveTo(tick);
        moveToPitch(notePitch);
    }
    else if (command == "cursor-chord-next" || command == "cursor-chord-previous")
    {
        // The start of a chord, on its lowest note.
        const auto starts = chordStarts();
        const auto to = command == "cursor-chord-next" ? nextOf(starts, time_, n)
                                                       : previousOf(starts, time_, n);
        if (!to)
        {
            return false;
        }
        moveTo(*to);
        std::optional<std::uint8_t> lowest;
        for (const auto& event : song.events(*trackId()).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && event.tick >= *to &&
                together(song, *to, event.tick, chordWindow_))
            {
                lowest = std::min(lowest.value_or(note->pitch), note->pitch);
            }
        }
        moveToPitch(lowest.value_or(pitch_));
    }
    else if (command == "cursor-mark")
    {
        const auto at = host_.jumpTime();
        if (!at)
        {
            return false;
        }
        moveTo(*at);
    }
    else if (command.starts_with("cursor-section-") || command.starts_with("cursor-label-"))
    {
        std::vector<core::Tick> ticks;
        if (command.starts_with("cursor-section-"))
        {
            for (std::size_t i = 0; i < song.sections().size(); ++i)
            {
                ticks.push_back(song.sectionStart(i));
            }
        }
        else
        {
            for (const auto& label : song.labels())
            {
                ticks.push_back(label.tick);
            }
        }
        const auto to =
            command.ends_with("-next") ? nextOf(ticks, time_, n) : previousOf(ticks, time_, n);
        if (!to)
        {
            return false;
        }
        moveTo(*to);
    }
    else if (command == "track-next" || command == "track-previous")
    {
        // The tracks in turn, with no conductor's here.
        const auto tracks = song.tracks().size();
        if (tracks == 0)
        {
            return false;
        }
        const auto steps = static_cast<std::size_t>(n) % tracks;
        const auto to = command == "track-next" ? (track_ + steps) % tracks
                                                : (track_ + tracks - steps) % tracks;
        showTrack(to, std::nullopt);
        align();
    }
    else if (command == "zoom-in" || command == "zoom-out")
    {
        // Fewer bars in view, or more.
        const auto last = static_cast<int>(kPageSizes.size()) - 1;
        zoomTo(static_cast<std::size_t>(
            std::clamp(static_cast<int>(pageSize_) + (command == "zoom-in" ? -n : n), 0, last)));
    }
    else if (command == "zoom-reset")
    {
        // The default number of bars, as whole bars, the page holding the cursor.
        pageSize_ = kDefaultPageSize;
        openedBars_.reset();
        align();
    }
    else if (command == "grid-coarser" || command == "grid-finer")
    {
        // Doubling or halving, keeping triplets, as far as the limits.
        auto to = grid();
        for (int i = 0; i < n; ++i)
        {
            const auto next = command == "grid-coarser"
                                  ? core::NoteFraction{to.numerator() * 2, to.denominator()}
                                  : core::NoteFraction{to.numerator(), to.denominator() * 2};
            if (straight(next) > kCoarsestGrid || straight(next) < kFinestGrid)
            {
                break;
            }
            to = next;
        }
        if (to == grid())
        {
            return false;
        }
        setGrid(to);
    }
    else if (command == "grid-triplets")
    {
        setGrid(isTriplet(grid()) ? straight(grid()) : grid().triplet());
    }
    else if (command == "note-add" || command == "note-add-advance")
    {
        return addNote(count, command == "note-add-advance");
    }
    else if (command == "event-delete")
    {
        return operate("operator-delete", "object-note", count);
    }
    else if (command == "put-merge" || command == "put-insert" || command == "put-ending")
    {
        return put(command == "put-insert", command == "put-ending");
    }
    else if (command == "put-merge-at-pitch" || command == "put-insert-at-pitch")
    {
        return put(command == "put-insert-at-pitch", false, true);
    }
    else if (command == "inspector-edit")
    {
        if (!noteHere("edit"))
        {
            return true;
        }
        if (editor_.open(inspection()))
        {
            host_.startTyping();
        }
    }
    else if (command == "move-left" || command == "move-right")
    {
        // To the grid steps either side, counted from each bar line, as h and l go.
        return changeNote("Move",
                          [&](core::Event& event)
                          {
                              for (int i = 0; i < n; ++i)
                              {
                                  event.tick = command == "move-left" ? gridBefore(event.tick)
                                                                      : gridAfter(event.tick);
                              }
                              return true;
                          });
    }
    else if (command == "move-left-fine" || command == "move-right-fine")
    {
        return changeNote("Nudge",
                          [&](core::Event& event)
                          {
                              const auto to =
                                  nudged(event.tick, command == "move-left-fine" ? -n : n);
                              // A batch must refuse a start before zero, rather than change
                              // its spacing by stopping only some notes there.
                              event.tick = selecting() ? to : std::max<core::Tick>(to, 0);
                              return true;
                          });
    }
    else if (command == "move-bar-next" || command == "move-bar-previous")
    {
        // A bar on or back, at the same place in the bar.
        return changeNote("Move",
                          [&](core::Event& event)
                          {
                              event.tick =
                                  sameInBar(m, event.tick, command == "move-bar-next" ? n : -n,
                                            offsetInBar(m, event.tick));
                              return true;
                          });
    }
    else if (command.starts_with("move-up") || command.starts_with("move-down") ||
             command.starts_with("move-octave-"))
    {
        const auto step = command.starts_with("move-octave-") ? 12 : 1;
        const auto by = command.ends_with("up") ? n * step : -n * step;
        return changeNote("Move",
                          [&](core::Event& event)
                          {
                              auto& note = std::get<core::Note>(event.data);
                              const auto to = note.pitch + by;
                              if (to < 0 || to > 127)
                              {
                                  return false;
                              }
                              note.pitch = static_cast<std::uint8_t>(to);
                              return true;
                          });
    }
    else if (command == "note-lengthen" || command == "note-shorten")
    {
        // The end to the grid steps either side, never shorter than a grid step.
        const bool longer = command == "note-lengthen";
        return changeNote(longer ? "Lengthen" : "Shorten",
                          [&](core::Event& event)
                          {
                              auto& note = std::get<core::Note>(event.data);
                              auto end = event.tick + note.duration;
                              for (int i = 0; i < n; ++i)
                              {
                                  end = longer ? gridAfter(end) : gridBefore(end);
                              }
                              end = std::max(end, event.tick + gridTicks());
                              if (!longer && end >= event.tick + note.duration)
                              {
                                  host_.refuse("A note can't be shortened below a grid step",
                                               MessageKind::Warning);
                                  return false;
                              }
                              note.duration = end - event.tick;
                              return true;
                          });
    }
    else if (command == "note-lengthen-fine" || command == "note-shorten-fine")
    {
        // The end a nudge on or back, never shorter than a nudge.
        const bool longer = command == "note-lengthen-fine";
        return changeNote(
            longer ? "Lengthen" : "Shorten",
            [&](core::Event& event)
            {
                auto& note = std::get<core::Note>(event.data);
                const auto end = std::max(nudged(event.tick + note.duration, longer ? n : -n),
                                          nudged(event.tick, 1));
                if (!longer && end >= event.tick + note.duration)
                {
                    host_.refuse("A note can't be shortened below a nudge", MessageKind::Warning);
                    return false;
                }
                note.duration = end - event.tick;
                return true;
            });
    }
    else if (command == "velocity-up" || command == "velocity-down")
    {
        // The current note's velocity, by 1 a press; steps one after another on a note undo
        // together, as the grid's do.
        const auto index = noteHere("step");
        if (!index)
        {
            return true;
        }
        const auto id = *trackId();
        auto event = song.events(id)[*index];
        auto& note = std::get<core::Note>(event.data);
        const auto to = static_cast<std::uint8_t>(
            std::clamp(note.velocity + (command == "velocity-up" ? n : -n), 1, 127));
        if (to == note.velocity)
        {
            return false;
        }
        const auto key = "velocity " + std::to_string(id.value) + " " + std::to_string(event.tick) +
                         " " + std::to_string(note.pitch);
        const auto title = "Velocity " + std::to_string(to) + " for " +
                           core::pitchName(note.pitch) + " in " + trackName();
        note.velocity = to;
        editEvents(
            title,
            [&](core::EventList& events)
            {
                events.erase(*index);
                events.insert(event);
            },
            key);
    }
    else if (command == "default-note-set")
    {
        // The current note's length and velocity, as the track's default note.
        const auto index = currentNote();
        if (!index)
        {
            host_.refuse("No note here to take as the default", MessageKind::Warning);
            return true;
        }
        const auto note = std::get<core::Note>(song.events(*trackId())[*index].data);
        const auto length =
            core::NoteFraction{std::max<core::Tick>(note.duration, 1), song.ppqn().whole()};
        changeTrack("Default note " + core::toString(length) + " v" +
                        std::to_string(note.velocity) + " in " + trackName(),
                    {},
                    [&](core::Track& track)
                    {
                        track.noteLength = length;
                        track.noteVelocity = note.velocity;
                    });
        host_.setMessage("Default note: " + core::toString(length) + ", velocity " +
                             std::to_string(note.velocity),
                         MessageKind::Information);
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

std::optional<CommandResult> PianoRollView::runLine(std::string_view command,
                                                    std::string_view arguments)
{
    if (command != "grid" && command != "len")
    {
        return std::nullopt;
    }
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return CommandResult::error("There's no track");
    }
    const auto& track = *tracks[std::min(track_, tracks.size() - 1)];
    if (arguments.empty())
    {
        return CommandResult{false,
                             command == "grid" ? "Grid " + core::toString(track.grid)
                                               : "Default note " + noteText(track),
                             false};
    }
    if (command == "len" && arguments == "grid")
    {
        changeTrack("Default length grid in " + trackName(), {},
                    [](core::Track& t) { t.noteLength.reset(); });
        return CommandResult{true};
    }
    const auto length = core::parseNoteFraction(arguments);
    if (!length)
    {
        return CommandResult::error(std::string{arguments} +
                                    " isn't a note length, such as 1/16, 1/8. or 1/8t");
    }
    if (command == "grid")
    {
        if (straight(*length) > kCoarsestGrid || straight(*length) < kFinestGrid)
        {
            return CommandResult::error("The grid can be from 1/64 to 1/1");
        }
        setGrid(*length);
    }
    else
    {
        changeTrack("Default length " + core::toString(*length) + " in " + trackName(), {},
                    [&](core::Track& t) { t.noteLength = *length; });
    }
    return CommandResult{true};
}

bool PianoRollView::operate(std::string_view op, std::string_view motion, int count)
{
    const auto id = trackId();
    if (!id)
    {
        return false;
    }
    if (motion == "operator-line" || motion == "object-bar-contents" || motion == "object-bar")
    {
        return operateOnBars(op, count, motion == "object-bar");
    }
    if (motion == "visual" || motion == "visual-time")
    {
        // A selection: its notes, put at the cursor's time rather than a bar's; with D its
        // time too, every event there, the gap closing as far as the section's end.
        const auto span = selected();
        if (!span)
        {
            return false;
        }
        const auto [from, to] = *span;
        if (motion == "visual")
        {
            std::vector<std::size_t> taken;
            const auto& events = document_.song().events(*trackId());
            for (const auto i : notesIn(from, to))
            {
                if (takes(events[i].tick, std::get<core::Note>(events[i].data).pitch))
                {
                    taken.push_back(i);
                }
            }
            return operateOn(op, taken, from, to - from);
        }
        if (selectedPitches())
        {
            host_.refuse("D takes time out of the whole track, not some of its pitches; v "
                         "selects every pitch",
                         MessageKind::Information);
            return true;
        }
        const auto& song = document_.song();
        const auto end = std::min(to, sectionEndAt(song, from));
        document_.edit("Delete " + *selectionStatus() + ", closing the gap", place(),
                       [&](core::Song& s)
                       { host_.eventRegister() = rippleDelete(s, track_, track_, from, end); });
        refresh();
        moveTo(from);
        return true;
    }
    if (motion == "object-track" || motion == "object-song")
    {
        // it, and ie, the song as far as this track goes: the track's notes, from the start, taken
        // as bars.
        const auto end = document_.song().end();
        const bool done = operateOn(op, notesIn(0, end), 0, end);
        host_.eventRegister().bars = true;
        return done;
    }
    if (isSectionObject(motion))
    {
        // is: this section's notes and as many sections' after it as the count says less one,
        // taken as bars.
        const auto range = sectionsFrom(document_.song(), time_, count);
        const bool done =
            operateOn(op, notesIn(range.from, range.to), range.from, range.to - range.from);
        host_.eventRegister().bars = true;
        return done;
    }
    if (motion == "object-chord")
    {
        const auto chord = chordHere(count);
        if (chord.empty())
        {
            return true;
        }
        const auto& events = document_.song().events(*id);
        core::Tick end = 0;
        for (const auto i : chord)
        {
            end = std::max(end, events[i].tick + std::get<core::Note>(events[i].data).duration);
        }
        const auto from = events[chord.front()].tick;
        return operateOn(op, chord, from, std::max<core::Tick>(end - from, 1));
    }
    if (motion == "object-note")
    {
        // in, x, Y: the current note (a count waits, as o's does).
        const auto index = noteHere(op == "operator-yank" ? "yank" : "delete");
        if (!index)
        {
            return true;
        }
        const auto& event = document_.song().events(*id)[*index];
        return operateOn(op, {*index}, event.tick,
                         std::max<core::Tick>(std::get<core::Note>(event.data).duration, 1));
    }
    if (acrossPitches(motion))
    {
        return true;
    }
    const auto span = timeSpan(motion, count);
    return span && operateOn(op, notesIn(span->first, span->second), span->first,
                             span->second - span->first);
}

bool PianoRollView::acrossPitches(std::string_view motion) const
{
    const bool across = motion == "cursor-up" || motion == "cursor-down" || motion == "octave-up" ||
                        motion == "octave-down";
    if (across)
    {
        host_.refuse("Ranges across pitches come with Visual mode", MessageKind::Information);
    }
    return across;
}

std::optional<std::pair<core::Tick, core::Tick>> PianoRollView::timeSpan(std::string_view motion,
                                                                         int count)
{
    // Where the motion goes in time, without moving the cursor or the view.
    const auto time = time_;
    const auto pitch = pitch_;
    const auto top = topPitch_;
    const auto start = viewStart_;
    const auto length = viewLength_;
    const auto moved = run(motion, count);
    const auto landed = time_;
    const auto landedPitch = pitch_;
    time_ = time;
    pitch_ = pitch;
    topPitch_ = top;
    viewStart_ = start;
    viewLength_ = length;
    if (!moved || !*moved)
    {
        return std::nullopt;
    }
    // From the cursor up to where it lands, as Vim's character motions go; with G and gg, the
    // grid step landed on too.
    const bool inclusive = motion == "go-first" || motion == "go-last" || motion == "cursor-last";
    const auto from = std::min(time, landed);
    auto to = std::max(time, landed);
    if (inclusive)
    {
        to = gridAfter(to);
    }
    // A find takes the note it finds too, to its end, as Vim's f does the character (dfE).
    if (motion == "cursor-find-next")
    {
        for (const auto& event : document_.song().events(*trackId()).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && event.tick == landed && note->pitch == landedPitch)
            {
                to = std::max(to, landed + note->duration);
            }
        }
    }
    if (from == to)
    {
        return std::nullopt;
    }
    return std::pair{from, to};
}

std::optional<OperatorTarget> PianoRollView::target(std::string_view motion, int count)
{
    // The notes a range or object takes, at every pitch, and with them the controllers there,
    // which follow quantised notes as pedals do; only the notes move otherwise.
    const auto id = trackId();
    if (!id || acrossPitches(motion))
    {
        return std::nullopt;
    }
    const auto& song = document_.song();
    const auto& events = song.events(*id);
    OperatorTarget result{.parts = {{*id, {}}}, .what = {}, .notesOnly = true};
    auto& taken = result.parts.front().events;
    if (motion == "object-chord")
    {
        taken = chordHere(count);
        if (taken.empty())
        {
            return std::nullopt;
        }
        result.what = std::to_string(taken.size()) + " notes in " + trackName();
        if (taken.size() == 1)
        {
            result.what = core::pitchName(std::get<core::Note>(events[taken.front()].data).pitch) +
                          " in " + trackName();
        }
        return result;
    }
    if (motion == "object-note")
    {
        const auto index = noteHere("change");
        if (!index)
        {
            return std::nullopt;
        }
        taken.push_back(*index);
        result.what =
            core::pitchName(std::get<core::Note>(events[*index].data).pitch) + " in " + trackName();
        return result;
    }
    std::optional<std::pair<core::Tick, core::Tick>> span;
    if (motion == "operator-line" || motion == "object-bar-contents" || motion == "object-bar")
    {
        const auto& m = meter();
        const auto bar = m.toBarBeatTick(time_).bar;
        span =
            std::pair{m.barStart(bar), std::min(m.barStart(bar + std::max(count, 1)), song.end())};
    }
    else if (motion == "object-track" || motion == "object-song")
    {
        span = std::pair{core::Tick{0}, song.end()};
    }
    else if (motion == "visual")
    {
        span = selected();
    }
    else if (isSectionObject(motion))
    {
        // is, and as, which is is to the operators that don't take time out.
        const auto range = sectionsFrom(song, time_, count);
        span = std::pair{range.from, range.to};
    }
    else
    {
        span = timeSpan(motion, count);
    }
    if (!span)
    {
        return std::nullopt;
    }
    std::size_t notes = 0;
    std::optional<std::size_t> only;
    const auto pitches = motion == "visual" ? selectedPitches() : std::nullopt;
    const auto selection = host_.selection();
    const bool moved = motion == "visual" && selection && selection->movedNotes;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        // With V, only the notes at its pitches, not the controllers with them.
        const auto* note = std::get_if<core::Note>(&events[i].data);
        if ((pitches || moved) && (note == nullptr || !takes(events[i].tick, note->pitch)))
        {
            continue;
        }
        if (events[i].tick >= span->first && events[i].tick < span->second)
        {
            taken.push_back(i);
            if (events[i].isNote())
            {
                ++notes;
                only = i;
            }
        }
    }
    if (notes == 0)
    {
        host_.refuse("No notes there", MessageKind::Information);
        return std::nullopt;
    }
    result.what = (notes == 1 ? core::pitchName(std::get<core::Note>(events[*only].data).pitch)
                              : std::to_string(notes) + " notes") +
                  " in " + trackName();
    return result;
}

std::vector<std::size_t> PianoRollView::notesIn(core::Tick from, core::Tick to) const
{
    std::vector<std::size_t> notes;
    const auto& events = document_.song().events(*trackId());
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].isNote() && events[i].tick >= from && events[i].tick < to)
        {
            notes.push_back(i);
        }
    }
    return notes;
}

bool PianoRollView::operateOnBars(std::string_view op, int count, bool time)
{
    // The cursor's bar and as many after it as the count says less one: its notes, or as time
    // (ab) all the track's events there, controllers too, since what moves stays together;
    // deleted, the gap closes as far as the section's end. Taken as bars either way.
    const auto& song = document_.song();
    const auto& m = meter();
    const auto bar = m.toBarBeatTick(time_).bar;
    const auto from = m.barStart(bar);
    auto to = std::min(m.barStart(bar + std::max(count, 1)), song.end());
    if (!time)
    {
        const bool done = operateOn(op, notesIn(from, to), from, to - from);
        host_.eventRegister().bars = true;
        return done;
    }
    if (op == "operator-yank")
    {
        host_.eventRegister() = takeEvents(song, track_, track_, from, to);
        host_.eventRegister().bars = true;
        auto what = barsText(m, from, to);
        what[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(what[0])));
        host_.setMessage(what + " yanked (" +
                             eventCount(host_.eventRegister().tracks.front().size()) + ")",
                         MessageKind::Information);
        return true;
    }
    to = std::min(to, sectionEndAt(song, from));
    document_.edit("Delete " + barsText(m, from, to) + " in " + trackName() + ", closing the gap",
                   place(),
                   [&](core::Song& s)
                   {
                       host_.eventRegister() = rippleDelete(s, track_, track_, from, to);
                       host_.eventRegister().bars = true;
                   });
    refresh();
    moveTo(from);
    return true;
}

std::optional<std::string> PianoRollView::describeRange(std::string_view op,
                                                        std::string_view motion) const
{
    // The notes a range takes, at every pitch; a bar as time takes every event there.
    if (motion == "operator-line" || motion == "object-bar-contents")
    {
        return "this bar's notes";
    }
    if (motion == "object-bar")
    {
        // As time only for d and y; to the others it's ib.
        if (op != "operator-delete" && op != "operator-yank")
        {
            return std::nullopt;
        }
        return op == "operator-delete" ? "this bar, closing the gap"
                                       : "this bar as time, controllers too";
    }
    if (motion == "object-note")
    {
        return "this note";
    }
    if (motion == "object-chord")
    {
        return "this chord";
    }
    if (motion == "object-track")
    {
        return "this track's notes";
    }
    if (motion == "object-song")
    {
        return "the whole song, this track's notes";
    }
    if (motion == "object-section-contents")
    {
        return "this section's notes";
    }
    if (motion == "object-section")
    {
        return sectionDescription(op);
    }
    if (motion == "cursor-right")
    {
        return "to the next grid step";
    }
    if (motion == "cursor-left")
    {
        return "from the grid step before";
    }
    return describeTimeRange(motion);
}

Inspection PianoRollView::inspection() const
{
    // The current note, or on an empty spot the cursor's position and pitch.
    const auto& song = document_.song();
    const auto& m = meter();
    Inspection result;
    if (const auto index = currentNote())
    {
        const auto& event = song.events(*trackId())[*index];
        result.fields.push_back({"Position", positionText(m, event.tick)});
        addNoteFields(result, song, m, event.tick, std::get<core::Note>(event.data));
        markEditable(result);
        addMusicalTiming(result, event, m, host_.timingUnit(grid()));
        return result;
    }
    result.kind = "No note";
    result.fields.push_back({"Position", positionText(m, time_)});
    result.fields.push_back({"Pitch", pitchText(pitch_)});
    return result;
}

void PianoRollView::addRuler(Region& ruler, const TimeScale& scale, const Page& p) const
{
    const auto& song = document_.song();
    const auto& m = meter();
    const int h = cell_.height;
    const int lineWidth = hairline(cell_);
    const int width = ruler.bounds.width;
    const auto inView = [&](core::Tick tick) { return tick >= p.start && tick < p.end; };

    // The top row: each section a block across its span, neighbours in two shades as on the
    // arrangement's strip, named in the colour of its line from where it shows, so the name of a
    // section begun before the view stays at the left edge. Then the labels by name, one at a
    // section's start after the section's name ("Verse · fix the fill"). A name stops short of
    // the next. The names are drawn straight on the blocks, without a background of their own,
    // which would stand taller than the block.
    const int inset = std::max(h / 10, 1);
    const auto shade = [&](std::size_t section)
    { return section % 2 == 0 ? Role::Panel : Role::Bar; };
    struct Name
    {
        int x = 0;     // its line
        int right = 0; // as far as it can run: its section's end, for a section's name
        std::u32string text;
        Style style;
        std::u32string label = {}; // at a section's start, after its name
    };
    std::vector<Name> names;
    for (std::size_t i = 0; i < song.sections().size(); ++i)
    {
        const auto start = song.sectionStart(i);
        const auto finish = song.sectionStart(i + 1);
        if (finish <= p.start || start >= p.end)
        {
            continue;
        }
        const int x0 = start > p.start ? scale.toX(start) + lineWidth : 0;
        const int x1 = scale.toX(std::min(finish, p.end));
        if (x1 <= x0)
        {
            continue;
        }
        ruler.under.push_back({{x0, inset, x1 - x0, h - 2 * inset}, shade(i), 255});
        const auto labels = song.labels();
        const auto label = std::ranges::find(labels, start, &core::Label::tick);
        names.push_back({x0 - lineWidth,
                         x1,
                         decodeUtf8(song.sections()[i]->name()),
                         {Role::Label, Role::Background, kBold},
                         label != labels.end() && start >= p.start ? decodeUtf8(label->name)
                                                                   : std::u32string{}});
    }
    for (const auto& label : song.labels())
    {
        const auto section = song.sectionAt(label.tick);
        if (inView(label.tick) && label.tick != song.sectionStart(section))
        {
            names.push_back({scale.toX(label.tick),
                             width,
                             decodeUtf8(label.name),
                             {Role::Label, Role::Background, kPlain}});
        }
    }
    std::ranges::stable_sort(names, {}, &Name::x);
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto& name = names[i];
        const int textX = name.x + 2 * lineWidth;
        int right = name.right;
        if (i + 1 < names.size())
        {
            right = std::min(right, names[i + 1].x);
        }
        const auto own = name.text.size(); // the section's name, before a label's
        if (!name.label.empty())
        {
            name.text += U" \u00B7 " + name.label;
        }
        const int room = (right - textX) / cell_.width;
        if (static_cast<int>(name.text.size()) > room)
        {
            if (room < kMinSegmentCharacters + 1)
            {
                continue;
            }
            name.text.resize(static_cast<std::size_t>(room - 1));
            name.text += U'\u2026';
        }
        // A label after a section's name in a plain face, as labels are.
        const auto bold = std::min(own, name.text.size());
        ruler.text.push_back({textX, 0, name.text.substr(0, bold), name.style});
        if (bold < name.text.size())
        {
            ruler.text.push_back({textX + static_cast<int>(bold) * cell_.width,
                                  0,
                                  name.text.substr(bold),
                                  {Role::Label, Role::Background, kPlain}});
        }
    }

    // The second row, a row of their own (decided November 2026, as they and the names ran
    // into each other in one): the play-from marker, and the marks (ma) on this track or the
    // conductor, each its letter; one with a pitch is a dot at it too, among the notes. Marks at
    // one time go in the order of their dots from the top, those without one first.
    std::vector<RowSign> signs;
    if (const auto* player = host_.player(); player != nullptr && inView(player->marker()))
    {
        signs.push_back({scale.toX(player->marker()), h, U'\u25B6', Role::Accent, {}, false});
    }
    std::vector<const core::Mark*> marks;
    for (const auto& mark : song.marks())
    {
        if ((!mark.track || mark.track == trackId()) && inView(mark.tick))
        {
            marks.push_back(&mark);
        }
    }
    const auto height = [](const core::Mark* mark)
    { return mark->track && mark->pitch != core::Mark::kNoPitch ? -mark->pitch : -128; };
    std::ranges::sort(marks, [&](const core::Mark* a, const core::Mark* b)
                      { return std::pair{a->tick, height(a)} < std::pair{b->tick, height(b)}; });
    for (const auto* mark : marks)
    {
        signs.push_back(
            {scale.toX(mark->tick) + 2 * lineWidth, h, static_cast<char32_t>(mark->letter)});
    }
    // The track's own program changes, each with its number, since they explain why a passage
    // sounds different. Its setup isn't an event in the song, so has no place here.
    if (const auto track = trackId())
    {
        for (const auto& event : song.events(*track).all())
        {
            if (const auto* program = std::get_if<core::ProgramChange>(&event.data);
                program != nullptr && inView(event.tick))
            {
                signs.push_back({scale.toX(event.tick) + 2 * lineWidth, h, U'◆', Role::EventSign,
                                 decodeUtf8(std::to_string(program->program))});
            }
        }
    }
    addTempoSigns(signs, song.tempo(), p.start, p.end, h,
                  [&](core::Tick tick) { return scale.toX(tick) + 2 * lineWidth; });
    addRowSigns(ruler, std::move(signs), cell_);

    // The third row: the bars, in the track's bars, numbered at their lines, every so many
    // bars from each section's first, as few as fit the widest number and a space into the
    // shortest bar in view.
    core::Tick shortest = p.end - p.start;
    for (auto bar = p.firstBar; bar <= p.lastBar; ++bar)
    {
        shortest = std::min(shortest, m.barStart(bar + 1) - m.barStart(bar));
    }
    const auto label = static_cast<core::Tick>(std::to_string(p.lastBar).size() + 1) * cell_.width;
    std::int64_t every = 1;
    while (every < 1024 && every * shortest * width < label * (p.end - p.start))
    {
        every *= 2;
    }
    int next = 0; // where the next number can start, clear of the last
    for (auto bar = p.firstBar; bar <= p.lastBar; ++bar)
    {
        const auto start = m.barStart(bar);
        const auto first = m.toBarBeatTick(song.sectionStart(song.sectionAt(start))).bar;
        if (!inView(start) || (bar - first) % every != 0)
        {
            continue;
        }
        const int x = scale.toX(start) + 2 * lineWidth;
        if (x < next)
        {
            continue;
        }
        auto text = decodeUtf8(std::to_string(bar));
        next = x + static_cast<int>(text.size() + 1) * cell_.width;
        ruler.text.push_back({x, 2 * h, std::move(text), {Role::Dim, Role::Background, kPlain}});
    }
}

void PianoRollView::addRegions(Frame& frame) const
{
    const auto& song = document_.song();
    const auto& main = layout_.main;
    const int h = cell_.height;
    const int keyWidth = kKeyColumns * cell_.width;
    const int rulerHeight = kRulerRows * h;
    const int laneHeight = kLaneRows * h;
    const int notesTop = main.y + rulerHeight;
    const int notesHeight = std::max(main.height - rulerHeight - laneHeight, 0);
    const int width = std::max(main.width - keyWidth, 0);
    const int rows = (notesHeight + h - 1) / h; // the last perhaps only partly shown
    const int lineWidth = hairline(cell_);
    const auto p = page();
    const auto rowOf = [&](int pitch) { return topPitch_ - pitch; };
    const auto shown = [&](int pitch) { return pitch <= topPitch_ && rowOf(pitch) < rows; };

    // The keyboard: each row's note name, the black keys' rows shaded faintly, the C's bold
    // and the cursor's pitch highlighted.
    CellGrid keys{kKeyColumns, rows};
    for (int row = 0; row < rows && topPitch_ - row >= 0; ++row)
    {
        const int pitch = topPitch_ - row;
        const auto name = core::pitchName(static_cast<std::uint8_t>(pitch));
        const Style style = pitch == pitch_   ? Style{Role::CursorText, Role::Cursor, kBold}
                            : pitch % 12 == 0 ? Style{Role::Text, Role::Background, kBold}
                                              : Style{Role::Dim, Role::Background, kPlain};
        keys.write(kKeyColumns - 1 - static_cast<int>(name.size()), row, name, style);
    }
    auto keyboard =
        gridRegion(keys, {main.x, notesTop, keyWidth, notesHeight}, cell_, Role::Background);
    for (int row = 0; row < rows && topPitch_ - row >= 0; ++row)
    {
        if (isBlackKey(topPitch_ - row) && topPitch_ - row != pitch_)
        {
            keyboard.under.push_back({{0, row * h, keyWidth, h}, Role::Panel, kBlackKeys});
        }
    }
    frame.push_back(std::move(keyboard));
    Region laneName{.bounds = {main.x, notesTop + notesHeight, keyWidth, laneHeight},
                    .background = Role::Background,
                    .text = {{cell_.width,
                              (laneHeight - h) / 2,
                              U"vel",
                              {Role::Dim, Role::Background, kPlain}}},
                    .under = {},
                    .over = {}};
    frame.push_back(std::move(laneName));

    // The notes, the page's bars stretched to the width, under the lines of the track's
    // meter at the grid.
    Region notes{.bounds = {main.x + keyWidth, notesTop, width, notesHeight},
                 .background = Role::Background,
                 .text = {},
                 .under = {},
                 .over = {}};
    Region lane{.bounds = {main.x + keyWidth, notesTop + notesHeight, width, laneHeight},
                .background = Role::Background,
                .text = {},
                .under = {},
                .over = {}};
    const TimeScale scale{.start = p.start, .length = p.end - p.start, .x = 0, .width = width};
    for (int row = 0; row < rows && topPitch_ - row >= 0; ++row)
    {
        if (isBlackKey(topPitch_ - row))
        {
            notes.under.push_back({{0, row * h, width, h}, Role::Panel, kBlackKeys});
        }
    }
    // The crosshair: a faint band along the cursor's row, and down the time it covers through
    // the lane, under the notes.
    const bool onPage = time_ >= p.start && time_ < p.end;
    const auto [spanStart, spanEnd] = cursorSpan();
    const int cursorX = scale.toX(std::max(spanStart, p.start));
    const int cursorWidth = std::max(scale.toX(std::min(spanEnd, p.end)) - cursorX, 2 * lineWidth);
    if (shown(pitch_))
    {
        notes.under.push_back({{0, rowOf(pitch_) * h, width, h}, Role::Selection, kBand});
    }
    if (onPage)
    {
        notes.under.push_back({{cursorX, 0, cursorWidth, notesHeight}, Role::Selection, kBand});
        lane.under.push_back({{cursorX, 0, cursorWidth, laneHeight}, Role::Selection, kBand});
    }
    // A Visual selection: its time at every pitch, tinted through the notes and the lane; or
    // with V its rows, through the notes alone.
    if (const auto span = selected(); span && span->first < p.end && span->second > p.start)
    {
        const int x0 = scale.toX(std::max(span->first, p.start));
        const int x1 = scale.toX(std::min(span->second, p.end));
        if (const auto pitches = selectedPitches())
        {
            const int top = std::max(rowOf(pitches->second), 0) * h;
            const int bottom = std::min(rowOf(pitches->first) + 1, rows) * h;
            if (bottom > top)
            {
                notes.under.push_back({{x0, top, x1 - x0, bottom - top}, Role::Selection, 255});
            }
        }
        else
        {
            notes.under.push_back({{x0, 0, x1 - x0, notesHeight}, Role::Selection, 255});
            lane.under.push_back({{x0, 0, x1 - x0, laneHeight}, Role::Selection, 255});
        }
    }
    for (const auto& line : meterLines(meter(), scale, 0, notesHeight, grid(), cell_))
    {
        notes.under.push_back(line);
    }
    for (const auto& line : meterLines(meter(), scale, 0, laneHeight, grid(), cell_))
    {
        if (line.role == Role::BarLine)
        {
            lane.under.push_back(line);
        }
    }
    lane.under.push_back({{0, 0, width, lineWidth}, Role::BarLine, 255});
    // Past the song's end, shaded, and its end marked.
    if (song.end() < p.end)
    {
        const int x = scale.toX(song.end());
        notes.under.push_back({{x, 0, width - x, notesHeight}, Role::Bar, 160});
        lane.under.push_back({{x, 0, width - x, laneHeight}, Role::Bar, 160});
        notes.under.push_back({{x, 0, 2 * lineWidth, notesHeight}, Role::BarLine, 255});
        lane.under.push_back({{x, 0, 2 * lineWidth, laneHeight}, Role::BarLine, 255});
    }
    // Where each section starts, a line down through the notes and the lane, stronger than a
    // bar line, so a view running on into the next section shows where; and each label, a
    // faint one.
    const auto boundary = [&](core::Tick tick, std::uint8_t alpha)
    {
        if (tick >= p.start && tick < p.end)
        {
            const int x = scale.toX(tick);
            notes.under.push_back({{x, 0, 2 * lineWidth, notesHeight}, Role::Label, alpha});
            lane.under.push_back({{x, 0, 2 * lineWidth, laneHeight}, Role::Label, alpha});
        }
    };
    for (std::size_t i = 1; i < song.sections().size(); ++i)
    {
        boundary(song.sectionStart(i), 255);
    }
    for (const auto& label : song.labels())
    {
        boundary(label.tick, kFaint);
    }
    // Each note at its exact place: brighter the louder, with a solid start so repeated notes
    // read apart, and a pixel's gap before the next; the current note, and those a selection
    // takes, in their own colour.
    // In the lane, a bar for each note starting on the page, as high as its velocity.
    const auto current = currentNote();
    const auto span = selected();
    const auto pitches = selectedPitches();
    const auto selection = host_.selection();
    const bool cutting = span && selection && selection->cut;
    const auto search = host_.searchQuery();
    if (const auto id = trackId())
    {
        const auto events = song.events(*id).all();
        const int inset = std::max(h / 8, 1);
        const int laneInset = std::max(h / 4, 1);
        for (std::size_t i = 0; i < events.size() && events[i].tick < p.end; ++i)
        {
            const auto& event = events[i];
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note == nullptr)
            {
                continue;
            }
            const auto end = event.tick + std::max<core::Tick>(note->duration, 1);
            if (end <= p.start)
            {
                continue;
            }
            // The current note stands out; or while selecting, the notes the selection takes,
            // those starting in it, and only those, even the one under the cursor.
            const bool picked =
                span ? takes(event.tick, note->pitch) &&
                           (!selection || !selection->filter || selection->movedNotes ||
                            core::matches(*selection->filter, event.data, song.ppqn()))
                     : current == i;
            const Role role = picked ? Role::NoteSelected : Role::Note;
            const bool matched = search && core::matches(*search, event.data, song.ppqn());
            const int x0 = scale.toX(std::max(event.tick, p.start));
            const int x1 = scale.toX(std::min(end, p.end));
            const int noteWidth = std::max(x1 - x0 - 1, 1);
            if (shown(note->pitch))
            {
                const int y = rowOf(note->pitch) * h + inset;
                const int start =
                    event.tick >= p.start ? std::min(noteWidth, std::max(cell_.width / 2, 2)) : 0;
                notes.under.push_back(
                    {{x0, y, start, h - 2 * inset}, role, velocityAlpha(note->velocity)});
                notes.under.push_back({{x0 + start, y, noteWidth - start, h - 2 * inset},
                                       role,
                                       bodyAlpha(note->velocity)});
                // A search's match, outlined, so it shows over the current note and a
                // selection's colour alike.
                if (matched)
                {
                    const int t = std::max(lineWidth, 1);
                    const Rect box{x0, y, noteWidth, h - 2 * inset};
                    for (const Rect side : {Rect{box.x, box.y, box.width, t},
                                            Rect{box.x, box.y + box.height - t, box.width, t},
                                            Rect{box.x, box.y, t, box.height},
                                            Rect{box.x + box.width - t, box.y, t, box.height}})
                    {
                        notes.under.push_back({side, Role::Match, 255});
                    }
                }
                // At a selection's edges: cutting, the part of a note sounding in from before
                // stands out and the part outside is dimmed, as is, either way, the part of a
                // note it takes sounding on past its end.
                const auto part =
                    [&](core::Tick from, core::Tick to, Role partRole, std::uint8_t alpha)
                {
                    const int a = scale.toX(std::clamp(from, p.start, p.end));
                    const int b =
                        std::min(scale.toX(std::clamp(to, p.start, p.end)), x0 + noteWidth);
                    if (b > a)
                    {
                        notes.under.push_back({{a, y, b - a, h - 2 * inset}, partRole, alpha});
                    }
                };
                if (span &&
                    (!pitches || (note->pitch >= pitches->first && note->pitch <= pitches->second)))
                {
                    if (cutting && event.tick < span->first && end > span->first)
                    {
                        part(event.tick, span->first, Role::Background, kDimmed);
                        part(span->first, std::min(end, span->second), Role::NoteSelected,
                             bodyAlpha(note->velocity));
                    }
                    if (event.tick < span->second && end > span->second &&
                        (cutting || event.tick >= span->first))
                    {
                        part(span->second, end, Role::Background, kDimmed);
                    }
                }
            }
            if (event.tick >= p.start)
            {
                const int top = laneInset + lineWidth;
                const int high = (laneHeight - top) * note->velocity / 127;
                // A match's velocity in the match's colour, so it reads with the note above
                // (decided November 2026), unless it's the current note or selected.
                lane.under.push_back({{x0, laneHeight - high, std::max(2 * lineWidth, 2), high},
                                      matched && !picked ? Role::Match : role,
                                      255});
            }
        }
    }

    // Where a quantised track's notes will be heard, faintly, for those far enough from where
    // they're stored to see: a few ticks, as a played part's notes all are slightly off.
    if (const auto id = trackId(); id && host_.quantizeMarks())
    {
        const auto* player = host_.player();
        const auto& track = *song.track(*id);
        if (track.quantize.on && (player == nullptr || !player->bypassQuantize()))
        {
            const auto result = core::playbackQuantized(song, track);
            if (const auto* quantized = std::get_if<core::QuantizedTrack>(&result))
            {
                const auto* played = &quantized->events;
                const auto near = std::max<core::Tick>(song.ppqn().quarter() / 48, 2);
                for (const auto& [event, stored] : *played)
                {
                    const auto* note = std::get_if<core::Note>(&event.data);
                    if (note != nullptr && std::abs(event.tick - stored) > near &&
                        event.tick >= p.start && event.tick < p.end && shown(note->pitch))
                    {
                        notes.over.push_back({{scale.toX(event.tick), rowOf(note->pitch) * h,
                                               std::max(2 * lineWidth, 2), h},
                                              Role::Note,
                                              kQuantizeMark});
                    }
                }
            }
        }
    }

    // Marks with a pitch, on this track: a dot over the notes at the mark's time and pitch, its
    // letter above in the ruler.
    for (const auto& mark : song.marks())
    {
        if (mark.track && mark.track == trackId() && mark.pitch != core::Mark::kNoPitch &&
            mark.tick >= p.start && mark.tick < p.end && shown(mark.pitch))
        {
            addMarkDot(notes, scale.toX(mark.tick) + 2 * lineWidth, rowOf(mark.pitch) * h, cell_);
        }
    }

    // The cursor's cell boxed, over the notes, and the playhead, a line through the notes and
    // the lane.
    if (onPage && shown(pitch_))
    {
        const int y = rowOf(pitch_) * h;
        const int edge = std::max(h / 10, 1);
        const int side = std::max(lineWidth, 1);
        notes.over.push_back({{cursorX, y, cursorWidth, edge}, Role::Cursor, 255});
        notes.over.push_back({{cursorX, y + h - edge, cursorWidth, edge}, Role::Cursor, 255});
        notes.over.push_back({{cursorX, y, side, h}, Role::Cursor, 255});
        notes.over.push_back({{cursorX + cursorWidth - side, y, side, h}, Role::Cursor, 255});
    }
    Region ruler{.bounds = {main.x + keyWidth, main.y, width, rulerHeight},
                 .background = Role::Background,
                 .text = {},
                 .under = {},
                 .over = {}};
    addRuler(ruler, scale, p);
    if (playhead_ && *playhead_ >= p.start && *playhead_ < p.end)
    {
        const int x = scale.toX(*playhead_);
        notes.over.push_back({{x, 0, 2 * lineWidth, notesHeight}, Role::Accent, 255});
        lane.over.push_back({{x, 0, 2 * lineWidth, laneHeight}, Role::Accent, 255});
        ruler.over.push_back({{x, 0, 2 * lineWidth, rulerHeight}, Role::Accent, 255});
    }
    frame.push_back(std::move(notes));
    frame.push_back(std::move(lane));
    frame.push_back(Region{.bounds = {main.x, main.y, keyWidth, rulerHeight},
                           .background = Role::Background,
                           .text = {},
                           .under = {},
                           .over = {}});
    frame.push_back(std::move(ruler));

    // The view's bar: the track, then the page, its section and bars, and where the cursor
    // is. The grid is in the status line, with the settings, and the note a new one would be
    // in the inspector, being the cursor's (decided November 2026, after use).
    std::vector<BarPair> left;
    std::vector<BarPair> right;
    if (trackId())
    {
        left = trackPairs(song, track_ + 1);
    }
    else
    {
        left.push_back({"No tracks", {}});
    }
    right.push_back({"Section:", song.sections()[p.section]->name(), kDropSection});
    right.push_back({"Bars:",
                     p.firstBar == p.lastBar
                         ? std::to_string(p.firstBar)
                         : std::to_string(p.firstBar) + "–" + std::to_string(p.lastBar),
                     kDropBars});
    std::ranges::move(cursorPlacePairs(song, meter(), time_), std::back_inserter(right));
    frame.push_back(labelledBar(left, right, layout_.top[0], width_, cell_));

    auto inspector =
        inspectorRegion(inspection(), layout_.top[1], width_, cell_,
                        static_cast<int>(positionText(meter(), lastTick()).size()), &editor_);
    if (trackId())
    {
        addNewNote(inspector, *song.tracks()[track_]);
    }
    frame.push_back(std::move(inspector));
}

void PianoRollView::addNewNote(Region& inspector, const core::Track& track) const
{
    // At the inspector's right, stacked: the note o would add where the cursor is, its length
    // and velocity, dim as e doesn't edit them; left out where the fields need the room.
    const Style label{Role::Dim, Role::Panel, kPlain};
    const std::array<std::pair<std::u32string, std::u32string>, 2> rows{{
        {U"Length:", decodeUtf8(track.noteLength ? core::toString(*track.noteLength) : "grid")},
        {U"Velocity:", decodeUtf8(std::to_string(track.noteVelocity))},
    }};
    const std::u32string title = U"New note";
    std::size_t labels = 0;
    std::size_t values = 0;
    for (const auto& [name, value] : rows)
    {
        labels = std::max(labels, name.size());
        values = std::max(values, value.size());
    }
    const auto wide = std::max(title.size(), labels + 1 + std::max<std::size_t>(values, 4));
    const int margin = BarSpacing::forCell(cell_).margin;
    const int x = inspector.bounds.width - margin - static_cast<int>(wide) * cell_.width;
    int used = 0;
    for (const auto& run : inspector.text)
    {
        used = std::max(used, run.x + static_cast<int>(run.text.size()) * cell_.width);
    }
    if (x < used + 3 * cell_.width || cell_.height * 3 > inspector.bounds.height)
    {
        return;
    }
    const int top = cell_.height / 4;
    inspector.text.push_back({x, top, title, label});
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const int y = top + static_cast<int>(i + 1) * cell_.height;
        inspector.text.push_back({x, y, rows[i].first, label});
        inspector.text.push_back(
            {x + static_cast<int>(labels + 1) * cell_.width, y, rows[i].second, label});
    }
}

std::optional<std::string> PianoRollView::gridStatus() const
{
    return trackId() ? std::optional{core::toString(grid())} : std::nullopt;
}

std::string PianoRollView::position(core::Tick tick) const
{
    return positionText(meter(), tick);
}

SongCursor PianoRollView::cursor() const
{
    return {.track = trackId(), .tick = time_, .pitch = pitch_};
}

void PianoRollView::setCursor(const SongCursor& cursor)
{
    // From the conductor's list, the track the piano roll was last on.
    auto track = track_;
    if (cursor.track)
    {
        const auto tracks = document_.song().tracks();
        const auto it =
            std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == *cursor.track; });
        if (it != tracks.end())
        {
            track = static_cast<std::size_t>(it - tracks.begin());
        }
    }
    // Coming to a time outside the view, or to another track, whole bars again, as a track
    // opens; otherwise the view stays as it was left.
    const auto tick = std::clamp<core::Tick>(cursor.tick, 0, lastTick());
    const bool fresh = !opened_ || track != track_;
    if (fresh)
    {
        showTrack(track, cursor.pitch);
    }
    else if (cursor.pitch)
    {
        moveToPitch(*cursor.pitch);
    }
    time_ = tick;
    if (fresh || tick < viewStart_ || tick >= viewEnd(viewStart_))
    {
        align();
    }
}

void PianoRollView::open(const SongCursor& cursor)
{
    openedBars_.reset();
    setCursor(cursor);
    align();
}

void PianoRollView::openBars(const SongCursor& cursor, core::Tick from, core::Tick to)
{
    open(cursor);
    const auto& m = meter();
    const auto bars = m.toBarBeatTick(std::max(to - 1, from)).bar - m.toBarBeatTick(from).bar + 1;
    openedBars_ = std::clamp<std::int64_t>(bars, 1, kPageSizes.back());
    viewStart_ = from;
    viewLength_.reset();
}

core::EditPlace PianoRollView::place() const
{
    return placeWith(trackId(), time_,
                     PianoRollState{.pitch = pitch_,
                                    .topPitch = topPitch_,
                                    .pageSize = pageSize_,
                                    .viewStart = viewStart_,
                                    .viewLength = viewLength_});
}

void PianoRollView::restore(const core::EditPlace& place)
{
    refresh();
    setCursor({.track = place.track, .tick = place.tick, .pitch = std::nullopt});
    // The view as it was, when the change was made in the piano roll.
    if (const auto state = pianoRollStateOf(place))
    {
        pitch_ = state->pitch;
        topPitch_ = state->topPitch;
        pageSize_ = std::min(state->pageSize, kPageSizes.size() - 1);
        viewStart_ = state->viewStart;
        viewLength_ = state->viewLength;
        turn(time_);
        follow();
    }
}

void PianoRollView::refresh()
{
    const auto tracks = document_.song().tracks();
    track_ = tracks.empty() ? 0 : std::min(track_, tracks.size() - 1);
    moveTo(time_);
    follow();
}

void PianoRollView::showSong()
{
    track_ = 0;
    time_ = 0;
    viewStart_ = 0;
    viewLength_.reset();
    pitch_ = 60;
    topPitch_ = 72;
    barOffset_.reset();
    pageSize_ = kDefaultPageSize;
    playhead_.reset();
    opened_ = false;
    pitches_.clear();
    pageSizes_.clear();
}

void PianoRollView::showPlayhead(core::Tick playhead, Follow follow)
{
    // The pages turn with the playhead, and following, the cursor goes with it too; if not,
    // the view comes back to the cursor when playback stops.
    playhead_ = playhead;
    if (follow == Follow::Cursor)
    {
        moveTo(playhead);
    }
    else if (follow == Follow::View)
    {
        turn(playhead);
    }
}

void PianoRollView::stopPlayhead(std::optional<core::Tick> cursorTo)
{
    playhead_.reset();
    moveTo(cursorTo.value_or(time_));
}

} // namespace modal::ui
