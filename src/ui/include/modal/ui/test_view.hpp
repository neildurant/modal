#pragma once

#include <modal/ui/arrange_page.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/grid_cursor.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace modal::ui
{

// What the program shows until the event list arrives (milestone 4), to try out the grid
// renderer: a large grid of labelled cells to move the cursor around, with headers that stay
// put as it scrolls, under a demo of bar lines in two meters on one time scale; a sketch of
// the arrangement, with a made-up song (see ArrangePage); and a page of samples of each
// theme role and the characters the views use.
class TestView
{
public:
    enum class MessageKind : std::uint8_t
    {
        Information, // in the message colour
        Warning,     // in the warning colour
    };

    enum class Page : std::uint8_t
    {
        Grid,
        Arrange,
        Samples,
    };

    static constexpr int kGridRows = 1000;
    static constexpr int kGridColumns = 1000;
    static constexpr int kCellColumns = 10;    // characters to a labelled cell
    static constexpr int kRowLabelColumns = 5; // the row numbers down the left
    static constexpr int kDemoRows = 6;        // the bar-line demo, above the grid

    // The view's commands, until there's an editor with commands of its own.
    static std::span<const CommandInfo> commands();

    // The keymap of the built-in modalrc alone, whose bindings run the view's commands and
    // the window's (see windowCommands).
    static Keymap defaultKeymap();

    // Gives the registry what runs the view's commands, which keys then run through it, with
    // the registry's other commands, such as the window's. The registry outlives the view;
    // the view can't be copied or moved, since the registry runs its commands on it.
    explicit TestView(CommandRegistry& registry, Keymap keymap = defaultKeymap(),
                      std::int64_t timeout = kDefaultTimeout);
    TestView(const TestView&) = delete;
    TestView& operator=(const TestView&) = delete;

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

    // What runs a line entered at the : prompt, such as the program's command runner.
    using LineHandler = std::function<void(const std::string& line)>;
    void setLineHandler(LineHandler handler) { lineHandler_ = std::move(handler); }

    // Replaces the keymap, as when the config changes it.
    void setKeymap(Keymap keymap) { keymap_ = std::move(keymap); }

    // How long to wait for a longer key binding, in milliseconds (timeoutlen).
    void setTimeout(std::int64_t timeout) { keys_.setTimeout(timeout); }

    Frame frame() const;

    Page page() const { return page_; }
    const GridCursor& cursor() const { return cursor_; }
    const ArrangePage& arrange() const { return arrange_; }
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
    Region demoRegion() const;
    Region cellsRegion() const;
    Region samplesRegion() const;
    Region promptRegion() const;
    std::optional<bool> runCommandLine(std::string_view command);
    void addBars(Frame& frame) const;

    CommandRegistry& commands_;
    Page page_ = Page::Grid;
    GridCursor cursor_;
    ArrangePage arrange_;
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
