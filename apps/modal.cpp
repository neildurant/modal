// modal: the sequencer.

#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/style.hpp>

#include <memory>
#include <string>
#include <variant>

namespace
{

using namespace modal;
using ui::Role;
using ui::Style;

// Until the test view (milestone 3, step 4): a sample of the characters and styles the views
// will use.
constexpr const char* kSample[] = {
    "Modal: the quick brown fox, 0123456789",
    "17:2:480 │ E3 (52) │ vel 96 │ dur 0:1:240 (1/8.) │ F♯3 B♭2",
    "",
    "┌───┬───┐ ┏━━━┳━━━┓ ┍━━━┯━━━┑   shades    ░▒▓█",
    "│ 1 │ 2 │ ┃ 1 ┃ 2 ┃ │ 1 │ 2 │   eighths   ▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█",
    "├───┼───┤ ┣━━━╋━━━┫ ┝━━━┿━━━┥   halves    ▀▄▌▐ ▔▕",
    "└───┴───┘ ┗━━━┻━━━┛ ┕━━━┷━━━┙   quadrants ▖▗▘▝ ▙▚▛▜▝▞▟",
    "",
    "density   ░░▒▒▓▓██▓▓▒▒░░  ▁▂▃▅▇█▇▅▃▂▁",
    "arrows    ←→↑↓ ▶◀▲▼ ●○ ·•…×±≤≥",
    "font's own (not drawn): ═║╔╗╚╝ ╭╮╰╯ ┄┆ ╱╲",
    "not in the font, so shown as a replacement: ♮♩",
    "ASCII     {}[]()<>|\\/\"'`~!@#$%^&*_+-=",
};

ui::Frame sampleFrame(int width, int height, ui::CellSize cell)
{
    const auto layout = ui::layoutWindow(width, height, cell, 1, 1);

    ui::CellGrid grid{layout.columns, layout.rows};
    int row = 1;
    for (const auto* line : kSample)
    {
        grid.write(1, row++, line, {});
    }
    ++row;
    int column = grid.write(1, row, "bold", {Role::Text, Role::Background, ui::kBold});
    column =
        grid.write(column + 2, row, "underline", {Role::Text, Role::Background, ui::kUnderline});
    column = grid.write(column + 2, row, "reverse", {Role::Text, Role::Background, ui::kReverse});
    column = grid.write(column + 2, row, "dim", {Role::Dim, Role::Background, ui::kPlain});
    column = grid.write(column + 2, row, "accent", {Role::Accent, Role::Background, ui::kPlain});
    column = grid.write(column + 2, row, "warning", {Role::Warning, Role::Background, ui::kPlain});
    column = grid.write(column + 2, row, "selected", {Role::Text, Role::Selection, ui::kPlain});
    grid.write(column + 2, row, "C", {Role::CursorText, Role::Cursor, ui::kPlain});
    row += 2;
    grid.write(1, row, "The last row and column may be partly visible; the bars follow the edges.",
               {Role::Dim, Role::Background, ui::kPlain});

    const auto spacing = ui::BarSpacing::forCell(cell);
    ui::Bar top;
    top.left = {{"Modal", {Role::Accent, Role::Bar, ui::kBold}},
                {"  grid renderer sample", {Role::BarText, Role::Bar, ui::kPlain}}};
    top.right = {{"cell " + std::to_string(cell.width) + "×" + std::to_string(cell.height) + " px",
                  {Role::BarText, Role::Bar, ui::kPlain}}};
    ui::Bar bottom;
    bottom.left = {{" NORMAL ", {Role::ModeText, Role::Mode, ui::kBold}},
                   {"  untitled", {Role::BarText, Role::Bar, ui::kPlain}}};
    bottom.right = {{std::to_string(width) + "×" + std::to_string(height) + " px   " +
                         std::to_string(layout.fullColumns) + "×" +
                         std::to_string(layout.fullRows) + " cells",
                     {Role::BarText, Role::Bar, ui::kPlain}}};

    ui::Frame frame;
    frame.push_back(ui::gridRegion(grid, layout.main, cell, Role::Background));
    auto topRegion = ui::layoutBar(top, width, cell, spacing);
    topRegion.bounds = layout.top[0];
    frame.push_back(std::move(topRegion));
    auto bottomRegion = ui::layoutBar(bottom, width, cell, spacing);
    bottomRegion.bounds = layout.bottom[0];
    frame.push_back(std::move(bottomRegion));
    return frame;
}

} // namespace

int main()
{
    auto opened = ui::sdl::Window::open({.title = "Modal",
                                         .columns = 120,
                                         .rows = 36,
                                         .width = 0,
                                         .height = 0,
                                         .offscreen = false,
                                         .font = {},
                                         .minimumColumns = 80,
                                         .minimumRows = 24});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        ui::sdl::showError("Modal", "Modal " + *error + ".");
        return 1;
    }
    auto& window = *std::get<std::unique_ptr<ui::sdl::Window>>(opened);
    const auto theme = ui::Theme::dark();
    window.run([&](ui::sdl::Window& w)
               { w.draw(sampleFrame(w.pixelWidth(), w.pixelHeight(), w.cellSize()), theme); });
    return 0;
}
