// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/note_fraction.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/time_motions.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr Style kPlainText{Role::Text, Role::Background, kPlain};
constexpr Style kDimText{Role::Dim, Role::Background, kPlain};
constexpr Style kHeading{Role::BarText, Role::Bar, kPlain};
constexpr Style kCursor{Role::CursorText, Role::Cursor, kPlain};

// The columns' headings and widths in cells, with two spaces after each, and whether
// they're right-justified, as numbers are.
struct ColumnInfo
{
    const char* heading;
    int width;
    bool right;
};

constexpr std::array<ColumnInfo, kColumnCount> kColumns{{
    {"Position", 0, false}, // as wide as the song's positions need
    {"Event", 13, false},
    {"Data", 18, false},
    {"Vel/Value", 11, true},
    {"Length", 19, false},
    {"Release", 9, true},
}}; // all within the smallest window, 80 cells

// Where text starts in a column: at its left, or right-justified to two cells before the
// next column.
int textX(int x, int width, bool right, std::string_view text)
{
    return right ? x + width - 2 - static_cast<int>(decodeUtf8(text).size()) : x;
}

constexpr int kMargin = 1; // a cell before the first column

// A command that takes a count, and one that works only at the : prompt.
constexpr CommandInfo counted(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true};
}

// A motion, which also takes a count.
constexpr CommandInfo motion(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true, .motion = true};
}

constexpr std::array kCommands{
    motion("cursor-left", "Move to the field before, or the bar before"),
    motion("cursor-right", "Move to the next field, or the next bar"),
    motion("cursor-down", "Move down an event"),
    motion("cursor-up", "Move up an event"),
    CommandInfo{.name = "cursor-first",
                .description = "Move to the first field, or the first bar",
                .motion = true},
    CommandInfo{.name = "cursor-last",
                .description = "Move to the last field, or the last bar",
                .motion = true},
    motion("cursor-bar-next", "Move to the event at or after the same place a bar on"),
    motion("cursor-bar-previous", "Move to the event at or after the same place a bar back"),
    motion("cursor-bar-line-next", "Move to the event at or after the next bar line"),
    motion("cursor-bar-line-previous", "Move to the event at or after the bar line before"),
    motion("cursor-chord-next", "Move to the next chord, or event"),
    motion("cursor-chord-previous", "Move to the chord, or event, before"),
    motion("cursor-section-next", "Move to the next section's start"),
    motion("cursor-section-previous", "Move to the section's start, or the one before"),
    motion("cursor-marker-next", "Move to the next marker"),
    motion("cursor-marker-previous", "Move to the marker before"),
    motion("go-first", "Go to the first event, or bar {count}"),
    motion("go-last", "Go to the end, or bar {count}"),
    counted("cursor-left-scroll", "Move to the field before"),
    counted("cursor-right-scroll", "Move to the next field"),
    counted("cursor-down-scroll", "Move down, scrolling to keep the cursor in place"),
    counted("cursor-up-scroll", "Move up, scrolling to keep the cursor in place"),
    counted("scroll-half-down", "Scroll down half a page"),
    counted("scroll-half-up", "Scroll up half a page"),
    counted("scroll-page-down", "Scroll down a page"),
    counted("scroll-page-up", "Scroll up a page"),
    CommandInfo{.name = "scroll-centre", .description = "Scroll the cursor to the middle"},
    counted("track-next", "Show the next track's list, or track {count}'s"),
    counted("track-previous", "Show the track's list before, {count} tracks back"),
    CommandInfo{.name = "field-edit",
                .description = "Edit the field under the cursor, typing a value or a change"},
    CommandInfo{.name = "note-add",
                .description = "Add a note at the cursor's time, typing its pitch"},
    counted("field-increase", "Step the field under the cursor up, {count} steps"),
    counted("field-decrease", "Step the field under the cursor down, {count} steps"),
    counted(
        "field-increase-fine",
        "Step the field under the cursor up, or lengthen the note by a nudge in the piano roll"),
    counted(
        "field-decrease-fine",
        "Step the field under the cursor down, or shorten the note by a nudge in the piano roll"),
    counted("number-increase",
            "Step the field under the cursor up, or the note's velocity in the piano roll"),
    counted("number-decrease",
            "Step the field under the cursor down, or the note's velocity in the piano roll"),
    counted("event-delete", "Delete this event, or {count} events from it"),
    CommandInfo{.name = "inspector-edit",
                .description = "Edit the fields in the inspector: Tab and Shift-Tab go between "
                               "them, Enter sets one and Escape leaves it"},
    CommandInfo{.name = "register-put",
                .description = "Put the yanked or deleted events, the first at the cursor's time"},
};

// Text to show in a cell of a width, without control characters, cut short with an
// ellipsis if it's too long.
std::string fitted(std::string_view text, int width)
{
    auto characters = decodeUtf8(text);
    for (auto& c : characters)
    {
        c = c < U' ' || c == 0x7F ? U'·' : c;
    }
    if (width > 1 && static_cast<int>(characters.size()) > width - 1)
    {
        characters.resize(static_cast<std::size_t>(width - 2));
        characters += U'…';
    }
    return encodeUtf8(characters);
}

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

} // namespace

std::span<const CommandInfo> EventListView::commands()
{
    return kCommands;
}

EventListView::EventListView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
    , track_(document.song().tracks().empty() ? 0 : 1)
    , list_(listOf(track_))
{
}

std::vector<int> EventListView::topHeights(CellSize cell) const
{
    // The view's own bar, a title for it, then the inspector, two lines with a quarter of a
    // line's padding above and below (decided in milestone 4: every view has its bar first,
    // then the inspector, then the view).
    return {cell.height, inspectorHeight(cell)};
}

void EventListView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = cell;
    follow();
}

int EventListView::viewRows() const
{
    return std::max(layout_.fullRows - 1, 1); // below the headings
}

Column EventListView::field() const
{
    const auto fields = list_.text(row_).fields;
    // The field kept to, or the last before it, or the first.
    Column best = fields.front();
    for (const auto f : fields)
    {
        if (f <= wanted_)
        {
            best = f;
        }
    }
    return best;
}

void EventListView::refresh()
{
    // The same time, and as far down the rows at that time as can be.
    const auto tick = list_.rows()[row_].tick;
    const auto below = row_ - list_.rowAt(tick);
    const auto tracks = document_.song().tracks().size();
    showTrack(std::min(track_, tracks), tick);
    const auto rows = list_.rows();
    while (row_ + 1 < rows.size() && rows[row_ + 1].tick == tick &&
           row_ - list_.rowAt(tick) < below)
    {
        ++row_;
    }
    follow();
}

void EventListView::showSong()
{
    top_ = 0;
    showTrack(document_.song().tracks().empty() ? 0 : 1, 0);
}

EventList EventListView::listOf(std::size_t track) const
{
    const auto& song = document_.song();
    return EventList{song,
                     track == 0 ? std::nullopt : std::optional{song.tracks()[track - 1]->id()}};
}

void EventListView::showTrack(std::size_t track, core::Tick tick)
{
    track_ = track;
    list_ = listOf(track);
    row_ = list_.rowAt(tick);
    follow();
}

std::size_t EventListView::rowAtOrBefore(core::Tick tick) const
{
    const auto after = list_.rowAt(tick + 1);
    return after > 0 ? after - 1 : 0;
}

void EventListView::showPlayhead(core::Tick playhead, Follow follow)
{
    const auto row = rowAtOrBefore(playhead);
    playheadRow_ = row;
    if (follow == Follow::Cursor)
    {
        row_ = row;
    }
    if (follow == Follow::None)
    {
        return;
    }
    // The playhead moves down to a third of the way down the view, and then the list
    // scrolls under it, so what's coming next shows, until the list's end comes into view.
    const auto rows = static_cast<std::size_t>(viewRows());
    const auto third = rows / 3;
    if (row < top_ || row > top_ + third)
    {
        const auto count = list_.rows().size();
        top_ = std::min(row > third ? row - third : 0, count > rows ? count - rows : 0);
    }
}

void EventListView::setCursor(const SongCursor& cursor)
{
    // The cursor's track (the conductor's list for none), at the row at its time; the row of
    // a note there of its pitch, if there's one, so coming back from another view lands on the
    // note it left.
    std::size_t track = 0;
    if (cursor.track)
    {
        const auto tracks = document_.song().tracks();
        const auto it =
            std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == *cursor.track; });
        track = it != tracks.end() ? static_cast<std::size_t>(it - tracks.begin()) + 1 : track_;
    }
    showTrack(track, cursor.tick);
    const auto rows = list_.rows();
    for (auto r = row_; r < rows.size() && rows[r].tick == cursor.tick; ++r)
    {
        if (rows[r].kind != RowKind::Event)
        {
            continue;
        }
        const auto& data = document_.song().events(*list_.track())[rows[r].index].data;
        if (const auto* note = std::get_if<core::Note>(&data); note && note->pitch == cursor.pitch)
        {
            row_ = r;
            break;
        }
    }
    follow();
}

void EventListView::stopPlayhead(std::optional<core::Tick> cursorTo)
{
    playheadRow_.reset();
    if (cursorTo)
    {
        row_ = rowAtOrBefore(*cursorTo);
    }
    follow();
}

const core::MeterMap& EventListView::cursorMeter() const
{
    return list_.meter();
}

SongCursor EventListView::cursor() const
{
    const auto& r = list_.rows()[row_];
    SongCursor cursor{.track = list_.track(), .tick = r.tick, .pitch = std::nullopt};
    if (r.kind == RowKind::Event)
    {
        if (const auto* note =
                std::get_if<core::Note>(&document_.song().events(*list_.track())[r.index].data))
        {
            cursor.pitch = note->pitch;
        }
    }
    return cursor;
}

void EventListView::moveRows(std::int64_t by)
{
    const auto last = static_cast<std::int64_t>(list_.rows().size()) - 1;
    row_ = static_cast<std::size_t>(
        std::clamp(static_cast<std::int64_t>(row_) + by, std::int64_t{0}, last));
}

void EventListView::moveFields(int by)
{
    const auto fields = list_.text(row_).fields;
    const auto at = std::ranges::find(fields, field()) - fields.begin();
    const auto to =
        std::clamp<std::ptrdiff_t>(at + by, 0, static_cast<std::ptrdiff_t>(fields.size()) - 1);
    wanted_ = fields[static_cast<std::size_t>(to)];
}

void EventListView::follow()
{
    const auto rows = static_cast<std::size_t>(viewRows());
    const auto count = list_.rows().size();
    top_ = std::min(top_, count > rows ? count - rows : 0);
    if (row_ < top_)
    {
        top_ = row_;
    }
    else if (row_ >= top_ + rows)
    {
        top_ = row_ - rows + 1;
    }
}

void EventListView::land(core::Tick target, bool forward,
                         const std::function<core::Tick(core::Tick)>& again)
{
    // Going back means earlier in time, not just an earlier row at the same time.
    const auto from = list_.rows()[row_].tick;
    auto row = list_.rowAt(target);
    while (!forward && list_.rows()[row].tick >= from && target > 0)
    {
        target = again(target);
        row = list_.rowAt(target);
    }
    if (forward || list_.rows()[row].tick < from)
    {
        row_ = row;
    }
}

std::vector<std::size_t> EventListView::chordStarts() const
{
    const auto& song = document_.song();
    const auto rows = list_.rows();
    std::vector<std::size_t> starts;
    std::optional<core::Tick> first;
    for (std::size_t i = 0; i + 1 < rows.size(); ++i) // not the end
    {
        if (track_ != 0 && rows[i].kind != RowKind::Event)
        {
            continue;
        }
        if (!first || !together(song, *first, rows[i].tick, chordWindow_))
        {
            starts.push_back(i);
            first = rows[i].tick;
        }
    }
    return starts;
}

std::optional<bool> EventListView::run(std::string_view command, int count)
{
    const bool keepsBarOffset = command == "cursor-bar-next" || command == "cursor-bar-previous";
    if (!keepsBarOffset)
    {
        barOffset_.reset();
    }
    const int n = std::max(count, 1);
    const auto rows = static_cast<std::int64_t>(viewRows());
    const auto scroll = [&](std::int64_t by)
    {
        const auto last = static_cast<std::int64_t>(list_.rows().size()) - 1;
        const auto maxTop = std::max(last + 1 - rows, std::int64_t{0});
        top_ = static_cast<std::size_t>(
            std::clamp(static_cast<std::int64_t>(top_) + by, std::int64_t{0}, maxTop));
        moveRows(by);
    };
    const auto barRow = [&](int bar) { return list_.rowAt(list_.meter().barStart(bar)); };

    const auto& meter = list_.meter();
    const auto tick = list_.rows()[row_].tick;
    const auto& song = document_.song();

    if (command == "cursor-down" || command == "cursor-up")
    {
        moveRows(command == "cursor-down" ? n : -n);
    }
    else if (keepsBarOffset)
    {
        const auto offset = barOffset_.value_or(offsetInBar(meter, tick));
        if (command == "cursor-bar-next")
        {
            land(sameInBar(meter, tick, n, offset), true, {});
        }
        else
        {
            land(sameInBar(meter, tick, -n, offset), false,
                 [&](core::Tick t) { return sameInBar(meter, t, -1, offset); });
        }
        barOffset_ = offset;
    }
    else if (command == "cursor-bar-line-next")
    {
        land(barLineAfter(meter, tick, n), true, {});
    }
    else if (command == "cursor-bar-line-previous")
    {
        land(barLineBefore(meter, tick, n), false,
             [&](core::Tick t) { return barLineBefore(meter, t, 1); });
    }
    else if (command == "cursor-chord-next" || command == "cursor-chord-previous")
    {
        const auto starts = chordStarts();
        // The chord the cursor is in: the last starting at or before its row.
        const auto after = std::ranges::upper_bound(starts, row_);
        auto at = after - starts.begin() - 1;
        if (command == "cursor-chord-next")
        {
            at += n;
        }
        else
        {
            at -= at >= 0 && starts[static_cast<std::size_t>(at)] == row_ ? n : n - 1;
        }
        if (at >= static_cast<std::ptrdiff_t>(starts.size()))
        {
            row_ = list_.rows().size() - 1; // past the last chord, the end
        }
        else if (!starts.empty())
        {
            const auto to = starts[static_cast<std::size_t>(std::max<std::ptrdiff_t>(at, 0))];
            if (command == "cursor-chord-next" || to < row_) // never back to a later row
            {
                row_ = to;
            }
        }
    }
    else if (command.starts_with("cursor-section-") || command.starts_with("cursor-marker-"))
    {
        const bool sections = command.starts_with("cursor-section-");
        std::vector<core::Tick> ticks;
        if (sections)
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
            command.ends_with("-next") ? nextOf(ticks, tick, n) : previousOf(ticks, tick, n);
        if (!to)
        {
            return false;
        }
        // On its row: a section's is the first at its tick; a marker's comes after any
        // section, meter or tempo change there.
        row_ = list_.rowAt(*to);
        const auto kind = sections ? RowKind::Section : RowKind::Marker;
        while (list_.rows()[row_].kind != kind)
        {
            ++row_;
        }
    }
    else if (command == "cursor-left" || command == "cursor-left-scroll")
    {
        moveFields(-n);
    }
    else if (command == "cursor-right" || command == "cursor-right-scroll")
    {
        moveFields(n);
    }
    else if (command == "cursor-first" || command == "cursor-last")
    {
        const auto fields = list_.text(row_).fields;
        wanted_ = command == "cursor-first" ? fields.front() : fields.back();
    }
    else if (command == "go-first")
    {
        row_ = count > 0 ? barRow(count) : 0;
    }
    else if (command == "go-last")
    {
        row_ = count > 0 ? barRow(count) : list_.rows().size() - 1;
    }
    else if (command == "cursor-down-scroll" || command == "cursor-up-scroll")
    {
        scroll(command == "cursor-down-scroll" ? n : -n);
    }
    else if (command.starts_with("scroll-half-") || command.starts_with("scroll-page-"))
    {
        const bool half = command.starts_with("scroll-half-");
        const auto page = std::max(half ? rows / 2 : rows - 2, std::int64_t{1}); // as in Vim
        scroll(n * page * (command.ends_with("down") ? 1 : -1));
    }
    else if (command == "scroll-centre")
    {
        top_ = row_ > static_cast<std::size_t>(rows / 2) ? row_ - static_cast<std::size_t>(rows / 2)
                                                         : 0;
    }
    else if (command == "track-next" || command == "track-previous")
    {
        const auto tracks = static_cast<std::int64_t>(document_.song().tracks().size()) + 1;
        std::int64_t to = static_cast<std::int64_t>(track_);
        if (command == "track-next")
        {
            to = count > 0 ? std::min<std::int64_t>(count, tracks - 1) : (to + 1) % tracks;
        }
        else
        {
            to = ((to - n) % tracks + tracks) % tracks;
        }
        showTrack(static_cast<std::size_t>(to), list_.rows()[row_].tick);
        return true;
    }
    else if (command == "event-delete")
    {
        return operate("operator-delete", "object-note", count);
    }
    else if (command == "register-put")
    {
        return put();
    }
    else if (command == "field-edit" || command == "inspector-edit")
    {
        // e too: the event list's fields are edited in their cells, as in a spreadsheet,
        // rather than in the inspector (decided October 2026, after use).
        return openField();
    }
    else if (command == "note-add")
    {
        return addNote();
    }
    else if (command.starts_with("field-increase") || command.starts_with("field-decrease") ||
             command == "number-increase" || command == "number-decrease")
    {
        return stepFieldBy(command.find("-increase") != std::string_view::npos ? n : -n);
    }
    else
    {
        return std::nullopt;
    }
    follow();
    return true;
}

bool EventListView::operate(std::string_view op, std::string_view motion, int count)
{
    if (motion == "object-bar")
    {
        setMessage("ab isn't in the event list, which is for events rather than time",
                   MessageKind::Information);
        return true;
    }
    if (motion == "object-chord")
    {
        const auto chord = chordHere(count);
        return chord.empty() || operateEvents(op, chord, row_);
    }
    const auto rows = rowsFor(motion, count);
    return rows && operateRows(op, rows->from, rows->to, rows->bar, rows->span);
}

std::vector<std::size_t> EventListView::chordHere(int count)
{
    // The chord holding the note on the cursor's row, notes only.
    const auto& row = list_.rows()[row_];
    const auto id = list_.track();
    if (track_ == 0 || !id || row.kind != RowKind::Event ||
        !document_.song().events(*id)[row.index].isNote())
    {
        setMessage("No chord here", MessageKind::Warning);
        return {};
    }
    const auto& song = document_.song();
    return chordNotes(song, song.events(*id), row.index, count, chordWindow_);
}

std::optional<OperatorTarget> EventListView::target(std::string_view motion, int count)
{
    // The track's events among the rows a range or object takes.
    if (track_ == 0)
    {
        setMessage("The conductor's tempo and meter changes aren't changed this way",
                   MessageKind::Information);
        return std::nullopt;
    }
    if (motion == "object-bar")
    {
        setMessage("ab isn't in the event list, which is for events rather than time",
                   MessageKind::Information);
        return std::nullopt;
    }
    const auto id = *list_.track();
    OperatorTarget result{.parts = {{id, {}}}, .what = {}, .notesOnly = false};
    auto& taken = result.parts.front().events;
    if (motion == "object-chord")
    {
        taken = chordHere(count);
        if (taken.empty())
        {
            return std::nullopt;
        }
        result.what = eventCount(taken.size()) + " in " + trackName();
        return result;
    }
    const auto rows = rowsFor(motion, count);
    if (!rows)
    {
        return std::nullopt;
    }
    const auto last = std::min(std::max(rows->from, rows->to), list_.rows().size() - 1);
    for (auto r = std::min(rows->from, rows->to); r <= last; ++r)
    {
        if (list_.rows()[r].kind == RowKind::Event)
        {
            taken.push_back(list_.rows()[r].index);
        }
    }
    if (taken.empty())
    {
        setMessage("None of " + trackName() + "'s own events there", MessageKind::Information);
        return std::nullopt;
    }
    std::ranges::sort(taken);
    result.what = eventCount(taken.size()) + " in " + trackName();
    return result;
}

std::optional<EventListView::Rows> EventListView::rowsFor(std::string_view motion, int count)
{
    if (motion == "object-note")
    {
        // in, x, Y: the event under the cursor, or as many from it as the count says.
        return Rows{row_, row_ + static_cast<std::size_t>(std::max(count, 1)) - 1};
    }
    if (motion == "operator-line" || motion == "object-bar-contents" || isSectionObject(motion))
    {
        // dd, ib: the rows of the cursor's bar and as many after it as the count says less one,
        // taken as bars; is the same for sections.
        const auto& meter = list_.meter();
        const auto tick = list_.rows()[row_].tick;
        const auto bar = meter.toBarBeatTick(tick).bar;
        auto from = meter.barStart(bar);
        auto to = meter.barStart(bar + std::max(count, 1));
        if (isSectionObject(motion))
        {
            const auto range = sectionsFrom(document_.song(), tick, count);
            from = range.from;
            to = range.to;
        }
        const auto first = list_.rowAt(from);
        auto last = first;
        while (last + 1 < list_.rows().size() && list_.rows()[last + 1].tick < to)
        {
            ++last;
        }
        if (list_.rows()[first].tick >= to)
        {
            return std::nullopt;
        }
        return Rows{first, last, from, to - from};
    }
    const auto from = row_;
    run(motion, count);
    const auto to = row_;
    row_ = from;
    // Moving between rows takes them all, the one landed on too, as Vim's line motions do;
    // the others stop before the row they land on.
    const bool whole = motion == "cursor-down" || motion == "cursor-up" || motion == "go-first" ||
                       motion == "go-last";
    if (whole)
    {
        return Rows{from, to};
    }
    if (to == from)
    {
        return std::nullopt;
    }
    return to > from ? Rows{from, to - 1} : Rows{to, from - 1};
}
bool EventListView::operateRows(std::string_view op, std::size_t from, std::size_t to,
                                std::optional<core::Tick> bar, core::Tick span)
{
    if (track_ == 0)
    {
        if (op == "operator-yank")
        {
            setMessage("Only a track's events can be yanked", MessageKind::Warning);
            return true;
        }
        return deleteConductor(std::min(from, to), std::max(from, to));
    }
    if (from > to)
    {
        std::swap(from, to);
    }
    to = std::min(to, list_.rows().size() - 1);
    // The track's own events among the rows; the conductor's rows and the end aren't touched.
    std::vector<std::size_t> indices;
    for (auto r = from; r <= to; ++r)
    {
        if (list_.rows()[r].kind == RowKind::Event)
        {
            indices.push_back(list_.rows()[r].index);
        }
    }
    if (indices.empty())
    {
        return false;
    }
    return operateEvents(op, indices, from, bar, span);
}

bool EventListView::operateEvents(std::string_view op, const std::vector<std::size_t>& indices,
                                  std::size_t row, std::optional<core::Tick> bar, core::Tick span)
{
    const auto id = *list_.track();
    const auto& events = document_.song().events(id);
    const auto first = bar.value_or(events[indices.front()].tick);
    auto& reg = host_.eventRegister();
    reg = EventRegister{};
    reg.bars = bar.has_value();
    reg.span = span;
    reg.tracks.assign(1, {});
    for (const auto i : indices)
    {
        auto event = events[i];
        event.tick -= first;
        reg.tracks.front().push_back(std::move(event));
    }
    const auto what =
        indices.size() == 1 ? std::string{"1 event"} : std::to_string(indices.size()) + " events";
    if (op == "operator-yank")
    {
        setMessage(what + " yanked", MessageKind::Information);
        return true;
    }
    edit("Delete " + what + " in " + trackName(),
         [&](core::Song& song)
         {
             song.updateEvents(id,
                               [&](core::EventList& list)
                               {
                                   for (auto it = indices.rbegin(); it != indices.rend(); ++it)
                                   {
                                       list.erase(*it);
                                   }
                               });
         });
    // On what followed, as in Vim.
    row_ = std::min(row, list_.rows().size() - 1);
    follow();
    return true;
}

bool EventListView::put()
{
    if (track_ == 0)
    {
        setMessage("Only a track's events can be put", MessageKind::Warning);
        return true;
    }
    if (host_.eventRegister().empty())
    {
        setMessage("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    if (!host_.eventRegister().sections.empty())
    {
        setMessage("The register holds sections: put them on the section strip",
                   MessageKind::Warning);
        return true;
    }
    // At the cursor's time, or taken as bars at the start of the cursor's bar.
    const auto& reg = host_.eventRegister();
    const auto& meter = list_.meter();
    const auto tick = list_.rows()[row_].tick;
    const auto at = reg.bars ? meter.barStart(meter.toBarBeatTick(tick).bar) : tick;
    std::size_t total = 0;
    for (const auto& events : reg.tracks)
    {
        total += events.size();
    }
    const auto where =
        reg.tracks.size() > 1 ? trackName() + " and the tracks after it" : trackName();
    edit("Put " + eventCount(total) + " in " + where,
         [&](core::Song& song) { putRegister(song, reg, track_ - 1, at); });
    return true;
}

namespace
{

// What a field is called in undo titles.
std::string fieldName(Column field, const core::Event& event)
{
    switch (field)
    {
    case Column::Position:
        return "position";
    case Column::Data:
        if (event.isNote() || std::holds_alternative<core::PolyPressure>(event.data))
        {
            return "pitch";
        }
        if (std::holds_alternative<core::ControlChange>(event.data))
        {
            return "controller";
        }
        if (std::holds_alternative<core::ProgramChange>(event.data))
        {
            return "program";
        }
        return "text";
    case Column::Value:
        return event.isNote() ? "velocity" : "value";
    case Column::Length:
        return "length";
    case Column::Release:
        return "release velocity";
    case Column::Event:
        break;
    }
    return "field";
}

} // namespace

bool EventListView::openField()
{
    const auto& row = list_.rows()[row_];
    if (row.kind == RowKind::End)
    {
        setMessage("Nothing to edit here", MessageKind::Information);
        return true;
    }
    if (row.dim)
    {
        setMessage("That's the conductor's: edit it in its list (gT)", MessageKind::Warning);
        return true;
    }
    if (track_ == 0 && field() != Column::Data)
    {
        setMessage("Moving the conductor's changes comes later", MessageKind::Warning);
        return true;
    }
    // The value as shown, less any note value or note number after it in brackets.
    const auto column = field();
    auto text = list_.text(row_)[column];
    text = text.substr(0, text.find(" ("));
    field_ = FieldPrompt{row_, column, true, std::nullopt};
    fieldLine_.open(decodeUtf8(text));
    host_.startTyping();
    return true;
}

bool EventListView::addNote()
{
    if (track_ == 0)
    {
        setMessage("The conductor's list has no notes", MessageKind::Warning);
        return true;
    }
    // At the cursor's time, with the velocity and length of the note before, if any.
    const auto tick = list_.rows()[row_].tick;
    core::Note note{60, 100, core::NoteFraction{1, 8}.toTicks(document_.song().ppqn())};
    for (const auto& event : document_.song().events(*list_.track()).all())
    {
        if (event.tick > tick)
        {
            break;
        }
        if (const auto* before = std::get_if<core::Note>(&event.data))
        {
            note = *before;
        }
    }
    field_ = FieldPrompt{row_, Column::Data, false, core::Event{tick, note}};
    fieldLine_.open();
    host_.startTyping();
    // Room for the new row below the cursor's.
    if (row_ + 2 > top_ + static_cast<std::size_t>(viewRows()))
    {
        ++top_;
    }
    return true;
}

bool EventListView::finishField()
{
    const auto prompt = *field_;
    const auto text = fieldLine_.take();
    if (prompt.adding)
    {
        // The new note's pitch: added once it's typed, as one undo step.
        const auto id = *list_.track();
        auto result = setField(*prompt.adding, Column::Data, text, list_.meter());
        std::optional<std::string> error;
        if (text.empty())
        {
            error = "Type the note's pitch, such as C4 or 60";
        }
        else if (const auto* why = std::get_if<std::string>(&result))
        {
            error = *why;
        }
        else
        {
            error = overlap(document_.song().events(id), std::nullopt,
                            std::get<core::Event>(result), list_.meter());
        }
        if (error)
        {
            setMessage(*error, MessageKind::Warning);
            fieldLine_.open(decodeUtf8(text));
            return true;
        }
        const auto note = std::get<core::Event>(result);
        field_.reset();
        host_.stopTyping();
        edit("Add a note in " + trackName(), [&](core::Song& song)
             { song.updateEvents(id, [&](core::EventList& events) { events.insert(note); }); });
        const auto rows = list_.rows();
        const auto& now = document_.song().events(id);
        for (auto r = list_.rowAt(note.tick); r < rows.size(); ++r)
        {
            if (rows[r].kind == RowKind::Event && now[rows[r].index] == note)
            {
                row_ = r;
                break;
            }
        }
        wanted_ = Column::Data;
        follow();
        return true;
    }
    const auto& row = list_.rows()[prompt.row];
    std::optional<std::string> error;
    if (track_ == 0)
    {
        error = setConductor(prompt.row, text);
    }
    else
    {
        const auto& event = document_.song().events(*list_.track())[row.index];
        const auto result = setField(event, prompt.column, text, list_.meter());
        if (const auto* why = std::get_if<std::string>(&result))
        {
            error = *why;
        }
        else
        {
            error = replaceEvent(prompt.row, std::get<core::Event>(result), prompt.column);
        }
    }
    if (error)
    {
        // The prompt stays open, with what was typed, to put right.
        setMessage(*error, MessageKind::Warning);
        fieldLine_.open(decodeUtf8(text));
        field_->fresh = false;
        return true;
    }
    field_.reset();
    host_.stopTyping();
    return true;
}

bool EventListView::stepFieldBy(int steps)
{
    const auto& row = list_.rows()[row_];
    if (track_ == 0 && row.kind != RowKind::End)
    {
        if (auto error = stepConductor(row_, steps))
        {
            setMessage(*error, MessageKind::Warning);
        }
        return true;
    }
    if (row.kind != RowKind::Event)
    {
        setMessage(row.dim ? "That's the conductor's: edit it in its list (gT)"
                           : "Nothing to change here",
                   MessageKind::Warning);
        return true;
    }
    const auto column = field();
    const auto& event = document_.song().events(*list_.track())[row.index];
    const auto result = stepField(event, column, steps, list_.meter());
    std::optional<std::string> error;
    if (const auto* why = std::get_if<std::string>(&result))
    {
        error = *why;
    }
    else
    {
        error = replaceEvent(row_, std::get<core::Event>(result), column);
    }
    if (error)
    {
        setMessage(*error, MessageKind::Warning);
    }
    return true;
}

std::optional<std::string> EventListView::setConductor(std::size_t row, std::string_view text)
{
    const auto& r = list_.rows()[row];
    const auto tick = r.tick;
    const auto& song = document_.song();
    switch (r.kind)
    {
    case RowKind::Section:
        if (text.empty())
        {
            return "A section needs a name";
        }
        edit("Rename section " + song.sections()[r.index]->name() + " to " + std::string{text},
             [&](core::Song& s) { s.renameSection(r.index, std::string{text}); });
        return std::nullopt;
    case RowKind::Marker:
        if (text.empty())
        {
            return "A marker needs a name; dd deletes it";
        }
        edit("Rename marker " + song.markers()[r.index].name + " to " + std::string{text},
             [&](core::Song& s) { s.setMarker(tick, std::string{text}); });
        return std::nullopt;
    case RowKind::Tempo:
    {
        const auto current = song.tempo().changes()[r.index].tempo.bpm();
        double bpm = 0;
        auto number = text;
        int sign = 0;
        if (number.size() > 1 && (number.front() == '+' || number.front() == '-'))
        {
            sign = number.front() == '+' ? 1 : -1;
            number.remove_prefix(1);
        }
        const auto [ptr, error] =
            std::from_chars(number.data(), number.data() + number.size(), bpm);
        if (sign != 0)
        {
            bpm = current + sign * bpm;
        }
        if (error != std::errc{} || ptr != number.data() + number.size() || !(bpm >= 1) ||
            bpm > 1000)
        {
            return "A tempo must be from 1 to 1000 BPM, or a change such as +5";
        }
        const auto tempo = core::Tempo::fromBpm(bpm);
        edit("Set tempo to " + core::bpmText(tempo),
             [&](core::Song& s) { s.updateTempo([&](core::TempoMap& t) { t.set(tick, tempo); }); });
        return std::nullopt;
    }
    case RowKind::Meter:
    {
        const auto signature = signatureOf(text, song.ppqn());
        if (!signature)
        {
            return "A meter is a time signature such as 3/4, its bottom a power of two to 64";
        }
        // A meter change soon after, such as a remainder bar, can leave no room for a whole
        // bar of the new meter; then nothing changes, and it says so.
        const auto apply = [&](core::Song& s)
        { s.updateMeter([&](core::MeterMap& m) { m.set(tick, *signature); }); };
        auto trial = song;
        apply(trial);
        if (trial.meter().signatureAt(tick) != *signature)
        {
            const auto changes = song.meter().changes();
            const auto next = std::ranges::upper_bound(changes, tick, {}, &core::MeterChange::tick);
            return "The meter change at " +
                   positionText(song.meter(), next != changes.end() ? next->tick : tick) +
                   " leaves no room for a whole " + std::string{text} +
                   " bar here; change or delete it first";
        }
        edit("Set meter to " + std::string{text}, apply);
        return std::nullopt;
    }
    default:
        return "Nothing to change here";
    }
}

std::optional<std::string> EventListView::stepConductor(std::size_t row, int steps)
{
    const auto& r = list_.rows()[row];
    const auto& song = document_.song();
    if (field() != Column::Data)
    {
        return "Moving the conductor's changes comes later";
    }
    if (r.kind == RowKind::Tempo)
    {
        // In whole BPM, from the nearest.
        const auto bpm = std::clamp<long>(
            std::lround(song.tempo().changes()[r.index].tempo.bpm()) + steps, 1, 1000);
        return setConductor(row, std::to_string(bpm));
    }
    if (r.kind == RowKind::Meter)
    {
        auto signature = song.meter().changes()[r.index].signature;
        signature.numerator =
            std::clamp(signature.numerator + steps, 1, core::TimeSignature::kMaxNumerator);
        return setConductor(row, std::to_string(signature.numerator) + "/" +
                                     std::to_string(signature.denominator));
    }
    return "Only tempos and meters can be stepped";
}

bool EventListView::deleteConductor(std::size_t from, std::size_t to)
{
    const auto& song = document_.song();
    to = std::min(to, list_.rows().size() - 1);
    // Every section keeps its tempo and meter at its start, and sections are joined, not
    // deleted.
    std::vector<core::Tick> starts;
    for (std::size_t i = 0; i < song.sections().size(); ++i)
    {
        starts.push_back(song.sectionStart(i));
    }
    const auto atStart = [&](core::Tick tick)
    { return std::ranges::find(starts, tick) != starts.end(); };
    std::vector<EventRow> doomed;
    bool kept = false;
    for (auto r = from; r <= to; ++r)
    {
        const auto& row = list_.rows()[r];
        const bool fixed = row.kind == RowKind::Section || row.kind == RowKind::End ||
                           (row.kind != RowKind::Marker && atStart(row.tick));
        kept = kept || (fixed && row.kind != RowKind::End);
        if (!fixed)
        {
            doomed.push_back(row);
        }
    }
    if (doomed.empty())
    {
        if (kept)
        {
            setMessage("Every section keeps its tempo and meter at its start, and sections are "
                       "joined, not deleted",
                       MessageKind::Warning);
        }
        return kept;
    }
    const auto what =
        doomed.size() == 1 ? std::string{"1 change"} : std::to_string(doomed.size()) + " changes";
    edit("Delete " + what + " in the conductor",
         [&](core::Song& s)
         {
             for (const auto& row : doomed)
             {
                 if (row.kind == RowKind::Marker)
                 {
                     s.removeMarker(row.tick);
                 }
                 else if (row.kind == RowKind::Tempo)
                 {
                     s.updateTempo([&](core::TempoMap& t) { t.remove(row.tick); });
                 }
                 else
                 {
                     s.updateMeter([&](core::MeterMap& m) { m.remove(row.tick); });
                 }
             }
         });
    row_ = std::min(from, list_.rows().size() - 1);
    follow();
    return true;
}

std::optional<std::string> EventListView::replaceEvent(std::size_t row, const core::Event& changed,
                                                       Column field)
{
    const auto id = *list_.track();
    const auto index = list_.rows()[row].index;
    const auto& events = document_.song().events(id);
    if (changed == events[index])
    {
        return std::nullopt;
    }
    if (auto why = overlap(events, index, changed, list_.meter()))
    {
        return why;
    }
    const auto title = "Set " + fieldName(field, changed) + " in " + trackName();
    edit(title,
         [&](core::Song& song)
         {
             song.updateEvents(id,
                               [&](core::EventList& list)
                               {
                                   list.erase(index);
                                   list.insert(changed);
                               });
         });
    // The cursor follows the event, as a change of position moves it.
    const auto rows = list_.rows();
    const auto& now = document_.song().events(id);
    for (auto r = list_.rowAt(changed.tick); r < rows.size() && rows[r].tick == changed.tick; ++r)
    {
        if (rows[r].kind == RowKind::Event && now[rows[r].index] == changed)
        {
            row_ = r;
            break;
        }
    }
    follow();
    return std::nullopt;
}

void EventListView::restore(const core::EditPlace& place)
{
    // Back where the change was made: its track, time and scroll.
    const auto tracks = document_.song().tracks();
    if (!place.track)
    {
        track_ = 0;
    }
    else
    {
        const auto it = std::ranges::find_if(tracks, [&](const auto& track)
                                             { return track->id() == *place.track; });
        track_ = it != tracks.end() ? static_cast<std::size_t>(it - tracks.begin()) + 1
                                    : std::min(track_, tracks.size());
    }
    list_ = listOf(track_);
    row_ = list_.rowAt(place.tick);
    const auto& view = place.view;
    if (view.size() == 2)
    {
        top_ = static_cast<std::size_t>(std::max<std::int64_t>(view[0], 0));
        const auto rows = list_.rows();
        for (std::int64_t i = 0;
             i < view[1] && row_ + 1 < rows.size() && rows[row_ + 1].tick == place.tick; ++i)
        {
            ++row_;
        }
    }
    follow();
}

bool EventListView::edit(std::string title, const std::function<void(core::Song&)>& change)
{
    const bool changed = document_.edit(std::move(title), place(), change);
    refresh();
    return changed;
}

core::EditPlace EventListView::place() const
{
    // The view's state: the top row, and how far down the rows at the cursor's time it is.
    const auto tick = list_.rows()[row_].tick;
    return {list_.track(),
            tick,
            {static_cast<std::int64_t>(top_), static_cast<std::int64_t>(row_ - list_.rowAt(tick))}};
}

std::string EventListView::trackName() const
{
    if (track_ == 0)
    {
        return "the conductor";
    }
    const auto& name = document_.song().tracks()[track_ - 1]->name;
    return name.empty() ? "track " + std::to_string(track_) : name;
}

void EventListView::type(char32_t character)
{
    if (std::exchange(field_->fresh, false))
    {
        fieldLine_.clear(); // typing replaces the value, as in a spreadsheet
    }
    fieldLine_.insert(character);
}

std::optional<bool> EventListView::runTyping(std::string_view command)
{
    if (command == "command-line-run" ||
        (field_->adding && (command == "command-line-next" || command == "command-line-previous")))
    {
        return finishField();
    }
    if (command == "command-line-next" || command == "command-line-previous")
    {
        // Set, unless nothing was typed, then on to the next field of the row that can be
        // edited, round from the last to the first, as in the inspector; a value that can't be
        // set keeps the field open.
        const auto column = field_->column;
        if (field_->fresh)
        {
            field_.reset();
            host_.stopTyping();
        }
        else if (finishField(); field_)
        {
            return true;
        }
        std::vector<Column> fields;
        for (const auto f : list_.text(row_).fields)
        {
            if (f != Column::Event && (track_ != 0 || f == Column::Data))
            {
                fields.push_back(f);
            }
        }
        if (fields.empty())
        {
            return true;
        }
        const auto here = std::ranges::find(fields, column);
        const auto count = static_cast<std::ptrdiff_t>(fields.size());
        const auto at = here == fields.end() ? 0 : here - fields.begin();
        const auto by = command == "command-line-next" ? 1 : -1;
        wanted_ = fields[static_cast<std::size_t>(((at + by) % count + count) % count)];
        return openField();
    }
    if (!field_->adding && (command == "command-line-older" || command == "command-line-newer"))
    {
        // A step of the value as it is, what was typed left aside, as + and - step it.
        field_.reset();
        host_.stopTyping();
        stepFieldBy(command == "command-line-older" ? 1 : -1);
        return openField();
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && fieldLine_.text().empty())) // as in Vim
    {
        field_.reset();
        host_.stopTyping();
        return true;
    }
    field_->fresh = false; // keys that edit the value keep it
    return editLine(fieldLine_, command);
}
void EventListView::addRegions(Frame& frame) const
{
    frame.push_back(listRegion());
    frame.push_back(barRegion());
    frame.push_back(inspectorRegion());
}

std::string EventListView::position(core::Tick tick) const
{
    return positionText(list_.meter(), tick);
}

std::string EventListView::timeStatus(core::Tick tick) const
{
    const auto& song = document_.song();
    return positionText(list_.meter(), tick) + "  " +
           signatureText(list_.meter().signatureAt(tick)) + "  " +
           song.sections()[song.sectionAt(tick)]->name();
}

std::string EventListView::cursorStatus() const
{
    const auto& song = document_.song();
    const auto tick = list_.rows()[row_].tick;
    return list_.text(row_)[Column::Position] + "  " +
           signatureText(list_.meter().signatureAt(tick)) + "  " +
           song.sections()[song.sectionAt(tick)]->name();
}
Region EventListView::listRegion() const
{
    const Rect& main = layout_.main;
    const int rows = (main.height + cell_.height - 1) / cell_.height;
    CellGrid grid{layout_.columns, rows};

    // The columns' places: the position as wide as the song's last needs.
    std::array<int, kColumnCount> x{};
    std::array<int, kColumnCount> width{};
    int at = kMargin;
    for (std::size_t c = 0; c < kColumnCount; ++c)
    {
        width[c] = kColumns[c].width;
        if (c == 0)
        {
            const auto end = list_.text(list_.rows().size() - 1)[Column::Position];
            width[c] = std::max(static_cast<int>(end.size()) + 3, 10);
        }
        x[c] = at;
        at += width[c];
    }

    // The headings stay put as the list scrolls.
    grid.fill(0, 0, layout_.columns, 1, {U' ', kHeading});
    for (std::size_t c = 0; c < kColumnCount; ++c)
    {
        const auto& column = kColumns[c];
        grid.write(textX(x[c], width[c], column.right, column.heading), 0, column.heading,
                   kHeading);
    }

    // A note being added by o shows as a row of its own after the cursor's, being edited.
    const bool adding = field_ && field_->adding;
    RowText added;
    if (adding)
    {
        const auto& note = *field_->adding;
        const auto& data = std::get<core::Note>(note.data);
        added.cells[static_cast<std::size_t>(Column::Position)] =
            positionText(list_.meter(), note.tick);
        added.cells[static_cast<std::size_t>(Column::Event)] = "note";
        added.cells[static_cast<std::size_t>(Column::Value)] = std::to_string(data.velocity);
        added.cells[static_cast<std::size_t>(Column::Length)] = lengthText(
            data.duration, document_.song().ppqn(), list_.meter().signatureAt(note.tick));
        added.cells[static_cast<std::size_t>(Column::Release)] =
            std::to_string(data.releaseVelocity);
    }
    const auto cursorField = field();
    for (int line = 1; line < rows; ++line)
    {
        auto row = top_ + static_cast<std::size_t>(line - 1);
        const bool isAdded = adding && row == field_->row + 1;
        if (adding && row > field_->row + 1)
        {
            --row;
        }
        if (!isAdded && row >= list_.rows().size())
        {
            break;
        }
        const auto text = isAdded ? added : list_.text(row);
        auto style = !isAdded && list_.rows()[row].dim ? kDimText : kPlainText;
        if (!isAdded && row == playheadRow_)
        {
            style.background = Role::Playhead;
            grid.fill(0, line, layout_.columns, 1, {U' ', style});
        }
        for (std::size_t c = 0; c < kColumnCount; ++c)
        {
            const auto column = static_cast<Column>(c);
            // The last column with text may run on, such as a name or SysEx bytes.
            const bool runsOn = column == Column::Data && text[Column::Value].empty() &&
                                text[Column::Length].empty();
            const int room = runsOn ? layout_.columns - x[c] : width[c];
            const auto shown = fitted(text.cells[c], room);
            const int start = textX(x[c], width[c], kColumns[c].right, shown);
            const bool inPrompt =
                field_ && column == field_->column && (adding ? isAdded : row == field_->row);
            if (inPrompt)
            {
                // The field being edited, in a box as wide as the column, or to the edge for
                // text that runs on: what's typed, justified as the column is so it stays
                // where it was, with its cursor; longer text scrolls to keep the cursor in it,
                // as at the : prompt.
                const Style editing{Role::Text, Role::Selection, kPlain};
                const auto box =
                    static_cast<std::size_t>(std::max(runsOn ? room : width[c] - 1, 2));
                const auto& typed = fieldLine_.text();
                const auto cursor = fieldLine_.cursor();
                const std::size_t first = cursor + 1 > box ? cursor + 1 - box : 0;
                const auto visible = encodeUtf8(typed.substr(first, box));
                const bool fits = typed.size() + 1 <= box;
                const int from = fits ? textX(x[c], width[c], kColumns[c].right, visible) : x[c];
                grid.fill(x[c] - 1, line, static_cast<int>(box) + 1, 1, {U' ', editing});
                grid.write(from, line, visible, editing);
                grid.set(from + static_cast<int>(cursor - first), line,
                         {cursor < typed.size() ? typed[cursor] : U' ', kCursor});
            }
            else if (!adding && row == row_ && column == cursorField)
            {
                grid.fill(start - 1, line,
                          std::max(static_cast<int>(decodeUtf8(shown).size()), 1) + 2, 1,
                          {U' ', kCursor});
                grid.write(start, line, shown, kCursor);
            }
            else
            {
                grid.write(start, line, shown, style);
            }
        }
        // The play-from marker in the margin, over the cursor's box if it's there.
        if (!isAdded && host_.player() != nullptr && row == rowAtOrBefore(host_.player()->marker()))
        {
            const bool onCursor = !adding && row == row_ && cursorField == Column::Position;
            grid.write(0, line, "▶",
                       onCursor ? kCursor : Style{Role::Accent, style.background, kPlain});
        }
    }
    return ui::gridRegion(grid, main, cell_, Role::Background);
}

std::optional<std::string> EventListView::describeRange(std::string_view op,
                                                        std::string_view motion) const
{
    // Rows of events; the event list has no time to take out.
    if (motion == "operator-line" || motion == "object-bar-contents")
    {
        return "this bar's events";
    }
    if (motion == "object-note")
    {
        return "this event";
    }
    if (motion == "object-chord")
    {
        return "this chord";
    }
    if (motion == "object-section-contents")
    {
        return "this section's events";
    }
    if (motion == "object-section")
    {
        return sectionDescription(op);
    }
    if (motion == "cursor-down")
    {
        return "this event and the next";
    }
    if (motion == "cursor-up")
    {
        return "this event and the one before";
    }
    if (motion == "go-last")
    {
        return "to the end";
    }
    if (motion == "go-first")
    {
        return "from the start";
    }
    return describeTimeRange(motion);
}

Region EventListView::inspectorRegion() const
{
    const int position =
        static_cast<int>(list_.text(list_.rows().size() - 1)[Column::Position].size());
    return ui::inspectorRegion(inspect(list_, row_), layout_.top[1], width_, cell_, position);
}

Region EventListView::barRegion() const
{
    return trackBarRegion(document_.song(), track_, list_.rows()[row_].tick, list_.meter(),
                          layout_.top[0], width_, cell_);
}
} // namespace modal::ui
