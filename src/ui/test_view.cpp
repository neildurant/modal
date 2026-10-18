#include <modal/core/meter_map.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/test_view.hpp>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>

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

// Every theme role, in four columns: text, backgrounds with their text, timeline lines and
// markers, and density.
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
     {nullptr, {}, false}},
    {{"grid line", {Role::GridLine, Role::Background, kPlain}, true},
     {"beat line", {Role::BeatLine, Role::Background, kPlain}, true},
     {"bar line", {Role::BarLine, Role::Background, kPlain}, true},
     {"note", {Role::Note, Role::Background, kPlain}, true},
     {"marker", {Role::Marker, Role::Background, kPlain}, true}},
    {{"density 1", {Role::Density1, Role::Background, kPlain}, true},
     {"density 2", {Role::Density2, Role::Background, kPlain}, true},
     {"density 3", {Role::Density3, Role::Background, kPlain}, true},
     {"density 4", {Role::Density4, Role::Background, kPlain}, true},
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

TestView::TestView()
    : cursor_(kGridRows, kGridColumns)
    , arrange_(demoSong())
{
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

bool TestView::handle(const Key& key)
{
    const bool plain = key.named == NamedKey::None && key.modifiers == kNoModifiers;
    const char32_t c = key.character;
    if (plain && ((c >= U'1' && c <= U'9') || (c == U'0' && count_ > 0)))
    {
        count_ = std::min(count_ * 10 + static_cast<int>(c - U'0'), kMaxCount);
        return true;
    }
    const int count = std::exchange(count_, 0);
    if (key == Key::of(NamedKey::Tab))
    {
        page_ = page_ == Page::Grid      ? Page::Arrange
                : page_ == Page::Arrange ? Page::Samples
                                         : Page::Grid;
        return true;
    }
    const bool moved = page_ == Page::Grid      ? move(key, count)
                       : page_ == Page::Arrange ? arrange_.handle(key, count)
                                                : false;
    return moved || count > 0; // a count given up is shown no more
}

bool TestView::move(const Key& key, int count)
{
    const int n = std::max(count, 1);
    const int halfPage = std::max(cursor_.viewRows() / 2, 1);
    const int page = std::max(cursor_.viewRows() - 2, 1); // as in Vim, keeping two rows
    const auto ctrl = [&](char32_t c) { return key == Key::typed(c, kCtrl); };
    const auto is = [&](char32_t c, NamedKey named)
    { return key == Key::typed(c) || (named != NamedKey::None && key == Key::of(named)); };

    if (is(U'h', NamedKey::Left))
    {
        cursor_.moveBy(0, -n);
    }
    else if (is(U'l', NamedKey::Right))
    {
        cursor_.moveBy(0, n);
    }
    else if (is(U'j', NamedKey::Down))
    {
        cursor_.moveBy(n, 0);
    }
    else if (is(U'k', NamedKey::Up))
    {
        cursor_.moveBy(-n, 0);
    }
    else if (is(U'0', NamedKey::Home))
    {
        cursor_.moveTo(cursor_.row(), 0);
    }
    else if (is(U'$', NamedKey::End)) // with a count, on rows further down, as in Vim
    {
        cursor_.moveTo(cursor_.row() + n - 1, kGridColumns - 1);
    }
    else if (is(U'G', NamedKey::None))
    {
        cursor_.moveTo(count > 0 ? count - 1 : kGridRows - 1, cursor_.column());
    }
    else if (ctrl(U'd'))
    {
        cursor_.scrollBy(n * halfPage);
    }
    else if (ctrl(U'u'))
    {
        cursor_.scrollBy(-n * halfPage);
    }
    else if (ctrl(U'f') || key == Key::of(NamedKey::PageDown))
    {
        cursor_.scrollBy(n * page);
    }
    else if (ctrl(U'b') || key == Key::of(NamedKey::PageUp))
    {
        cursor_.scrollBy(-n * page);
    }
    else
    {
        return false;
    }
    return true;
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
    const int barLineWidth = std::max(cell_.width / 7, 1);
    const int gap = std::max(cell_.height / 8, 1); // between the lanes
    const core::NoteFraction grid16{1, 16};

    // Each lane is two rows: bar numbers placed at their bar lines, to the pixel, and notes.
    const auto lane = [&](core::TimeSignature signature, int row,
                          std::initializer_list<core::NoteFraction> rhythm)
    {
        const core::MeterMap meters{ppqn, signature};
        const int top = row * cell_.height;
        const int laneHeight = 2 * cell_.height - gap;
        auto lines = meterLines(meters, scale, top, laneHeight, grid16, barLineWidth);
        region.under.insert(region.under.end(), lines.begin(), lines.end());

        for (std::int64_t bar = 1;; ++bar)
        {
            const core::Tick start = meters.barStart(bar);
            if (start >= scale.start + scale.length)
            {
                break;
            }
            region.text.push_back({scale.toX(start) + barLineWidth + 2, top,
                                   decodeUtf8(number(static_cast<int>(bar))), kDim});
            // The rhythm, repeated through the bar, each note a sixteenth short of the next.
            core::Tick tick = start;
            const core::Tick end = std::min(meters.barStart(bar + 1), scale.start + scale.length);
            for (auto it = rhythm.begin(); tick < end;
                 it = std::next(it) == rhythm.end() ? rhythm.begin() : std::next(it))
            {
                const core::Tick length = it->toTicks(ppqn);
                const int x = scale.toX(tick) + barLineWidth + 1;
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
    if (page_ == Page::Grid)
    {
        bottom.centre = {
            {"row " + number(cursor_.row() + 1) + "  col " + number(cursor_.column() + 1), bar}};
    }
    else if (page_ == Page::Arrange)
    {
        bottom.centre = {{arrange_.status(), bar}};
    }
    if (count_ > 0)
    {
        bottom.right.push_back({number(count_) + "   ", {Role::Accent, Role::Bar, kBold}});
    }
    bottom.right.push_back({number(width_) + "×" + number(height_) + " px   " +
                                number(layout_.fullColumns) + "×" + number(layout_.fullRows) +
                                " cells",
                            bar});

    const auto spacing = BarSpacing::forCell(cell_);
    auto topRegion = layoutBar(top, width_, cell_, spacing);
    topRegion.bounds = layout_.top[0];
    frame.push_back(std::move(topRegion));
    auto bottomRegion = layoutBar(bottom, width_, cell_, spacing);
    bottomRegion.bounds = layout_.bottom[0];
    frame.push_back(std::move(bottomRegion));
}

} // namespace modal::ui
