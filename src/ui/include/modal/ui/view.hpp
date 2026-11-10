// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/event.hpp>
#include <modal/core/event_query.hpp>
#include <modal/core/player.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/layout.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

// A message in the status line: information in the message colour, or a warning.
enum class MessageKind : std::uint8_t
{
    Information,
    Warning,
};

// The cursor the views share (decided for milestone 5): a track (none for the conductor), a
// time and a pitch. Each view shows it its own way: the arrangement as the bar holding the
// time, the piano roll as a crosshair, the event list as the row at the time. The pitch is
// none until there's one to keep, from a note or the piano roll, so the piano roll can start
// at the middle of a track's notes.
struct SongCursor
{
    std::optional<core::TrackId> track;
    core::Tick tick = 0;
    std::optional<std::uint8_t> pitch;

    friend bool operator==(const SongCursor&, const SongCursor&) = default;
};

// How a view goes with the playhead while playing: not at all, once the view's been moved
// away from it; its view only, the cursor staying put (with rewind); or the cursor too.
enum class Follow
{
    None,
    View,
    Cursor,
};

// The views of the song (decided for milestone 5): the arrangement, the home view; the piano
// roll; and the event list.
enum class ViewKind : std::uint8_t
{
    Arrangement,
    PianoRoll,
    EventList,
    Tracks,
};

// The unnamed register, shared by the views (decided for milestone 5): what was yanked or
// deleted. Either an area's events, a list for each of its tracks from the top down, their
// ticks from the area's start, and the area's length, which P makes room for (0 if it has
// none, as from the event list's rows); or whole sections, from the section strip.
struct EventRegister
{
    std::vector<std::vector<core::Event>> tracks;
    std::vector<std::shared_ptr<const core::Section>> sections;
    core::Tick span = 0;
    // Taken as bars (ib, ab, dd, or the arrangement's bars), its ticks from a bar line, so it's
    // put at the start of the cursor's bar rather than at the cursor (decided October 2026).
    bool bars = false;
    // The cursor's pitch when it was taken in the piano roll, which Alt-p puts at the cursor's
    // pitch then, the notes keeping their intervals to it (decided November 2026); taken
    // elsewhere, the lowest note is.
    std::optional<std::uint8_t> pitch = std::nullopt;

    std::vector<core::TrackCopy> wholeTracks = {};

    bool empty() const
    {
        return wholeTracks.empty() && sections.empty() &&
               std::ranges::all_of(tracks, [](const auto& events) { return events.empty(); });
    }

    friend bool operator==(const EventRegister&, const EventRegister&) = default;
};

// A Visual selection (milestone 6): where it started, the cursor being its other end, and its
// kind: v a time range, V whole rows, Ctrl-v a box. Where the view counts in rows a time can't
// tell apart, as the event list's rows at one time, or the strip's sections, it keeps its own
// index there too, for the place it was started in.
enum class SelectionKind : std::uint8_t
{
    Range,
    Rows,
    Box,
};

struct Selection
{
    SongCursor anchor;
    SelectionKind kind = SelectionKind::Range;
    Where where = Where::Tracks;
    std::optional<std::size_t> index;
    // The time whole rows (V) take, once an object has set it (Vis); until then the whole
    // song's, or in the piano roll the page's when V was pressed.
    std::optional<std::pair<core::Tick, core::Tick>> span;
    // The starts it takes, within its span, where an object set them: a chord's (vic), so
    // a note starting while the chord sounds isn't taken with it.
    std::optional<std::pair<core::Tick, core::Tick>> starts = std::nullopt;
    bool cut = false; // cutting at its edges (|)
    // Only the events a search matches in it (, s S, decided November 2026), which operators
    // take; D and p, which take its time, are refused.
    std::optional<core::EventQuery> filter = {};
    // The object that made it from v's, its count and the view's place then and after, until
    // the cursor moves or it changes kind: operators take it as they would that object, so vXd
    // is always dX (decided November 2026, after a review).
    struct Object
    {
        std::string name;
        int count = 0;
        core::EditPlace at;
        core::EditPlace after;
        bool moved = false; // operators take the moved selection rather than resolving it again
    };
    std::optional<Object> object = std::nullopt;

    // After moving, retain the notes originally taken rather than adopting whatever the
    // moved area crosses. Start/pitch pairs identify notes in the track; imported identical
    // notes at a start are selected together. Shared immutable state keeps view queries cheap.
    using NotePosition = std::pair<core::Tick, std::uint8_t>;
    struct MovedNotes
    {
        core::TrackId track;
        std::vector<NotePosition> positions; // sorted, at their current places
        core::EditPlace after; // explicit cursor movement starts an area selection again

        bool contains(core::TrackId id, core::Tick tick, std::uint8_t pitch) const
        {
            return id == track && std::ranges::binary_search(positions, NotePosition{tick, pitch});
        }
    };
    std::shared_ptr<const MovedNotes> movedNotes = nullptr;
};

// What an operator other than d and y takes over a range or object (decided for milestone 6):
// on each track, the events it takes, by index among the track's events, in order; what they
// are, for messages and undo ("4 notes in Bass", "bar 3 of Bass"); and whether only notes are
// to be moved in time, as in the piano roll, which shows only notes, while elsewhere what
// moves in time moves together, controllers too.
// Where a range's edges cut notes, when cutting at the edges (|, decided October 2026): its
// tracks, its time, and with V or Ctrl-v in the piano roll its pitches, lowest and highest.
struct CutRange
{
    std::vector<core::TrackId> tracks;
    core::Tick from = 0;
    core::Tick to = 0;
    std::optional<std::pair<std::uint8_t, std::uint8_t>> pitches;

    bool takes(const core::Note& note) const
    {
        return !pitches || (note.pitch >= pitches->first && note.pitch <= pitches->second);
    }
};

struct OperatorTarget
{
    struct Part
    {
        core::TrackId track;
        std::vector<std::size_t> events;
    };

    std::vector<Part> parts;
    std::string what;
    bool notesOnly = false;
    // Cutting at the range's edges first, in the same change; the parts' events are then those
    // starting in it once cut.
    std::optional<CutRange> cut = std::nullopt;
    // The stretch of time it covers, where the view can say, for the pedalling tools, which act
    // on the time and not only the notes a range takes in the piano roll.
    std::optional<std::pair<core::Tick, core::Tick>> span = std::nullopt;
};

// What a view asks of the workspace that shows it.
class ViewHost
{
public:
    virtual ~ViewHost() = default;

    // Shows a message in the status line until the next key.
    virtual void setMessage(std::string message, MessageKind kind) = 0;
    // A command not doing what was asked, refused or given up, saying why: a macro stops at
    // it, whatever its message's kind (decided November 2026), and only then; a warning from a
    // command that did what was asked doesn't stop one.
    virtual void refuse(std::string message, MessageKind kind = MessageKind::Warning) = 0;

    // A view starts or stops taking typed keys itself, as the event list does when a field is
    // edited in place: the mode is the command line's while it does.
    virtual void startTyping() = 0;
    virtual void stopTyping() = 0;

    // The unnamed register, shared by the views.
    virtual EventRegister& eventRegister() = 0;

    // The player, for the play-from marker, if there is one.
    virtual const core::Player* player() const = 0;

    // Whether to mark where a quantised track's notes will be heard (quantizemarks).
    virtual bool quantizeMarks() const = 0;

    // The tracks being moved through the others, to show them.
    virtual std::vector<core::TrackId> movingTracks() const = 0;

    // The output ports there are now, to choose from.
    virtual std::vector<std::string> outputPorts() const = 0;
    virtual std::vector<std::string> inputPorts() const { return {}; }

    // Shows another view at the cursor; or opens it afresh there, as Enter on a bar in the
    // arrangement does (see View::open).
    virtual void show(ViewKind kind) = 0;
    virtual void open(ViewKind kind) = 0;

    // Opens the : prompt with a line in it to edit, as renaming a section does.
    virtual void openPrompt(std::string line) = 0;
    // Asks for a name at the prompt, after a label, with one there to edit; answer runs with
    // what's given (Enter), and Escape runs nothing.
    virtual void askName(std::string label, std::string name,
                         std::function<void(const std::string&)> answer) = 0;

    // Asks a question in the status line, running yes if the next key is y and no for any
    // other, or for Escape stop, if there's one, which stops a run of questions; with nothing
    // to run, it says nothing was done.
    virtual void ask(std::string question, std::function<void()> yes, std::function<void()> no = {},
                     std::function<void()> stop = {}) = 0;

    // Puts the play-from marker at a time and plays from there, as Enter on a section does.
    virtual void playFrom(core::Tick tick) = 0;

    // The unit used to display coarse/fine note timing; by default the track's editing grid.
    virtual core::NoteFraction timingUnit(core::NoteFraction grid) const { return grid; }

    // The Visual selection, if there is one; operators take it as the motion "visual".
    virtual std::optional<Selection> selection() const = 0;
    // Moves it with what it took, as its notes moved: its start and pitch, the cursor being
    // moved by the view itself.
    virtual void moveSelection(core::Tick by, int pitches,
                               std::vector<Selection::NotePosition> notes) = 0;

    // A meter the user chose, typed in a field or at the prompt, offered from now on if it
    // isn't among the built-in ones.
    virtual void chosenMeter(core::TimeSignature /*meter*/) {}

    // The time of the mark an operator's range goes to ('a), for the motion cursor-mark.
    virtual std::optional<core::Tick> jumpTime() const { return std::nullopt; }

    // What a find goes to (fE, a pitch class), for the motions cursor-find-next and
    // cursor-find-previous; and what the last search looks for (/C1), for cursor-match-next and
    // cursor-match-previous, which go round past the ends, as Vim's do, saying so.
    virtual std::optional<core::EventQuery> findQuery() const { return std::nullopt; }
    virtual std::optional<core::EventQuery> searchQuery() const { return std::nullopt; }
    virtual void searchWrapped(bool /*forward*/) {}
};

// A view of the song in the workspace: the event list, and later the arrangement and the
// piano roll. It draws its own area, its bar above it and the inspector; the workspace draws
// the status line and the : prompt, handles keys, and runs the commands every view shares.
class View
{
public:
    virtual ~View() = default;

    // Its name, for messages: "the event list".
    virtual std::string_view name() const = 0;

    // The heights of the view's regions at the top of the window, in pixels: its bar, then
    // the inspector.
    virtual std::vector<int> topHeights(CellSize cell) const = 0;

    // Fits the view to the window's layout, scrolling if the cursor would be out of view.
    virtual void resize(const WindowLayout& layout, int width, CellSize cell) = 0;

    // Runs one of the view's own commands, returning whether what's shown changed, or nothing
    // if it has no command of that name.
    virtual std::optional<bool> run(std::string_view command, int count) = 0;

    // Runs one of the view's own commands that take arguments from the : line, such as
    // :section-split NAME, or nothing if it has no such command.
    virtual std::optional<CommandResult> runLine(std::string_view /*command*/,
                                                 std::string_view /*arguments*/)
    {
        return std::nullopt;
    }

    // Runs an operator (operator-delete or operator-yank) over where a motion goes, or with
    // operator-line over the cursor's place and as many after it as the count says.
    virtual bool operate(std::string_view op, std::string_view motion, int count) = 0;

    // Where a range, object or the selection (visual) cuts notes, when cutting at the edges,
    // or nothing where it can't, as in the event list, whose ranges are rows.
    virtual std::optional<CutRange> cutRange(std::string_view /*motion*/, int /*count*/)
    {
        return std::nullopt;
    }

    // What an operator other than d and y takes over a range or object here, or nothing if it
    // takes nothing, having said why where it's not plain.
    virtual std::optional<OperatorTarget> target(std::string_view /*motion*/, int /*count*/)
    {
        return std::nullopt;
    }

    // What an operator would take over a range or object here, for the hint menu after it
    // ("this bar's notes"), or nothing if it takes nothing over it in this view.
    virtual std::optional<std::string> describeRange(std::string_view /*op*/,
                                                     std::string_view /*motion*/) const
    {
        return std::nullopt;
    }

    // Its area, its bar and the inspector, in that order.
    virtual void addRegions(Frame& frame) const = 0;

    // The event under the cursor as what to search for (* and #): a note's pitch, or a
    // controller, or nothing if there's none; and where the cursor is among the matches of a
    // search, for the status line ("match 3/24").
    virtual std::optional<core::EventQuery> queryHere() const { return std::nullopt; }
    virtual std::string matchStatus(const core::EventQuery& /*query*/) const { return {}; }
    // The events of the cursor's track a query matches at the cursor, for gn: the note there,
    // the event list's row, or in the arrangement those in the cursor's bar; nothing if none.
    virtual std::optional<OperatorTarget::Part> matchesHere(const core::EventQuery& /*query*/) const
    {
        return std::nullopt;
    }

    // The Visual selection here, as words for the status line ("bars 3-4 on every track"), or
    // nothing if the view can't select; its span of time; its own index at the cursor, for a
    // selection started
    // there (see Selection); and the span of time an object takes, which Visual mode selects
    // (vis), or nothing, having said why.
    virtual std::optional<std::string> selectionStatus() const { return std::nullopt; }
    // A warning about what the selection would do, shown after it, as a box across a
    // section's start, whose time D takes out of each section apart.
    virtual std::optional<std::string> selectionWarning() const { return std::nullopt; }
    virtual std::optional<std::pair<core::Tick, core::Tick>> selectionSpan() const
    {
        return std::nullopt;
    }
    virtual std::optional<std::size_t> selectionIndex() const { return std::nullopt; }
    // The time whole rows (V) take when they're first selected, if not the whole song's.
    virtual std::optional<std::pair<core::Tick, core::Tick>> rowsSpan() const
    {
        return std::nullopt;
    }
    virtual std::optional<std::pair<core::Tick, core::Tick>> objectSpan(std::string_view /*object*/,
                                                                        int /*count*/)
    {
        return std::nullopt;
    }
    // Makes a selection what an object takes, the cursor at its far end: by default from
    // the start of its span to its last tick; or for whole rows, its time.
    virtual void selectObject(std::string_view object, int count, Selection& selection)
    {
        if (const auto span = objectSpan(object, count);
            span && selection.kind == SelectionKind::Rows)
        {
            selection.span = span;
        }
        else if (span && selection.kind == SelectionKind::Box)
        {
            // Its time, keeping the rows: the corner it started at keeps its track or pitch.
            const auto here = cursor();
            selection.anchor.tick = span->first;
            setCursor(SongCursor{here.track, span->second - 1, here.pitch});
        }
        else if (span)
        {
            const auto here = cursor();
            selection.anchor = SongCursor{here.track, span->first, here.pitch};
            selection.index.reset();
            setCursor(SongCursor{here.track, span->second - 1, here.pitch});
        }
    }

    // A time's position, in the view's own bars.
    virtual std::string position(core::Tick tick) const = 0;

    // The shared cursor, and putting it somewhere, as when coming from another view.
    virtual SongCursor cursor() const = 0;
    virtual void setCursor(const SongCursor& cursor) = 0;

    // Opens the view afresh at the cursor, as Enter on a bar in the arrangement opens the piano
    // roll: the same as setCursor, unless the view keeps a scroll it should start again from.
    virtual void open(const SongCursor& cursor) { setCursor(cursor); }

    // The meter the cursor's time is counted in, in the view's own bars: its track's, or the
    // conductor's in its list.
    virtual const core::MeterMap& cursorMeter() const = 0;

    // Where an edit is made, for undo; and going back there, after undo or redo.
    virtual core::EditPlace place() const = 0;
    virtual void restore(const core::EditPlace& place) = 0;

    // Shows the song as it is now, keeping the cursor's place; or a new song from its start.
    virtual void refresh() = 0;
    virtual void showSong() = 0;

    // The playhead while playing, the view and the cursor following it as asked; then its end,
    // the cursor going to a time if one is given.
    virtual void showPlayhead(core::Tick playhead, Follow follow) = 0;
    virtual void stopPlayhead(std::optional<core::Tick> cursorTo) = 0;

    // Typing in place, as the event list's field editing does, once the view has called
    // startTyping: whether it is, a typed character, and the command line's commands, which
    // edit its line rather than the : prompt's.
    virtual bool typing() const { return false; }
    // What can be typed into the field being edited, and what the keys do, for the status line.
    virtual std::optional<std::string> typingHint() const { return std::nullopt; }

    // The editing grid the view steps by, for the status line's settings, if it has one.
    virtual std::optional<std::string> gridStatus() const { return std::nullopt; }
    virtual void type(char32_t /*character*/) {}
    virtual std::optional<bool> runTyping(std::string_view /*command*/) { return std::nullopt; }
};

// Runs one of the command line's editing commands (command-line-backspace, -delete, -left,
// -right, -start, -end, -clear, -older, -newer) on a line, returning whether it changed, or
// nothing for another command.
std::optional<bool> editLine(CommandLine& line, std::string_view command);

} // namespace modal::ui
