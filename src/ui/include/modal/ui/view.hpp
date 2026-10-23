// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/event.hpp>
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

// The views of the song (decided for milestone 5): the arrangement, the home view; the piano
// roll; and the event list.
enum class ViewKind : std::uint8_t
{
    Arrangement,
    PianoRoll,
    EventList,
};

// The unnamed register, shared by the views (decided for milestone 5): what was yanked or
// deleted. Either an area's events, a list for each of its tracks from the top down, their
// ticks from the area's start; or whole sections, from the section strip.
struct EventRegister
{
    std::vector<std::vector<core::Event>> tracks;
    std::vector<std::shared_ptr<const core::Section>> sections;

    bool empty() const
    {
        return sections.empty() &&
               std::ranges::all_of(tracks, [](const auto& events) { return events.empty(); });
    }
};

// What a view asks of the workspace that shows it.
class ViewHost
{
public:
    virtual ~ViewHost() = default;

    // Shows a message in the status line until the next key.
    virtual void setMessage(std::string message, MessageKind kind) = 0;

    // A view starts or stops taking typed keys itself, as the event list does when a field is
    // edited in place: the mode is the command line's while it does.
    virtual void startTyping() = 0;
    virtual void stopTyping() = 0;

    // The unnamed register, shared by the views.
    virtual EventRegister& eventRegister() = 0;

    // The player, for the play-from marker, if there is one.
    virtual const core::Player* player() const = 0;

    // Shows another view at the cursor; or opens it afresh there, as Enter on a bar in the
    // arrangement does (see View::open).
    virtual void show(ViewKind kind) = 0;
    virtual void open(ViewKind kind) = 0;

    // Opens the : prompt with a line in it to edit, as naming a new section does.
    virtual void openPrompt(std::string line) = 0;

    // Puts the play-from marker at a time and plays from there, as Enter on a section does.
    virtual void playFrom(core::Tick tick) = 0;
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
    // :section NAME, or nothing if it has no such command.
    virtual std::optional<CommandResult> runLine(std::string_view /*command*/,
                                                 std::string_view /*arguments*/)
    {
        return std::nullopt;
    }

    // Runs an operator (operator-delete or operator-yank) over where a motion goes, or with
    // operator-line over the cursor's place and as many after it as the count says.
    virtual bool operate(std::string_view op, std::string_view motion, int count) = 0;

    // Its area, its bar and the inspector, in that order.
    virtual void addRegions(Frame& frame) const = 0;

    // For the status line: where the cursor is, and where a time is, each as its position,
    // the meter there and its section, in the view's own bars; and a time's position alone.
    virtual std::string cursorStatus() const = 0;
    virtual std::string timeStatus(core::Tick tick) const = 0;
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

    // The playhead while playing, with the cursor following it if asked; then its end, the
    // cursor going to a time if one is given.
    virtual void showPlayhead(core::Tick playhead, bool cursorFollows) = 0;
    virtual void stopPlayhead(std::optional<core::Tick> cursorTo) = 0;

    // Typing in place, as the event list's field editing does, once the view has called
    // startTyping: whether it is, a typed character, and the command line's commands, which
    // edit its line rather than the : prompt's.
    virtual bool typing() const { return false; }
    virtual void type(char32_t /*character*/) {}
    virtual std::optional<bool> runTyping(std::string_view /*command*/) { return std::nullopt; }
};

// Runs one of the command line's editing commands (command-line-backspace, -delete, -left,
// -right, -start, -end, -clear, -older, -newer) on a line, returning whether it changed, or
// nothing for another command.
std::optional<bool> editLine(CommandLine& line, std::string_view command);

} // namespace modal::ui
