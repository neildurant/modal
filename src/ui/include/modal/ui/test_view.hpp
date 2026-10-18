#pragma once

#include <modal/ui/arrange_page.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/grid_cursor.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>

#include <cstdint>
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

    // The commands keys can be bound to, until there's an editor with commands of its own.
    static std::span<const CommandInfo> commands();

    // The keymap of the built-in modalrc alone.
    static Keymap defaultKeymap();

    explicit TestView(Keymap keymap = defaultKeymap(), std::int64_t timeout = kDefaultTimeout);

    // Fits the view to the window, scrolling if the cursor would be out of it.
    void resize(int width, int height, CellSize cell);

    // Handles a key pressed at a time in milliseconds, through the keymap, returning whether
    // what's shown changed.
    bool handle(const Key& key, std::int64_t now);

    // When wake should next be called, for keys waiting on a timeout; and the call, which
    // returns whether what's shown changed.
    std::optional<std::int64_t> deadline() const { return keys_.deadline(); }
    bool wake(std::int64_t now);

    // Runs a command with a count (0 for none), returning whether it changed what's shown,
    // or nothing if there's no such command.
    std::optional<bool> run(std::string_view command, int count);

    Frame frame() const;

    Page page() const { return page_; }
    const GridCursor& cursor() const { return cursor_; }
    const ArrangePage& arrange() const { return arrange_; }
    std::string pending() const { return keys_.pending(); } // the count and keys typed so far

    // Shows a message in the status line, as a warning, until the next key.
    void setMessage(std::string message) { message_ = std::move(message); }
    const std::string& message() const { return message_; }

private:
    bool runAll(const std::vector<Action>& actions);
    Region demoRegion() const;
    Region cellsRegion() const;
    Region samplesRegion() const;
    void addBars(Frame& frame) const;

    Page page_ = Page::Grid;
    GridCursor cursor_;
    ArrangePage arrange_;
    Keymap keymap_;
    KeyDispatcher keys_;
    std::string message_;
    int width_ = 0;
    int height_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
