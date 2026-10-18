#include <modal/ui/sdl/window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <variant>

using namespace modal::ui::sdl;

TEST_CASE("an offscreen window draws frames that can be read back")
{
    auto opened =
        Window::open({.title = "test", .width = 64, .height = 48, .offscreen = true, .font = {}});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        FAIL(*error);
    }
    auto& window = *std::get<std::unique_ptr<Window>>(opened);
    CHECK(window.pixelWidth() >= 64);
    CHECK(window.pixelHeight() >= 48);
    CHECK(window.pixel(0, 0) == std::nullopt); // nothing drawn yet

    const Colour background{0x12, 0x34, 0x56};
    window.drawFrame(background);
    CHECK(window.pixel(0, 0) == background);
    CHECK(window.pixel(window.pixelWidth() - 1, window.pixelHeight() - 1) == background);
    CHECK(window.pixel(window.pixelWidth(), 0) == std::nullopt);
}

namespace
{

std::unique_ptr<Window> offscreen(FontOptions font = {})
{
    auto opened = Window::open(
        {.title = "test", .width = 320, .height = 200, .offscreen = true, .font = font});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        FAIL(*error);
    }
    return std::move(std::get<std::unique_ptr<Window>>(opened));
}

} // namespace

TEST_CASE("the built-in font gives cells taller than they are wide, growing with its size")
{
    auto window = offscreen();
    const auto cell = window->cellSize();
    CHECK(cell.width > 0);
    CHECK(cell.height > cell.width);

    REQUIRE(window->setFont({.file = "", .points = 24.0f}).empty());
    CHECK(window->cellSize().width > cell.width);
    CHECK(window->cellSize().height > cell.height);
}

TEST_CASE("a font that can't be opened is reported, and the old one kept")
{
    auto window = offscreen();
    const auto cell = window->cellSize();
    const auto error = window->setFont({.file = "no such font.ttf", .points = 12.0f});
    CHECK(error.find("no such font.ttf") != std::string::npos);
    CHECK(window->cellSize() == cell);
}

TEST_CASE("text is drawn one character to a cell")
{
    auto window = offscreen();
    const auto cell = window->cellSize();
    const Colour background{0, 0, 0};
    const Colour red{255, 0, 0};
    window->drawFrame(background, [&] { window->drawText(1, 2, "█ █", red); });

    // The middle of each cell: full blocks in columns 1 and 3, a space between.
    const auto middle = [&](int column, int row)
    {
        return window->pixel(column * cell.width + cell.width / 2,
                             row * cell.height + cell.height / 2);
    };
    CHECK(middle(0, 2) == background);
    CHECK(middle(1, 2) == red);
    CHECK(middle(2, 2) == background);
    CHECK(middle(3, 2) == red);
    CHECK(middle(1, 1) == background);
}

// Hidden: writes a sample of the font to look at, to MODAL_SAMPLE_BMP or font-sample.bmp.
TEST_CASE("a sample of the built-in font", "[.sample]")
{
    auto opened = Window::open(
        {.title = "sample", .width = 640, .height = 260, .offscreen = true, .font = {}});
    REQUIRE(std::holds_alternative<std::unique_ptr<Window>>(opened));
    auto& window = *std::get<std::unique_ptr<Window>>(opened);
    const Colour text{0xc0, 0xca, 0xf5};
    window.drawFrame({0x1a, 0x1b, 0x26},
                     [&]
                     {
                         window.drawText(1, 1, "Modal: the quick brown fox, 0123456789", text);
                         window.drawText(1, 2, "17:2:480 │ E3 (52) │ vel 96 │ F♯3 B♭2", text);
                         window.drawText(1, 4, "┌───┬───┐ ░▒▓█ ▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█", text);
                         window.drawText(1, 5, "│ 1 │ 2 │ ▀▄▌▐ ←→↑↓ ▶◀▲▼ ●○ ·•…×±", text);
                         window.drawText(1, 6, "├───┼───┤ ━┃═║╔╗╚╝ missing: ♮♩", text);
                         window.drawText(1, 7, "└───┴───┘ {}[]()<>|\\/\"'`~!@#$%^&*_+-=", text);
                     });
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    CHECK(window.saveFrame(path != nullptr ? path : "font-sample.bmp"));
}
