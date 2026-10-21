#pragma once

#include <modal/core/document.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/event_list.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>

#include <chrono>
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
    EventListView(CommandRegistry& registry, core::Document& document,
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

    void setKeymap(Keymap keymap);
    void setTimeout(std::int64_t timeout) { keys_.setTimeout(timeout); }

    // How far apart events can start and still be one chord, for ( and ) (chordwindow).
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }

    Frame frame() const;

    // The track shown: 0 for the conductor, then 1 for the first track.
    std::size_t track() const { return track_; }
    const EventList& list() const { return list_; }
    std::size_t row() const { return row_; }
    Column field() const; // the cursor's, on its row
    std::size_t top() const { return top_; }
    int viewRows() const; // rows of events fully shown

    // Normal; OperatorPending after d or y, waiting for a motion; or CommandLine at the :
    // prompt, or editing a field in place.
    Mode mode() const { return mode_; }
    const CommandLine& commandLine() const { return line_; }

    // Whether a field is being edited in place (after Enter), and what's typed there.
    bool editingField() const { return field_.has_value(); }
    const CommandLine& fieldLine() const { return fieldLine_; }
    std::string pending() const; // the operator, count and keys typed so far

    // What the unnamed register holds: events yanked or deleted, their ticks from the first's.
    const std::vector<core::Event>& registerEvents() const { return register_; }

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

    // Moves to the first row at or after a time a motion reached. Going back, if that row
    // isn't earlier than the cursor's, the motion is taken again from there until it is, or it
    // can go no further, so it never sticks.
    void land(core::Tick target, bool forward, const std::function<core::Tick(core::Tick)>& again);

    // The rows starting chords: events starting together, within the chord window of the
    // first of them; in the conductor's list, its rows.
    std::vector<std::size_t> chordStarts() const;

    // The keymap in operator-pending mode: the Normal mode's motions, the operator's own keys
    // again for the event under the cursor (dd), and Escape, unless omap binds those keys.
    void buildOperatorKeymap();

    // Runs a motion or the operator's own key after an operator, then the operator on the
    // rows from the cursor's to where the motion goes.
    std::optional<bool> runOperator(std::string_view command, int count);

    // Deletes or yanks the track's own events among some rows, which may be in either order.
    bool operate(std::string_view op, std::size_t from, std::size_t to);
    bool put();
    bool undoOrRedo(bool undo);

    // The line keys type into: a field's, while one is being edited, or the : prompt's.
    CommandLine& activeLine() { return field_ ? fieldLine_ : line_; }

    // Starts editing the field under the cursor in place, with its value in it; then sets it
    // from what's typed, or steps it.
    bool openField();
    bool addNote(); // o: a note at the cursor's time, editing its pitch first
    bool finishField();
    bool stepFieldBy(int steps);

    // The conductor's list: sets a row's value (a section's or marker's name, a tempo, a
    // meter) from text or steps it, or deletes the tempo and meter changes and markers among
    // some rows, but not a section's own tempo and meter at its start, nor a section.
    std::optional<std::string> setConductor(std::size_t row, std::string_view text);
    std::optional<std::string> stepConductor(std::size_t row, int steps);
    bool deleteConductor(std::size_t from, std::size_t to);

    // Replaces the event of a row with a changed one, if it doesn't overlap a note of its
    // pitch, and puts the cursor on it. Returns why not, or nothing.
    std::optional<std::string> replaceEvent(std::size_t row, const core::Event& changed,
                                            Column field);

    // Changes the song as one undo step, made where the cursor is, and shows the change.
    bool edit(std::string title, const std::function<void(core::Song&)>& change);
    core::EditPlace place() const;
    std::string trackName() const;
    void moveFields(int by);
    void follow();

    Region listRegion() const;
    Region promptRegion() const;
    void addBars(Frame& frame) const;

    CommandRegistry& commands_;
    core::Document& document_;
    std::size_t track_ = 0;
    EventList list_;
    std::size_t row_ = 0;
    Column wanted_ = Column::Position;    // the field the cursor keeps to, as rows change
    std::optional<core::Tick> barOffset_; // where in the bar { and } keep to, while repeated
    std::chrono::milliseconds chordWindow_{30};
    std::size_t top_ = 0;
    Keymap keymap_;
    Keymap operatorKeymap_;
    KeyDispatcher keys_;
    struct PendingOperator
    {
        std::string name; // operator-delete or operator-yank
        int count;
    };
    std::optional<PendingOperator> operator_;
    struct FieldPrompt
    {
        std::size_t row;
        Column column;
        bool fresh;                        // typing replaces the value; keys that edit it keep it
        std::optional<core::Event> adding; // a note being added by o, shown after the row
    };
    std::optional<FieldPrompt> field_;
    CommandLine fieldLine_;
    std::vector<core::Event> register_;
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
