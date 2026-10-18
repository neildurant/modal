#include <modal/ui/config.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/test_view.hpp>
#include <modal/ui/window_commands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// A view in a window of 80 by 24 cells: 22 rows between the bars, 6 of them the demo, 1 the
// column numbers, so 15 rows of labelled cells; 75 columns right of the row numbers, so 7
// labelled cells fully shown.
TestView view80x24()
{
    TestView view;
    view.resize(80 * kCell.width, 24 * kCell.height, kCell);
    return view;
}

void type(TestView& view, std::u32string_view keys)
{
    for (const char32_t key : keys)
    {
        view.handle(Key::typed(key), 0);
    }
}

// A row of cells of a region, as text, from the text runs on that row; runs placed between
// cells, as in bars, are taken from the cell they start in.
std::u32string rowText(const Region& region, int row)
{
    std::u32string line(static_cast<std::size_t>(region.bounds.width / kCell.width + 1), U' ');
    for (const auto& run : region.text)
    {
        if (run.y == row * kCell.height)
        {
            line.replace(static_cast<std::size_t>(run.x / kCell.width), run.text.size(), run.text);
        }
    }
    return line;
}

// The style of the run covering a cell.
Style styleAt(const Region& region, int column, int row)
{
    for (const auto& run : region.text)
    {
        const int first = run.x / kCell.width;
        if (run.y == row * kCell.height && column >= first &&
            column < first + static_cast<int>(run.text.size()))
        {
            return run.style;
        }
    }
    return {};
}

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the test view's motions take counts, as in Vim")
{
    auto view = view80x24();
    type(view, U"12j");
    CHECK(view.cursor().row() == 12);
    type(view, U"10l");
    CHECK(view.cursor().column() == 10);
    type(view, U"0");
    CHECK(view.cursor().column() == 0); // 0 with no count is a motion
    type(view, U"3k2h");
    CHECK(view.cursor().row() == 9);
    CHECK(view.cursor().column() == 0);

    view.handle(Key::of(NamedKey::Down), 0);
    view.handle(Key::of(NamedKey::Right), 0);
    CHECK(view.cursor().row() == 10);
    CHECK(view.cursor().column() == 1);

    type(view, U"$");
    CHECK(view.cursor().column() == TestView::kGridColumns - 1);
    type(view, U"3$"); // and two rows further down
    CHECK(view.cursor().row() == 12);

    type(view, U"G");
    CHECK(view.cursor().row() == TestView::kGridRows - 1);
    type(view, U"40G");
    CHECK(view.cursor().row() == 39);
    view.handle(Key::of(NamedKey::Home), 0);
    CHECK(view.cursor().column() == 0);
}

TEST_CASE("a count shows until a key uses it or Escape gives it up")
{
    auto view = view80x24();
    CHECK(view.handle(Key::typed(U'4'), 0));
    CHECK(view.handle(Key::typed(U'0'), 0));
    CHECK(view.pending() == "40");
    CHECK(view.handle(Key::of(NamedKey::Escape), 0)); // the count is no longer shown
    CHECK(view.pending().empty());
    CHECK_FALSE(view.handle(Key::of(NamedKey::Escape), 0));

    type(view, U"5x"); // a key with no binding gives the count up too
    CHECK(view.pending().empty());
    type(view, U"j");
    CHECK(view.cursor().row() == 1);

    type(view, U"9999999");
    CHECK(view.pending() == "99999");
}

TEST_CASE("the test view pages by half and whole screens, keeping two rows")
{
    auto view = view80x24();
    REQUIRE(view.cursor().viewRows() == 15);
    REQUIRE(view.cursor().viewColumns() == 7);

    view.handle(Key::typed(U'd', kCtrl), 0);
    CHECK(view.cursor().top() == 7);
    CHECK(view.cursor().row() == 7);
    view.handle(Key::typed(U'u', kCtrl), 0);
    CHECK(view.cursor().top() == 0);

    view.handle(Key::typed(U'f', kCtrl), 0);
    CHECK(view.cursor().top() == 13);
    type(view, U"2");
    view.handle(Key::of(NamedKey::PageDown), 0);
    CHECK(view.cursor().top() == 39);
    view.handle(Key::typed(U'b', kCtrl), 0);
    CHECK(view.cursor().top() == 26);
}

TEST_CASE("Tab goes through the grid, the arrangement and the samples")
{
    auto view = view80x24();
    CHECK(view.handle(Key::of(NamedKey::Tab), 0));
    CHECK(view.page() == TestView::Page::Arrange);
    CHECK(view.handle(Key::of(NamedKey::Tab), 0));
    CHECK(view.page() == TestView::Page::Samples);
    CHECK_FALSE(view.handle(Key::typed(U'j'), 0));
    CHECK(view.cursor().row() == 0);

    const auto frame = view.frame();
    REQUIRE(frame.size() == 3); // the samples and two bars
    CHECK(contains(rowText(frame[0], 0), U"Theme roles"));
    CHECK(contains(rowText(frame[2], 0), U"samples"));

    view.handle(Key::of(NamedKey::Tab), 0);
    CHECK(view.page() == TestView::Page::Grid);
}

TEST_CASE("the grid shows labelled cells under fixed column and row numbers")
{
    auto view = view80x24();
    type(view, U"jl");
    auto frame = view.frame();
    REQUIRE(frame.size() == 4); // the demo, the grid and two bars
    const auto& grid = frame[1];
    CHECK(grid.bounds == Rect{0, 7 * kCell.height, 80 * kCell.width, 16 * kCell.height});

    CHECK(contains(rowText(grid, 0), U"      1         2         3"));
    CHECK(contains(rowText(grid, 1), U"   1  1,1       1,2       1,3"));
    CHECK(contains(rowText(grid, 2), U"   2  2,1       2,2       2,3"));

    // The cursor's cell, the whole of it, and its column and row numbers.
    CHECK(styleAt(grid, 15, 2) == Style{Role::CursorText, Role::Cursor, kPlain});
    CHECK(styleAt(grid, 24, 2) == Style{Role::CursorText, Role::Cursor, kPlain});
    CHECK(styleAt(grid, 25, 2).background == Role::Background);
    CHECK(styleAt(grid, 16, 0).foreground == Role::Accent);
    CHECK(styleAt(grid, 3, 2).foreground == Role::Accent);
    CHECK(styleAt(grid, 3, 1).foreground == Role::BarText);

    // Moved past the view's edges, the cells scroll but the numbers stay at the edges.
    type(view, U"20j9l");
    frame = view.frame();
    CHECK(view.cursor().top() == 7);
    CHECK(view.cursor().left() == 4);
    CHECK(contains(rowText(frame[1], 0), U"      5         6"));
    CHECK(contains(rowText(frame[1], 1), U"   8  8,5       8,6"));

    const auto& status = frame[3];
    CHECK(contains(rowText(status, 0), U"row 22  col 11"));
}

TEST_CASE("the status line shows a count being typed")
{
    auto view = view80x24();
    type(view, U"25");
    const auto frame = view.frame();
    CHECK(contains(rowText(frame[3], 0), U"25   800×480 px   80×22 cells"));
}

TEST_CASE("when the window shrinks, the grid scrolls to keep the cursor fully in view")
{
    auto view = view80x24();
    type(view, U"14j6l");
    CHECK(view.cursor().top() == 0);
    // A pixel short of a row, and of a labelled cell's columns.
    view.resize(75 * kCell.width - 1, 24 * kCell.height - 1, kCell);
    CHECK(view.cursor().viewRows() == 14);
    CHECK(view.cursor().viewColumns() == 6);
    CHECK(view.cursor().top() == 1);
    CHECK(view.cursor().left() == 1);
}

TEST_CASE("the demo draws bars of 4/4 above bars of 7/8, numbered at their bar lines")
{
    auto view = view80x24();
    const auto frame = view.frame();
    const auto& demo = frame[0];
    CHECK(demo.bounds == Rect{0, kCell.height, 80 * kCell.width, 6 * kCell.height});

    std::vector<int> barLines[2];
    int notes = 0;
    for (const auto& shape : demo.under)
    {
        if (shape.role == Role::BarLine)
        {
            barLines[shape.rect.y == kCell.height ? 0 : 1].push_back(shape.rect.x);
        }
        notes += shape.role == Role::Note ? 1 : 0;
    }
    // Across 745 pixels from 50: four bars of 4/4, and of 7/8 four whole bars and part of one.
    REQUIRE(barLines[0].size() == 4);
    REQUIRE(barLines[1].size() == 5);
    CHECK(barLines[0][0] == 50);
    CHECK(barLines[1][0] == 50);
    const int common = barLines[0][1] - barLines[0][0];
    const int seven = barLines[1][1] - barLines[1][0];
    CHECK(common == 186);
    CHECK(seven == 162); // 7/8 of 186.25 is 162.97, rounded down
    CHECK(notes == 16 + 14);

    // The bar numbers are placed to the pixel, not to cells.
    int numbers = 0;
    for (const auto& run : demo.text)
    {
        numbers += run.style.foreground == Role::Dim && run.x % kCell.width != 0 ? 1 : 0;
    }
    CHECK(numbers >= 4);
}

TEST_CASE("gg goes to the first row, or the one counted, as G does to the last")
{
    auto view = view80x24();
    type(view, U"G");
    CHECK(view.cursor().row() == TestView::kGridRows - 1);
    type(view, U"g");
    CHECK(view.pending() == "g");
    type(view, U"g");
    CHECK(view.cursor().row() == 0);
    type(view, U"25gg");
    CHECK(view.cursor().row() == 24);
}

TEST_CASE("z centres the view once the time for zz has passed, and zz resets the zoom")
{
    auto view = view80x24();
    type(view, U"40j");
    REQUIRE(view.cursor().top() == 26); // the cursor on the view's last row
    view.handle(Key::typed(U'z'), 1000);
    CHECK(view.pending() == "z");
    REQUIRE(view.deadline() == 1000 + kDefaultTimeout);
    CHECK_FALSE(view.wake(1000 + kDefaultTimeout - 1));
    CHECK(view.cursor().top() == 26);
    CHECK(view.wake(1000 + kDefaultTimeout));
    CHECK(view.cursor().top() == 33); // row 40 in the middle of 15
    CHECK(view.pending().empty());
    CHECK_FALSE(view.deadline());

    // A key straight after z doesn't wait: z runs, then the key.
    type(view, U"G");
    view.handle(Key::typed(U'z'), 3000);
    view.handle(Key::typed(U'k'), 3001);
    CHECK(view.cursor().row() == TestView::kGridRows - 2);
    CHECK(view.cursor().top() == TestView::kGridRows - 15);

    // On the arrangement, zz zooms back to a quarter note to a column.
    view.handle(Key::of(NamedKey::Tab), 4000);
    type(view, U"--");
    CHECK(view.arrange().zoom() == ArrangePage::kDefaultZoom + 2);
    type(view, U"zz");
    CHECK(view.arrange().zoom() == ArrangePage::kDefaultZoom);
}

TEST_CASE("the built-in modalrc binds keys only to the test view's and the window's commands")
{
    Config config;
    std::vector<CommandInfo> commands{TestView::commands().begin(), TestView::commands().end()};
    commands.insert(commands.end(), windowCommands().begin(), windowCommands().end());
    ConfigReader reader{config, commands};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    CHECK(reader.errors().empty());

    // And every command is one the test view runs.
    TestView view;
    for (const auto& command : TestView::commands())
    {
        CAPTURE(command.name);
        CHECK(view.run(command.name, 0).has_value());
    }
    CHECK_FALSE(view.run("no-such-command", 0).has_value());
}

TEST_CASE("a message shows in the status line until the next key")
{
    auto view = view80x24();
    view.setMessage("modalrc:3: unknown setting \"fontsiz\"", TestView::MessageKind::Warning);
    CHECK(contains(rowText(view.frame()[3], 0), U"modalrc:3: unknown setting"));
    CHECK(view.handle(Key::typed(U'x'), 0)); // unbound, but the message goes
    CHECK(view.message().empty());
    CHECK_FALSE(contains(rowText(view.frame()[3], 0), U"modalrc:3"));

    // A warning is in the warning colour, information in the message colour.
    const auto messageStyle = [&]
    {
        const auto frame = view.frame();
        for (const auto& run : frame[3].text)
        {
            if (run.text.find(U"hello") != std::u32string::npos)
            {
                return run.style.foreground;
            }
        }
        return Role::Background;
    };
    view.setMessage("hello", TestView::MessageKind::Warning);
    CHECK(messageStyle() == Role::Warning);
    view.setMessage("hello", TestView::MessageKind::Information);
    CHECK(messageStyle() == Role::Message);
}

TEST_CASE("commands the view doesn't have go to its handler, such as the window's")
{
    auto view = view80x24();
    std::vector<std::pair<std::string, int>> handled;
    view.setCommandHandler(
        [&](std::string_view command, int count) -> std::optional<bool>
        {
            if (command != "font-bigger")
            {
                return std::nullopt;
            }
            handled.emplace_back(command, count);
            return true;
        });
    CHECK(view.handle(Key::typed(U'=', kCtrl), 0));
    view.handle(Key::typed(U'3'), 0);
    view.handle(Key::of(NamedKey::KeypadPlus, kCtrl), 0);
    CHECK(handled ==
          std::vector<std::pair<std::string, int>>{{"font-bigger", 0}, {"font-bigger", 3}});
    CHECK_FALSE(view.run("no-such-command", 0).has_value());
}
