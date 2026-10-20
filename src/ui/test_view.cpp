#include <modal/core/meter_map.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/test_view.hpp>
#include <modal/ui/window_commands.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr Style kDim{Role::Dim, Role::Background, kPlain};
constexpr Style kHeader{Role::BarText, Role::Bar, kPlain};
constexpr Style kHeaderCursor{Role::Accent, Role::Bar, kBold};
constexpr Style kTitle{Role::Accent, Role::Background, kBold};

// The characters and box drawing the views will use, as on the samples page.
constexpr const char* kCharacters[] = {
    "Modal: the quick brown fox, 0123456789",
    "17:2:480 │ E3 (52) │ vel 96 │ dur 0:1:240 (1/8.) │ F♯3 B♭2",
    "┌───┬───┐ ┏━━━┳━━━┓ ┍━━━┯━━━┑   shades    ░▒▓█",
    "│ 1 │ 2 │ ┃ 1 ┃ 2 ┃ │ 1 │ 2 │   eighths   ▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█",
    "├───┼───┤ ┣━━━╋━━━┫ ┝━━━┿━━━┥   halves    ▀▄▌▐ ▔▕",
    "└───┴───┘ ┗━━━┻━━━┛ ┕━━━┷━━━┙   quadrants ▖▗▘▝ ▙▚▛▜▝▞▟",
    "density   ░░▒▒▓▓██▓▓▒▒░░  ▁▂▃▅▇█▇▅▃▂▁",
    "arrows    ←→↑↓ ▶◀▲▼ ●○ ·•…×±≤≥",
    "font's own (not drawn): ═║╔╗╚╝ ╭╮╰╯ ┄┆ ╱╲",
    "not in the font, so shown as a replacement: ♮♩",
    "ASCII     {}[]()<>|\\/\"'`~!@#$%^&*_+-=",
};

struct RoleSample
{
    const char* name;
    Style style;
    bool swatch; // a block of the role's colour before its name
};

// Every theme role, by the name highlight takes, in four columns: text, backgrounds with
// their text, timeline lines and markers, and density.
constexpr int kRoleGroups = 4;
constexpr RoleSample kRoles[kRoleGroups][5] = {
    {{"text", {Role::Text, Role::Background, kPlain}, false},
     {"dim", {Role::Dim, Role::Background, kPlain}, false},
     {"accent", {Role::Accent, Role::Background, kPlain}, false},
     {"warning", {Role::Warning, Role::Background, kPlain}, false},
     {"background", {Role::Background, Role::Text, kPlain}, false}},
    {{"cursor", {Role::CursorText, Role::Cursor, kPlain}, false},
     {"selection", {Role::Text, Role::Selection, kPlain}, false},
     {"bar", {Role::BarText, Role::Bar, kPlain}, false},
     {"mode", {Role::ModeText, Role::Mode, kBold}, false},
     {"message", {Role::Message, Role::Bar, kPlain}, false}},
    {{"grid-line", {Role::GridLine, Role::Background, kPlain}, true},
     {"beat-line", {Role::BeatLine, Role::Background, kPlain}, true},
     {"bar-line", {Role::BarLine, Role::Background, kPlain}, true},
     {"note", {Role::Note, Role::Background, kPlain}, true},
     {"marker", {Role::Marker, Role::Background, kPlain}, true}},
    {{"density-1", {Role::Density1, Role::Background, kPlain}, true},
     {"density-2", {Role::Density2, Role::Background, kPlain}, true},
     {"density-3", {Role::Density3, Role::Background, kPlain}, true},
     {"density-4", {Role::Density4, Role::Background, kPlain}, true},
     {nullptr, {}, false}},
};

std::string number(int value)
{
    return std::to_string(value);
}

// Text right-aligned in a width, cut from its left if too long.
std::string rightAligned(const std::string& text, int width)
{
    const auto size = static_cast<int>(text.size());
    return size >= width ? text.substr(static_cast<std::size_t>(size - width))
                         : std::string(static_cast<std::size_t>(width - size), ' ') + text;
}

} // namespace

Keymap TestView::defaultKeymap()
{
    Config config;
    CommandRegistry all;
    all.add(commands());
    all.add(windowCommands());
    ConfigReader reader{config, all};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    assert(reader.errors().empty()); // the tests run the built-in modalrc too
    return config.keymap;
}

TestView::TestView(CommandRegistry& registry, Keymap keymap, std::int64_t timeout)
    : commands_(registry)
    , cursor_(kGridRows, kGridColumns)
    , arrange_(demoSong())
    , keymap_(std::move(keymap))
    , keys_(timeout)
{
    for (const auto& info : commands())
    {
        registry.add(info, [this, name = info.name](const CommandCall& call)
                     { return CommandResult{runOwn(name, call.count).value_or(false)}; });
    }
}

void TestView::resize(int width, int height, CellSize cell)
{
    width_ = width;
    height_ = height;
    cell_ = {std::max(cell.width, 1), std::max(cell.height, 1)};
    layout_ = layoutWindow(width, height, cell_, 1, 1);
    // Below the demo and the column numbers, right of the row numbers, the labelled cells
    // that are fully shown.
    cursor_.setView(layout_.fullRows - kDemoRows - 1,
                    (layout_.fullColumns - kRowLabelColumns) / kCellColumns);
    arrange_.resize(layout_.main, cell_);
}

namespace
{

// A command that takes a count, and one that works only at the : prompt.
constexpr CommandInfo counted(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true};
}

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

constexpr std::array kCommands{
    counted("cursor-left", "Move left a column, or a bar"),
    counted("cursor-down", "Move down a row, or a track"),
    counted("cursor-up", "Move up a row, or a track"),
    counted("cursor-right", "Move right a column, or a bar"),
    counted("cursor-first", "Move to the first column, or bar"),
    counted("cursor-last", "Move to the last column, or bar"),
    counted("go-first", "Go to the first row or bar, or the one counted"),
    counted("go-last", "Go to the last row or bar, or the one counted"),
    counted("cursor-left-scroll", "Move left, scrolling to keep the cursor in place"),
    counted("cursor-down-scroll", "Move down, scrolling to keep the cursor in place"),
    counted("cursor-up-scroll", "Move up, scrolling to keep the cursor in place"),
    counted("cursor-right-scroll", "Move right, scrolling to keep the cursor in place"),
    counted("scroll-half-down", "Scroll down half a page"),
    counted("scroll-half-up", "Scroll up half a page"),
    counted("scroll-page-down", "Scroll down a page"),
    counted("scroll-page-up", "Scroll up a page"),
    CommandInfo{.name = "scroll-centre",
                .description = "Scroll the cursor to the middle of the view"},
    counted("zoom-in", "Zoom in"),
    counted("zoom-out", "Zoom out"),
    CommandInfo{.name = "zoom-reset", .description = "Zoom to the default"},
    CommandInfo{.name = "test-next-page", .description = "Show the next page of the test view"},
    CommandInfo{.name = "cancel", .description = "Cancel"},
    CommandInfo{.name = "command-line", .description = "Open the : command line"},
    atPrompt("command-line-run", "Run the command line"),
    atPrompt("command-line-cancel", "Close the command line"),
    atPrompt("command-line-backspace", "Delete the character before the cursor"),
    atPrompt("command-line-delete", "Delete the character at the cursor"),
    atPrompt("command-line-left", "Move the cursor left"),
    atPrompt("command-line-right", "Move the cursor right"),
    atPrompt("command-line-start", "Move the cursor to the start"),
    atPrompt("command-line-end", "Move the cursor to the end"),
    atPrompt("command-line-clear", "Clear the command line"),
    atPrompt("command-line-older", "Recall the line run before"),
    atPrompt("command-line-newer", "Recall the line run after"),
};

} // namespace

std::span<const CommandInfo> TestView::commands()
{
    return kCommands;
}

bool TestView::handle(const Key& key, std::int64_t now)
{
    const bool hadMessage = !std::exchange(message_, std::string{}).empty();
    // At the prompt, a character that starts no command-line binding is typed.
    if (mode_ == Mode::CommandLine && keys_.pending().empty() && key.named == NamedKey::None &&
        (key.modifiers & (kCtrl | kAlt)) == 0)
    {
        const std::vector<Key> keys{key};
        const auto match = keymap_.match(Mode::CommandLine, keys);
        if (match.binding == nullptr && !match.longer)
        {
            line_.insert(key.character);
            return true;
        }
    }
    const auto before = keys_.pending();
    const bool changed = runAll(keys_.key(keymap_, mode_, key, now));
    return changed || hadMessage || keys_.pending() != before;
}

bool TestView::wake(std::int64_t now)
{
    const auto before = keys_.pending();
    const bool changed = runAll(keys_.timeout(keymap_, now));
    return changed || keys_.pending() != before;
}

bool TestView::runAll(const std::vector<Action>& actions)
{
    bool changed = false;
    for (const auto& action : actions)
    {
        changed = run(action.command, action.count).changed || changed;
    }
    return changed;
}

CommandResult TestView::run(std::string_view command, int count)
{
    auto result = commands_.run(command, count, mode_);
    if (!result.message.empty())
    {
        setMessage(result.message,
                   result.warning ? MessageKind::Warning : MessageKind::Information);
    }
    return result;
}

std::optional<bool> TestView::runOwn(std::string_view command, int count)
{
    const int n = std::max(count, 1);
    const bool grid = page_ == Page::Grid;
    const bool arrange = page_ == Page::Arrange;
    // Runs the action for the page shown, if it has one.
    const auto onPage = [&](auto onGrid, auto onArrange)
    {
        if (grid)
        {
            onGrid();
        }
        else if (arrange)
        {
            onArrange();
        }
        return grid || arrange;
    };
    const auto rows = [&](bool half)
    {
        const int view = grid ? cursor_.viewRows() : arrange_.viewTracks();
        return std::max(half ? view / 2 : view - 2, 1); // a page keeps two rows, as in Vim
    };

    if (command == "cursor-left")
    {
        return onPage([&] { cursor_.moveBy(0, -n); }, [&] { arrange_.moveBars(-n); });
    }
    if (command == "cursor-right")
    {
        return onPage([&] { cursor_.moveBy(0, n); }, [&] { arrange_.moveBars(n); });
    }
    if (command == "cursor-down")
    {
        return onPage([&] { cursor_.moveBy(n, 0); }, [&] { arrange_.moveTracks(n); });
    }
    if (command == "cursor-up")
    {
        return onPage([&] { cursor_.moveBy(-n, 0); }, [&] { arrange_.moveTracks(-n); });
    }
    if (command == "cursor-first")
    {
        return onPage([&] { cursor_.moveTo(cursor_.row(), 0); }, [&] { arrange_.goToBar(1); });
    }
    if (command == "cursor-last") // with a count, on a row further down, as in Vim
    {
        return onPage([&] { cursor_.moveTo(cursor_.row() + n - 1, kGridColumns - 1); },
                      [&] { arrange_.goToLastBar(); });
    }
    if (command == "go-first" || command == "go-last")
    {
        const bool first = command == "go-first";
        return onPage(
            [&]
            {
                const int row = count > 0 ? count - 1 : first ? 0 : kGridRows - 1;
                cursor_.moveTo(row, cursor_.column());
            },
            [&]
            {
                if (count > 0 || first)
                {
                    arrange_.goToBar(std::max(count, 1));
                }
                else
                {
                    arrange_.goToLastBar();
                }
            });
    }
    if (command == "scroll-half-down" || command == "scroll-half-up" ||
        command == "scroll-page-down" || command == "scroll-page-up")
    {
        const bool half = command.starts_with("scroll-half");
        const int by = n * rows(half) * (command.ends_with("down") ? 1 : -1);
        return onPage([&] { cursor_.scrollRowsBy(by); }, [&] { arrange_.scrollTracks(by); });
    }
    if (command == "cursor-left-scroll" || command == "cursor-right-scroll")
    {
        const int by = command == "cursor-left-scroll" ? -n : n;
        return onPage([&] { cursor_.scrollColumnsBy(by); }, [&] { arrange_.scrollBars(by); });
    }
    if (command == "cursor-up-scroll" || command == "cursor-down-scroll")
    {
        const int by = command == "cursor-up-scroll" ? -n : n;
        return onPage([&] { cursor_.scrollRowsBy(by); }, [&] { arrange_.scrollTracks(by); });
    }
    if (command == "scroll-centre")
    {
        return onPage([&] { cursor_.centre(); }, [&] { arrange_.centre(); });
    }
    if (command == "zoom-in" || command == "zoom-out" || command == "zoom-reset")
    {
        if (!arrange)
        {
            return false;
        }
        if (command == "zoom-reset")
        {
            arrange_.resetZoom();
        }
        else
        {
            arrange_.zoomBy(command == "zoom-in" ? -n : n);
        }
        return true;
    }
    if (command == "test-next-page")
    {
        page_ = page_ == Page::Grid      ? Page::Arrange
                : page_ == Page::Arrange ? Page::Samples
                                         : Page::Grid;
        return true;
    }
    if (command == "cancel")
    {
        return false;
    }
    if (command == "command-line")
    {
        mode_ = Mode::CommandLine;
        line_.open();
        return true;
    }
    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    return std::nullopt;
}

std::optional<bool> TestView::runCommandLine(std::string_view command)
{
    if (mode_ != Mode::CommandLine)
    {
        return false;
    }
    if (command == "command-line-run")
    {
        mode_ = Mode::Normal;
        const auto line = line_.take();
        if (lineHandler_ && !line.empty())
        {
            lineHandler_(line);
        }
        return true;
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty())) // as in Vim
    {
        mode_ = Mode::Normal;
        return true;
    }
    if (command == "command-line-backspace")
    {
        return line_.backspace();
    }
    if (command == "command-line-delete")
    {
        return line_.erase();
    }
    if (command == "command-line-left")
    {
        line_.left();
    }
    else if (command == "command-line-right")
    {
        line_.right();
    }
    else if (command == "command-line-start")
    {
        line_.start();
    }
    else if (command == "command-line-end")
    {
        line_.end();
    }
    else if (command == "command-line-clear")
    {
        line_.clear();
    }
    else if (command == "command-line-older")
    {
        return line_.older();
    }
    else if (command == "command-line-newer")
    {
        return line_.newer();
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

Region TestView::promptRegion() const
{
    // ":" and the line, from where the cursor still fits, and the cursor as a cell of its own.
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto& text = line_.text();
    const auto cursor = line_.cursor();
    const auto room =
        static_cast<std::size_t>(std::max((width_ - 2 * margin) / cell_.width - 2, 1));
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    const Style bar{Role::BarText, Role::Bar, kPlain};
    Region region{
        .bounds = layout_.bottom[0], .background = Role::Bar, .text = {}, .under = {}, .over = {}};
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    region.text.push_back({x(0), 0, U":", bar});
    region.text.push_back({x(1), 0, text.substr(first, cursor - first), bar});
    const char32_t under = cursor < text.size() ? text[cursor] : U' ';
    region.text.push_back({x(1 + cursor - first),
                           0,
                           std::u32string(1, under),
                           {Role::CursorText, Role::Cursor, kPlain}});
    if (cursor + 1 < text.size())
    {
        region.text.push_back({x(2 + cursor - first), 0, text.substr(cursor + 1), bar});
    }
    return region;
}

Frame TestView::frame() const
{
    Frame frame;
    if (page_ == Page::Grid)
    {
        frame.push_back(demoRegion());
        frame.push_back(cellsRegion());
    }
    else if (page_ == Page::Arrange)
    {
        arrange_.addRegions(frame);
    }
    else
    {
        frame.push_back(samplesRegion());
    }
    addBars(frame);
    return frame;
}

Region TestView::demoRegion() const
{
    const Rect& main = layout_.main;
    const int height = std::min(kDemoRows * cell_.height, main.height);
    CellGrid grid{layout_.columns, kDemoRows};
    grid.write(1, 0, "Bar lines on one time scale: 4/4 above, 7/8 below", kDim);
    grid.write(1, 1, "4/4", {});
    grid.write(1, 3, "7/8", {});
    Region region =
        ui::gridRegion(grid, {main.x, main.y, main.width, height}, cell_, Role::Background);

    // Four bars of 4/4 across the width after the labels, less half a cell at the right.
    const auto ppqn = core::kPpqn;
    const TimeScale scale{
        .start = 0,
        .length = core::NoteFraction{4, 1}.toTicks(ppqn),
        .x = kRowLabelColumns * cell_.width,
        .width = std::max(main.width - kRowLabelColumns * cell_.width - cell_.width / 2, 0)};
    const int barLineWidth = hairline(cell_);
    const int gap = std::max(cell_.height / 8, 1); // between the lanes
    const core::NoteFraction grid16{1, 16};

    // Each lane is two rows: bar numbers placed at their bar lines, to the pixel, and notes.
    const auto lane = [&](core::TimeSignature signature, int row,
                          std::initializer_list<core::NoteFraction> rhythm)
    {
        const core::MeterMap meters{ppqn, signature};
        const int top = row * cell_.height;
        const int laneHeight = 2 * cell_.height - gap;
        auto lines = meterLines(meters, scale, top, laneHeight, grid16, cell_);
        region.under.insert(region.under.end(), lines.begin(), lines.end());

        for (std::int64_t bar = 1;; ++bar)
        {
            const core::Tick start = meters.barStart(bar);
            if (start >= scale.start + scale.length)
            {
                break;
            }
            region.text.push_back({scale.toX(start) + 2 * barLineWidth, top,
                                   decodeUtf8(number(static_cast<int>(bar))), kDim});
            // The rhythm, repeated through the bar, each note a sixteenth short of the next.
            core::Tick tick = start;
            const core::Tick end = std::min(meters.barStart(bar + 1), scale.start + scale.length);
            for (auto it = rhythm.begin(); tick < end;
                 it = std::next(it) == rhythm.end() ? rhythm.begin() : std::next(it))
            {
                const core::Tick length = it->toTicks(ppqn);
                const int x = scale.toX(tick) + 2 * barLineWidth;
                const int right = scale.toX(tick + length - grid16.toTicks(ppqn));
                region.under.push_back(
                    {{x, top + cell_.height + gap, std::max(right - x, 1), cell_.height - 3 * gap},
                     Role::Note,
                     255});
                tick += length;
            }
        }
    };
    lane({4, 4}, 1, {core::NoteFraction{1, 4}});
    lane({7, 8}, 3, {core::NoteFraction{1, 4}, core::NoteFraction{1, 4}, core::NoteFraction{3, 8}});
    return region;
}

Region TestView::cellsRegion() const
{
    const Rect& main = layout_.main;
    const int top = std::min(kDemoRows * cell_.height, main.height);
    const Rect bounds{main.x, main.y + top, main.width, main.height - top};
    const int rows = (bounds.height + cell_.height - 1) / cell_.height;
    CellGrid grid{layout_.columns, rows};

    // The column numbers across the top and the row numbers down the left stay put.
    grid.fill(0, 0, layout_.columns, 1, {U' ', kHeader});
    grid.fill(0, 0, kRowLabelColumns, rows, {U' ', kHeader});
    for (int column = cursor_.left();; ++column)
    {
        const int x = kRowLabelColumns + (column - cursor_.left()) * kCellColumns;
        if (x >= layout_.columns || column >= kGridColumns)
        {
            break;
        }
        const bool current = column == cursor_.column();
        grid.write(x + 1, 0, number(column + 1), current ? kHeaderCursor : kHeader);

        for (int line = 1; line < rows && cursor_.top() + line - 1 < kGridRows; ++line)
        {
            const int row = cursor_.top() + line - 1;
            const auto label = number(row + 1) + "," + number(column + 1);
            if (current && row == cursor_.row())
            {
                const Style cursor{Role::CursorText, Role::Cursor, kPlain};
                grid.fill(x, line, kCellColumns, 1, {U' ', cursor});
                grid.write(x + 1, line, label, cursor);
            }
            else
            {
                grid.write(x + 1, line, label, kDim);
            }
        }
    }
    for (int line = 1; line < rows && cursor_.top() + line - 1 < kGridRows; ++line)
    {
        const int row = cursor_.top() + line - 1;
        grid.write(0, line, rightAligned(number(row + 1), kRowLabelColumns - 1),
                   row == cursor_.row() ? kHeaderCursor : kHeader);
    }
    return ui::gridRegion(grid, bounds, cell_, Role::Background);
}

Region TestView::samplesRegion() const
{
    CellGrid grid{layout_.columns, layout_.rows};
    grid.write(1, 0, "Theme roles", kTitle);
    grid.write(13, 0, "as highlight names them; cursor, bar and mode have -text roles too", kDim);
    for (int group = 0; group < kRoleGroups; ++group)
    {
        const int x = 1 + group * 19;
        for (int i = 0; i < 5; ++i)
        {
            const auto& role = kRoles[group][i];
            if (role.name == nullptr)
            {
                continue;
            }
            if (role.swatch)
            {
                grid.write(x, i + 1, "████", role.style);
                grid.write(x + 5, i + 1, role.name, {});
            }
            else
            {
                grid.write(x, i + 1, std::string{" "} + role.name + " ", role.style);
            }
        }
    }

    int column = grid.write(1, 7, "bold", {Role::Text, Role::Background, kBold});
    column = grid.write(column + 2, 7, "underline", {Role::Text, Role::Background, kUnderline});
    column = grid.write(column + 2, 7, "reverse", {Role::Text, Role::Background, kReverse});
    grid.write(column + 2, 7, "bold and underlined",
               {Role::Accent, Role::Background, static_cast<Attributes>(kBold | kUnderline)});

    grid.write(1, 9, "Characters", kTitle);
    int row = 10;
    for (const auto* line : kCharacters)
    {
        grid.write(1, row++, line, {});
    }
    return ui::gridRegion(grid, layout_.main, cell_, Role::Background);
}

void TestView::addBars(Frame& frame) const
{
    const Style bar{Role::BarText, Role::Bar, kPlain};
    Bar top;
    top.left = {{"Modal", {Role::Accent, Role::Bar, kBold}}, {"  test view", bar}};
    top.centre = {{"Tab switches pages", bar}};
    top.right = {{"cell " + number(cell_.width) + "×" + number(cell_.height) + " px", bar}};

    Bar bottom;
    const char* pageName = page_ == Page::Grid      ? "  grid"
                           : page_ == Page::Arrange ? "  arrange"
                                                    : "  samples";
    bottom.left = {{" NORMAL ", {Role::ModeText, Role::Mode, kBold}}, {pageName, bar}};
    if (!message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        bottom.left.push_back({"  " + message_, {role, Role::Bar, kPlain}});
    }
    else if (page_ == Page::Grid)
    {
        bottom.centre = {
            {"row " + number(cursor_.row() + 1) + "  col " + number(cursor_.column() + 1), bar}};
    }
    else if (page_ == Page::Arrange)
    {
        bottom.centre = {{arrange_.status(), bar}};
    }
    if (const auto pending = keys_.pending(); !pending.empty())
    {
        bottom.right.push_back({pending + "   ", {Role::Accent, Role::Bar, kBold}});
    }
    bottom.right.push_back({number(width_) + "×" + number(height_) + " px   " +
                                number(layout_.fullColumns) + "×" + number(layout_.fullRows) +
                                " cells",
                            bar});

    const auto spacing = BarSpacing::forCell(cell_);
    auto topRegion = layoutBar(top, width_, cell_, spacing);
    topRegion.bounds = layout_.top[0];
    frame.push_back(std::move(topRegion));
    if (mode_ == Mode::CommandLine)
    {
        frame.push_back(promptRegion());
        return;
    }
    auto bottomRegion = layoutBar(bottom, width_, cell_, spacing);
    bottomRegion.bounds = layout_.bottom[0];
    frame.push_back(std::move(bottomRegion));
}

} // namespace modal::ui
