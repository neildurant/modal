// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/time_motions.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    CommandInfo{.name = "cursor-note-next",
                .description = "Next note's start, at any pitch",
                .count = true},
    CommandInfo{.name = "cursor-note-previous",
                .description = "Note's start before, at any pitch",
                .count = true},
    CommandInfo{.name = "grid-coarser",
                .description = "Make the track's grid coarser, {count} times",
                .count = true},
    CommandInfo{.name = "grid-finer",
                .description = "Make the track's grid finer, {count} times",
                .count = true},
    CommandInfo{.name = "grid-triplets",
                .description = "Turn triplets in the track's grid on or off"},
    CommandInfo{.name = "note-default",
                .description = "Take this note's length and velocity as the track's default note"},
    CommandInfo{.name = "grid",
                .description = "Set the track's grid, or show it with no length",
                .usage = "[LENGTH]",
                .keys = false},
    CommandInfo{.name = "len",
                .description = "Set the length of the track's default note, or grid to follow "
                               "the grid; or show it with no length",
                .usage = "[LENGTH|grid]",
                .keys = false},
};

// The grid's limits, without triplets: from a 1/64 note to a whole note.
constexpr core::NoteFraction kFinestGrid{1, 64};
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

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

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
    return {cell.height, inspectorHeight(cell)};
}

void PianoRollView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = {std::max(cell.width, 1), std::max(cell.height, 1)};
    follow();
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

PianoRollView::Page PianoRollView::pageAt(core::Tick tick) const
{
    // Pages of whole bars, counted from the section's first bar, in the track's bars.
    const auto& song = document_.song();
    const auto& m = meter();
    Page page;
    page.section = song.sectionAt(tick);
    const auto sectionStart = song.sectionStart(page.section);
    const auto sectionEnd = song.sectionStart(page.section + 1);
    const auto first = m.toBarBeatTick(sectionStart).bar;
    const auto last = m.toBarBeatTick(std::max(sectionEnd - 1, sectionStart)).bar;
    const auto size = pageBars();
    const auto bar = std::clamp(m.toBarBeatTick(tick).bar, first, last);
    page.number = (bar - first) / size + 1;
    page.count = (last - first) / size + 1;
    page.firstBar = first + (page.number - 1) * size;
    page.lastBar = std::min(page.firstBar + size - 1, last);
    page.start = std::max(m.barStart(page.firstBar), sectionStart);
    page.end = std::max(std::min(m.barStart(page.lastBar + 1), sectionEnd), page.start + 1);
    return page;
}

PianoRollView::Page PianoRollView::page() const
{
    return pageAt(time_);
}

int PianoRollView::noteRows() const
{
    return std::max((layout_.main.height - kLaneRows * cell_.height) / cell_.height, 1);
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
}

void PianoRollView::moveToPitch(int pitch)
{
    pitch_ = static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
    follow();
}

void PianoRollView::showTrack(std::size_t track, std::optional<std::uint8_t> pitch)
{
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
    const auto id = trackId();
    if (!id)
    {
        return std::nullopt;
    }
    const auto events = document_.song().events(*id).all();
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < events.size() && events[i].tick <= time_; ++i)
    {
        const auto* note = std::get_if<core::Note>(&events[i].data);
        if (note != nullptr && note->pitch == pitch_ &&
            time_ < events[i].tick + std::max<core::Tick>(note->duration, 1))
        {
            found = i;
        }
    }
    return found;
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
    const bool keepsBarOffset = command == "cursor-bar-next" || command == "cursor-bar-previous";
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

    if (command == "cursor-left" || command == "cursor-left-scroll")
    {
        auto tick = time_;
        for (int i = 0; i < n; ++i)
        {
            tick = gridBefore(tick);
        }
        moveTo(tick);
    }
    else if (command == "cursor-right" || command == "cursor-right-scroll")
    {
        auto tick = time_;
        for (int i = 0; i < n && gridAfter(tick) <= lastTick(); ++i)
        {
            tick = gridAfter(tick);
        }
        moveTo(tick);
    }
    else if (command == "cursor-beat-next")
    {
        return go(beatAfter(m, time_, n));
    }
    else if (command == "cursor-beat-previous")
    {
        moveTo(beatBefore(m, time_, n));
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
    else if (command == "octave-up" || command == "join")
    {
        // J is join on the section strip and an octave down here.
        moveToPitch(pitch_ + 12 * (command == "octave-up" ? n : -n));
    }
    else if (command == "cursor-up-scroll" || command == "cursor-down-scroll")
    {
        scroll(command == "cursor-down-scroll" ? n : -n);
    }
    else if (command.starts_with("scroll-half-") || command.starts_with("scroll-page-"))
    {
        const int rows = noteRows();
        const int page = std::max(command.starts_with("scroll-half-") ? rows / 2 : rows - 2, 1);
        scroll(n * page * (command.ends_with("down") ? 1 : -1));
    }
    else if (command == "scroll-centre")
    {
        centre();
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
    else if (command.starts_with("cursor-section-") || command.starts_with("cursor-marker-"))
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
            for (const auto& marker : song.markers())
            {
                ticks.push_back(marker.tick);
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
        // The tracks in turn, with no conductor's here; with a count, gt goes to that track.
        const auto tracks = song.tracks().size();
        if (tracks == 0)
        {
            return false;
        }
        const auto steps = static_cast<std::size_t>(n) % tracks;
        const auto to = count > 0 && command == "track-next"
                            ? std::min(static_cast<std::size_t>(count), tracks) - 1
                        : command == "track-next" ? (track_ + steps) % tracks
                                                  : (track_ + tracks - steps) % tracks;
        showTrack(to, std::nullopt);
    }
    else if (command == "zoom-in" || command == "zoom-out")
    {
        // Fewer bars to a page, or more; the new page is the one holding the cursor.
        const auto last = static_cast<int>(kPageSizes.size()) - 1;
        pageSize_ = static_cast<std::size_t>(
            std::clamp(static_cast<int>(pageSize_) + (command == "zoom-in" ? -n : n), 0, last));
    }
    else if (command == "zoom-reset")
    {
        pageSize_ = kDefaultPageSize;
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
    else if (command == "note-default")
    {
        // The current note's length and velocity, as the track's default note.
        const auto index = currentNote();
        if (!index)
        {
            host_.setMessage("No note here to take as the default", MessageKind::Warning);
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

bool PianoRollView::operate(std::string_view /*op*/, std::string_view /*motion*/, int /*count*/)
{
    host_.setMessage("Editing notes in the piano roll comes next", MessageKind::Information);
    return false;
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
        result.fields.push_back({"Time", songTime(song.tempo().toTime(event.tick))});
        addNoteFields(result, song, m, event.tick, std::get<core::Note>(event.data));
        return result;
    }
    result.kind = "No note";
    result.fields.push_back({"Position", positionText(m, time_)});
    result.fields.push_back({"Time", songTime(song.tempo().toTime(time_))});
    result.fields.push_back({"Pitch", pitchText(pitch_)});
    return result;
}

void PianoRollView::addRegions(Frame& frame) const
{
    const auto& song = document_.song();
    const auto& main = layout_.main;
    const int h = cell_.height;
    const int keyWidth = kKeyColumns * cell_.width;
    const int laneHeight = kLaneRows * h;
    const int notesHeight = std::max(main.height - laneHeight, 0);
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
        gridRegion(keys, {main.x, main.y, keyWidth, notesHeight}, cell_, Role::Background);
    for (int row = 0; row < rows && topPitch_ - row >= 0; ++row)
    {
        if (isBlackKey(topPitch_ - row) && topPitch_ - row != pitch_)
        {
            keyboard.under.push_back({{0, row * h, keyWidth, h}, Role::Panel, kBlackKeys});
        }
    }
    frame.push_back(std::move(keyboard));
    Region laneName{.bounds = {main.x, main.y + notesHeight, keyWidth, laneHeight},
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
    Region notes{.bounds = {main.x + keyWidth, main.y, width, notesHeight},
                 .background = Role::Background,
                 .text = {},
                 .under = {},
                 .over = {}};
    Region lane{.bounds = {main.x + keyWidth, main.y + notesHeight, width, laneHeight},
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
    // The crosshair: a faint band along the cursor's row, and down its grid step through the
    // lane, under the notes.
    const bool onPage = time_ >= p.start && time_ < p.end;
    const int cursorX = scale.toX(time_);
    const int cursorWidth =
        std::max(scale.toX(std::min(gridAfter(time_), p.end)) - cursorX, 2 * lineWidth);
    if (shown(pitch_))
    {
        notes.under.push_back({{0, rowOf(pitch_) * h, width, h}, Role::Selection, kBand});
    }
    if (onPage)
    {
        notes.under.push_back({{cursorX, 0, cursorWidth, notesHeight}, Role::Selection, kBand});
        lane.under.push_back({{cursorX, 0, cursorWidth, laneHeight}, Role::Selection, kBand});
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

    // Each note at its exact place: brighter the louder, with a solid start so repeated notes
    // read apart, and a pixel's gap before the next; the current note in the accent colour.
    // In the lane, a bar for each note starting on the page, as high as its velocity.
    const auto current = currentNote();
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
            const Role role = current == i ? Role::Accent : Role::Note;
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
            }
            if (event.tick >= p.start)
            {
                const int top = laneInset + lineWidth;
                const int high = (laneHeight - top) * note->velocity / 127;
                lane.under.push_back(
                    {{x0, laneHeight - high, std::max(2 * lineWidth, 2), high}, role, 255});
            }
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
    if (playhead_ && *playhead_ >= p.start && *playhead_ < p.end)
    {
        const int x = scale.toX(*playhead_);
        notes.over.push_back({{x, 0, 2 * lineWidth, notesHeight}, Role::Accent, 255});
        lane.over.push_back({{x, 0, 2 * lineWidth, laneHeight}, Role::Accent, 255});
    }
    frame.push_back(std::move(notes));
    frame.push_back(std::move(lane));

    // The view's bar: the track, then the page, its section and bars, and the grid.
    const Style label{Role::Dim, Role::Bar, kPlain};
    const Style value{Role::BarText, Role::Bar, kPlain};
    Bar top;
    const auto pair = [&](std::vector<Segment>& group, std::string name, std::string text)
    {
        group.push_back({std::move(name), label});
        group.push_back({" " + std::move(text) + "     ", value});
    };
    if (const auto id = trackId())
    {
        const auto& track = *song.tracks()[track_];
        pair(top.left, "Track " + std::to_string(track_ + 1) + ":", track.name);
        pair(top.left, "Channel:", std::to_string(track.channel));
        pair(top.left, "Port:", track.port.empty() ? "default" : track.port);
    }
    else
    {
        top.left.push_back({"No tracks", label});
    }
    pair(top.right, "Section:", song.sections()[p.section]->name());
    pair(top.right, "Bars:",
         p.firstBar == p.lastBar ? std::to_string(p.firstBar)
                                 : std::to_string(p.firstBar) + "–" + std::to_string(p.lastBar));
    pair(top.right, "Page:", std::to_string(p.number) + "/" + std::to_string(p.count));
    pair(top.right, "Grid:", core::toString(grid()));
    if (trackId())
    {
        pair(top.right, "Note:", noteText(*song.tracks()[track_]));
    }
    top.right.back().text.resize(top.right.back().text.size() - 5); // no space after the last
    auto bar = layoutBar(top, width_, cell_, BarSpacing::forCell(cell_));
    bar.bounds = layout_.top[0];
    frame.push_back(std::move(bar));

    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_,
                                    static_cast<int>(positionText(meter(), lastTick()).size())));
}

std::string PianoRollView::position(core::Tick tick) const
{
    return positionText(meter(), tick);
}

std::string PianoRollView::timeStatus(core::Tick tick) const
{
    const auto& song = document_.song();
    return positionText(meter(), tick) + "  " + signatureText(meter().signatureAt(tick)) + "  " +
           song.sections()[song.sectionAt(tick)]->name();
}

std::string PianoRollView::cursorStatus() const
{
    const auto& song = document_.song();
    return positionText(meter(), time_) + "  " + core::pitchName(pitch_) + "  " +
           signatureText(meter().signatureAt(time_)) + "  " +
           song.sections()[song.sectionAt(time_)]->name();
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
    if (!opened_ || track != track_)
    {
        showTrack(track, cursor.pitch);
    }
    else if (cursor.pitch)
    {
        moveToPitch(*cursor.pitch);
    }
    moveTo(cursor.tick);
}

core::EditPlace PianoRollView::place() const
{
    return {trackId(), time_, {pitch_, topPitch_, static_cast<std::int64_t>(pageSize_)}};
}

void PianoRollView::restore(const core::EditPlace& place)
{
    refresh();
    setCursor({.track = place.track, .tick = place.tick, .pitch = std::nullopt});
    // The view as it was, when the change was made in the piano roll.
    if (place.view.size() == 3)
    {
        pitch_ = static_cast<std::uint8_t>(std::clamp<std::int64_t>(place.view[0], 0, 127));
        topPitch_ = static_cast<std::uint8_t>(std::clamp<std::int64_t>(place.view[1], 0, 127));
        pageSize_ = static_cast<std::size_t>(std::clamp<std::int64_t>(
            place.view[2], 0, static_cast<std::int64_t>(kPageSizes.size()) - 1));
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
    pitch_ = 60;
    topPitch_ = 72;
    barOffset_.reset();
    pageSize_ = kDefaultPageSize;
    playhead_.reset();
    opened_ = false;
    pitches_.clear();
    pageSizes_.clear();
}

void PianoRollView::showPlayhead(core::Tick playhead, bool cursorFollows)
{
    // Following, the cursor goes with the playhead, and the pages turn with it.
    playhead_ = playhead;
    if (cursorFollows)
    {
        moveTo(playhead);
    }
}

void PianoRollView::stopPlayhead(std::optional<core::Tick> cursorTo)
{
    playhead_.reset();
    if (cursorTo)
    {
        moveTo(*cursorTo);
    }
}

} // namespace modal::ui
