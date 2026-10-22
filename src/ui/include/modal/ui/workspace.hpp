// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/view.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The window's contents: the views of the song, one shown at a time, with what they share
// (decided for milestone 5). The workspace handles keys through the keymap, with counts and
// pending operators, runs the commands every view has (the : prompt, playing, undo, :port)
// and hands the rest to the view shown; it draws that view, then the status line, or the :
// prompt, at the bottom.
class Workspace final : private ViewHost
{
public:
    // Every command the workspace and its views run.
    static std::span<const CommandInfo> commands();

    // The keymap of the built-in modalrc alone, whose bindings run the workspace's commands
    // and the window's (see windowCommands).
    static Keymap defaultKeymap();

    // Shows the document's song, in the event list from track 1 (or the conductor's list, if
    // there are no tracks). Gives the registry what runs the commands, which keys then run
    // through it with the registry's others, such as the window's. The registry and the
    // document outlive the workspace, which can't be copied or moved, since the registry runs
    // its commands on it.
    Workspace(CommandRegistry& registry, core::Document& document, Keymap keymap = defaultKeymap(),
              std::int64_t timeout = kDefaultTimeout);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    ~Workspace() override;

    EventListView& eventList() { return *eventList_; }
    const EventListView& eventList() const { return *eventList_; }

    // Fits the views to the window, scrolling if the cursor would be out of view.
    void resize(int width, int height, CellSize cell);

    // Handles a key pressed at a time in milliseconds, through the keymap, returning whether
    // what's shown changed.
    bool handle(const Key& key, std::int64_t now);

    // When wake should next be called, for keys waiting on a timeout, or for the next frame
    // while playing; and the call, which returns whether what's shown changed.
    std::optional<std::int64_t> deadline() const;
    bool wake(std::int64_t now);

    // How often to draw a frame while playing, in milliseconds; the display's refresh rate
    // may hold it back further.
    static constexpr std::int64_t kFrameInterval = 8;

    Frame frame() const;

    // Runs a command through the registry with a count (0 for none), as a key bound to it
    // does, showing any message it gives.
    CommandResult run(std::string_view command, int count);

    // Shows the document's song as it is now, keeping the cursor's place; or a song new to
    // the document, opened or new, from its start.
    void refresh();
    void showSong();

    // The player the transport commands play the song with, or none, when they do nothing.
    // It must outlive the workspace.
    void setPlayer(core::Player* player);

    // Whether the play-from marker stays put when playback stops (the rewind setting), and the
    // cursor stays where it was; otherwise the marker moves to where playback stops, and the
    // cursor follows the playhead there.
    void setRewind(bool rewind);
    bool rewind() const { return rewind_; }

    // What a port name given to :port refers to: the output port's full name, or else the
    // ports it could be when several match equally well (none when none match); and every
    // output port there is. An empty name only lists them.
    struct PortLookup
    {
        std::optional<std::string> port;
        std::vector<std::string> candidates;
        std::vector<std::string> ports;
    };
    using PortFinder = std::function<PortLookup(std::string_view name)>;
    void setPortFinder(PortFinder finder) { portFinder_ = std::move(finder); }

    // What runs a line entered at the : prompt, such as the program's command runner.
    using LineHandler = std::function<void(const std::string& line)>;
    void setLineHandler(LineHandler handler) { lineHandler_ = std::move(handler); }

    void setKeymap(Keymap keymap);
    void setTimeout(std::int64_t timeout) { keys_.setTimeout(timeout); }

    // Normal; OperatorPending after d or y, waiting for a motion; or CommandLine at the :
    // prompt, or while a view takes typing, as when editing a field in place.
    Mode mode() const { return mode_; }
    const CommandLine& commandLine() const { return line_; }
    std::string pending() const; // the operator, count and keys typed so far

    // What the unnamed register holds: events yanked or deleted, their ticks from the first's.
    const std::vector<core::Event>& registerEvents() const { return register_; }

    // Shows a message in the status line until the next key.
    void setMessage(std::string message, MessageKind kind) override
    {
        message_ = std::move(message);
        messageKind_ = kind;
    }
    const std::string& message() const { return message_; }

private:
    void startTyping() override { mode_ = Mode::CommandLine; }
    void stopTyping() override { mode_ = Mode::Normal; }
    std::vector<core::Event>& eventRegister() override { return register_; }
    const core::Player* player() const override { return player_; }

    View& view() const { return *eventList_; }

    bool runAll(const std::vector<Action>& actions);

    // Runs a command the workspace has itself, returning whether what's shown changed, or
    // nothing if it isn't one; or a view's command, through the view shown.
    std::optional<bool> runShared(std::string_view command, int count);
    bool runViewCommand(std::string_view command, int count);
    std::optional<bool> runCommandLine(std::string_view command);

    // The keymap in operator-pending mode: the Normal mode's motions, the operator's own keys
    // again for the cursor's place (dd), and Escape, unless omap binds those keys.
    void buildOperatorKeymap();

    // Runs a motion or the operator's own key after an operator, then the operator.
    bool runOperator(std::string_view command, int count);

    bool undoOrRedo(bool undo);

    // Runs a transport command; then shows the playhead, or where playback stopped.
    bool transport(std::string_view command);
    void showPlayhead();

    // :port NAME sets the cursor's track's output port; :port alone says what it is.
    CommandResult port(std::string_view name);

    Region promptRegion() const;
    Region statusRegion() const;

    CommandRegistry& commands_;
    core::Document& document_;
    std::unique_ptr<EventListView> eventList_;
    Keymap keymap_;
    Keymap operatorKeymap_;
    KeyDispatcher keys_;
    struct PendingOperator
    {
        std::string name; // operator-delete or operator-yank
        int count;
    };
    std::optional<PendingOperator> operator_;
    std::vector<core::Event> register_;
    std::string message_;
    MessageKind messageKind_ = MessageKind::Information;
    LineHandler lineHandler_;
    PortFinder portFinder_;
    Mode mode_ = Mode::Normal;
    CommandLine line_;
    int width_ = 0;
    int height_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
    core::Player* player_ = nullptr;
    bool rewind_ = true;
    bool wasPlaying_ = false;   // or stopping, until the player knows where it stopped
    std::int64_t lastTime_ = 0; // of the last key or wake-up
};

} // namespace modal::ui
