#pragma once

#include <modal/core/document.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/event_list.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace modal::ui
{

// The event list (decided in milestone 4): one track's events at a time, with the
// conductor's section starts, meter and tempo changes and markers among them, dimmed, and
// the end of the song last; or the conductor's own list, track 0. A row for each event,
// with its fields in columns under fixed headings; the cursor is on one field of one row.
// A bar at the top names the track and the section the cursor is in, and the status line at
// the bottom shows the mode, the cursor's position, keys waiting and messages, or the :
// prompt.
class EventListView
{
public:
    enum class MessageKind : std::uint8_t
    {
        Information, // in the message colour
        Warning,     // in the warning colour
    };

    // The view's commands.
    static std::span<const CommandInfo> commands();

    // The keymap of the built-in modalrc alone, whose bindings run the view's commands and
    // the window's (see windowCommands).
    static Keymap defaultKeymap();

    // Shows the document's song, from track 1 (or the conductor's list, if there are no
    // tracks). Gives the registry what runs the view's commands, which keys then run through
    // it with the registry's others, such as the window's. The registry and the document
    // outlive the view, which can't be copied or moved, since the registry runs its commands
    // on it.
    EventListView(CommandRegistry& registry, const core::Document& document,
                  Keymap keymap = defaultKeymap(), std::int64_t timeout = kDefaultTimeout);
    EventListView(const EventListView&) = delete;
    EventListView& operator=(const EventListView&) = delete;

    // Fits the view to the window, scrolling if the cursor would be out of it.
    void resize(int width, int height, CellSize cell);

    // Handles a key pressed at a time in milliseconds, through the keymap, returning whether
    // what's shown changed.
    bool handle(const Key& key, std::int64_t now);

    // When wake should next be called, for keys waiting on a timeout; and the call, which
    // returns whether what's shown changed.
    std::optional<std::int64_t> deadline() const { return keys_.deadline(); }
    bool wake(std::int64_t now);

    // Runs a command through the registry with a count (0 for none), as a key bound to it
    // does, showing any message it gives.
    CommandResult run(std::string_view command, int count);

    // Shows the document's song as it is now, as after a change, keeping the cursor at the
    // same time on the same track if it's still there.
    void refresh();

    // What runs a line entered at the : prompt, such as the program's command runner.
    using LineHandler = std::function<void(const std::string& line)>;
    void setLineHandler(LineHandler handler) { lineHandler_ = std::move(handler); }

    void setKeymap(Keymap keymap) { keymap_ = std::move(keymap); }
    void setTimeout(std::int64_t timeout) { keys_.setTimeout(timeout); }

    Frame frame() const;

    // The track shown: 0 for the conductor, then 1 for the first track.
    std::size_t track() const { return track_; }
    const EventList& list() const { return list_; }
    std::size_t row() const { return row_; }
    Column field() const; // the cursor's, on its row
    std::size_t top() const { return top_; }
    int viewRows() const; // rows of events fully shown

    Mode mode() const { return mode_; } // Normal, or CommandLine at the : prompt
    const CommandLine& commandLine() const { return line_; }
    std::string pending() const { return keys_.pending(); } // the count and keys typed so far

    // Shows a message in the status line until the next key.
    void setMessage(std::string message, MessageKind kind)
    {
        message_ = std::move(message);
        messageKind_ = kind;
    }
    const std::string& message() const { return message_; }

private:
    bool runAll(const std::vector<Action>& actions);
    std::optional<bool> runOwn(std::string_view command, int count);
    std::optional<bool> runCommandLine(std::string_view command);

    // Shows a track's list, the cursor at or after a tick.
    void showTrack(std::size_t track, core::Tick tick);
    EventList listOf(std::size_t track) const;
    void moveRows(std::int64_t by);
    void moveFields(int by);
    void follow();

    Region listRegion() const;
    Region promptRegion() const;
    void addBars(Frame& frame) const;

    CommandRegistry& commands_;
    const core::Document& document_;
    std::size_t track_ = 0;
    EventList list_;
    std::size_t row_ = 0;
    Column wanted_ = Column::Position; // the field the cursor keeps to, as rows change
    std::size_t top_ = 0;
    Keymap keymap_;
    KeyDispatcher keys_;
    std::string message_;
    MessageKind messageKind_ = MessageKind::Information;
    LineHandler lineHandler_;
    Mode mode_ = Mode::Normal;
    CommandLine line_;
    int width_ = 0;
    int height_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
