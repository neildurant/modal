#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/test_view.hpp>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace modal::ui;
using namespace modal::ui::sdl;

namespace
{

std::unique_ptr<Window> offscreen(int width, int height, FontOptions font = {},
                                  int minimumColumns = 0, int minimumRows = 0)
{
    auto opened = Window::open({.title = "test",
                                .columns = 0,
                                .rows = 0,
                                .width = width,
                                .height = height,
                                .offscreen = true,
                                .font = std::move(font),
                                .minimumColumns = minimumColumns,
                                .minimumRows = minimumRows});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        FAIL(*error);
    }
    return std::move(std::get<std::unique_ptr<Window>>(opened));
}

// A region covering the whole window holding one run of text.
Region textRegion(const Window& window, TextRun run)
{
    Region region{.bounds = {0, 0, window.pixelWidth(), window.pixelHeight()},
                  .background = Role::Background,
                  .text = {},
                  .under = {},
                  .over = {}};
    region.text.push_back(std::move(run));
    return region;
}

// How many pixels in a cell aren't the background.
int inked(const Window& window, int x, int y, CellSize cell, Colour background)
{
    int count = 0;
    for (int dy = 0; dy < cell.height; ++dy)
    {
        for (int dx = 0; dx < cell.width; ++dx)
        {
            count += window.pixel(x + dx, y + dy) != background ? 1 : 0;
        }
    }
    return count;
}

} // namespace

TEST_CASE("an offscreen window draws frames that can be read back")
{
    auto window = offscreen(64, 48);
    CHECK(window->pixelWidth() >= 64);
    CHECK(window->pixelHeight() >= 48);
    CHECK(window->pixel(0, 0) == std::nullopt); // nothing drawn yet

    const auto theme = Theme::dark();
    window->draw({}, theme);
    CHECK(window->pixel(0, 0) == theme[Role::Background]);
    CHECK(window->pixel(window->pixelWidth() - 1, window->pixelHeight() - 1) ==
          theme[Role::Background]);
    CHECK(window->pixel(window->pixelWidth(), 0) == std::nullopt);
}

TEST_CASE("the built-in font gives cells taller than they are wide, growing with its size")
{
    auto window = offscreen(320, 200);
    const auto cell = window->cellSize();
    CHECK(cell.width > 0);
    CHECK(cell.height > cell.width);

    REQUIRE(window->setFont({.file = "", .points = 24.0f}).empty());
    CHECK(window->cellSize().width > cell.width);
    CHECK(window->cellSize().height > cell.height);
}

TEST_CASE("a font that can't be opened is reported, and the old one kept")
{
    auto window = offscreen(320, 200);
    const auto cell = window->cellSize();
    const auto error = window->setFont({.file = "no such font.ttf", .points = 12.0f});
    CHECK(error.find("no such font.ttf") != std::string::npos);
    CHECK(window->cellSize() == cell);
}

TEST_CASE("text runs are drawn in their styles' colours, one character to a cell")
{
    auto window = offscreen(320, 200);
    const auto theme = Theme::dark();
    const auto cell = window->cellSize();
    Region region = textRegion(*window, {cell.width, cell.height * 2, U"█ █",
                                         Style{Role::Note, Role::Background, kPlain}});
    region.text.push_back({0, cell.height * 4, U"  ", Style{Role::Text, Role::Selection, kPlain}});
    region.text.push_back(
        {0, cell.height * 5, U"  ", Style{Role::Text, Role::Selection, kReverse}});
    window->draw({region}, theme);

    const auto middle = [&](int column, int row)
    {
        return window->pixel(column * cell.width + cell.width / 2,
                             row * cell.height + cell.height / 2);
    };
    CHECK(middle(0, 2) == theme[Role::Background]);
    CHECK(middle(1, 2) == theme[Role::Note]); // full blocks in columns 1 and 3
    CHECK(middle(2, 2) == theme[Role::Background]);
    CHECK(middle(3, 2) == theme[Role::Note]);
    CHECK(middle(1, 4) == theme[Role::Selection]); // a run's background
    CHECK(middle(2, 4) == theme[Role::Background]);
    CHECK(middle(1, 5) == theme[Role::Text]); // reversed
}

TEST_CASE("bold text is heavier, and underlining draws a line across the cells")
{
    auto window = offscreen(320, 200);
    const auto theme = Theme::dark();
    const auto cell = window->cellSize();
    Region region = textRegion(*window, {0, 0, U"M", Style{}});
    region.text.push_back({cell.width * 2, 0, U"M", Style{Role::Text, Role::Background, kBold}});
    region.text.push_back(
        {0, cell.height * 2, U"   ", Style{Role::Accent, Role::Background, kUnderline}});
    window->draw({region}, theme);

    const auto background = theme[Role::Background];
    CHECK(inked(*window, cell.width * 2, 0, cell, background) >
          inked(*window, 0, 0, cell, background));

    // Somewhere down the underlined cells' middle column there's the line, across all three.
    int lineRow = -1;
    for (int y = cell.height * 2; y < cell.height * 3 && lineRow < 0; ++y)
    {
        if (window->pixel(cell.width / 2, y) == theme[Role::Accent])
        {
            lineRow = y;
        }
    }
    REQUIRE(lineRow >= 0);
    CHECK(window->pixel(cell.width * 3 - 1, lineRow) == theme[Role::Accent]);
    CHECK(window->pixel(cell.width * 3, lineRow) == background);
}

TEST_CASE("a region is clipped to its bounds, with shapes under and over its text")
{
    auto window = offscreen(320, 200);
    const auto theme = Theme::dark();
    Region region{.bounds = {10, 10, 40, 30},
                  .background = Role::Bar,
                  .text = {},
                  .under = {{{-100, -100, 1000, 1000}, Role::Note, 255}},
                  .over = {{{20, 0, 5, 30}, Role::Marker, 255}}};
    window->draw({region}, theme);
    CHECK(window->pixel(9, 9) == theme[Role::Background]);
    CHECK(window->pixel(10, 10) == theme[Role::Note]);
    CHECK(window->pixel(49, 39) == theme[Role::Note]);
    CHECK(window->pixel(50, 40) == theme[Role::Background]);
    CHECK(window->pixel(32, 20) == theme[Role::Marker]); // over, offset by the bounds
}

TEST_CASE("a bottom bar touches the bottom edge, whatever the window's height")
{
    auto window = offscreen(320, 203);
    const auto theme = Theme::dark();
    const auto cell = window->cellSize();
    const int width = window->pixelWidth();
    const int height = window->pixelHeight();
    const auto layout = layoutWindow(width, height, cell, 1, 1);

    Region bottom = layoutBar({{}, {}, {}, Role::Bar}, width, cell, BarSpacing::forCell(cell));
    bottom.bounds = layout.bottom[0];
    window->draw({bottom}, theme);
    CHECK(window->pixel(0, height - 1) == theme[Role::Bar]);
    CHECK(window->pixel(0, height - cell.height) == theme[Role::Bar]);
    CHECK(window->pixel(0, height - cell.height - 1) == theme[Role::Background]);
}

TEST_CASE("a window can't be smaller than its minimum size in cells")
{
    auto window = offscreen(100, 100, {}, 20, 8);
    const auto cell = window->cellSize();
    CHECK(window->pixelWidth() >= 20 * cell.width);
    CHECK(window->pixelHeight() >= 8 * cell.height);

    // A larger font raises the minimum, and the window grows to it.
    REQUIRE(window->setFont({.file = "", .points = 24.0f}).empty());
    CHECK(window->pixelWidth() >= 20 * window->cellSize().width);
    CHECK(window->pixelHeight() >= 8 * window->cellSize().height);
}

// Hidden: writes a sample of the font to look at, to MODAL_SAMPLE_BMP or font-sample.bmp.
TEST_CASE("a sample of the built-in font", "[.sample]")
{
    auto window = offscreen(640, 260);
    const auto cell = window->cellSize();
    CellGrid grid{64, 10};
    const Style plain{};
    grid.write(1, 1, "Modal: the quick brown fox, 0123456789", plain);
    grid.write(1, 2, "17:2:480 │ E3 (52) │ vel 96 │ F♯3 B♭2", plain);
    grid.write(1, 4, "┌───┬───┐ ░▒▓█ ▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█", plain);
    grid.write(1, 5, "│ 1 │ 2 │ ▀▄▌▐ ←→↑↓ ▶◀▲▼ ●○ ·•…×±", plain);
    grid.write(1, 6, "├───┼───┤ ━┃═║╔╗╚╝ missing: ♮♩", plain);
    grid.write(1, 7, "└───┴───┘ {}[]()<>|\\/\"'`~!@#$%^&*_+-=", plain);
    grid.write(1, 8, "bold", Style{Role::Text, Role::Background, kBold});
    grid.write(6, 8, "underline", Style{Role::Text, Role::Background, kUnderline});
    grid.write(16, 8, "reverse", Style{Role::Text, Role::Background, kReverse});
    window->draw({gridRegion(grid, {0, 0, 640, 260}, cell, Role::Background)}, Theme::dark());
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    CHECK(window->saveFrame(path != nullptr ? path : "font-sample.bmp"));
}

// Hidden: writes the pages of the test view to look at, to MODAL_SAMPLE_BMP with "-grid",
// "-arrange", "-arrange-out" (zoomed out) and "-samples" before its extension, or to
// test-view-grid.bmp and so on.
TEST_CASE("a sample of the test view", "[.sample]")
{
    auto window = offscreen(1000, 600);
    TestView view;
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    for (const char32_t key : std::u32string_view{U"12j3l"})
    {
        view.handle(Key::typed(key));
    }
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "test-view.bmp";
    base = base.substr(0, base.rfind('.'));
    window->draw(view.frame(), Theme::dark());
    CHECK(window->saveFrame(base + "-grid.bmp"));
    view.handle(Key::of(NamedKey::Tab));
    for (const char32_t key : std::u32string_view{U"6j9l"})
    {
        view.handle(Key::typed(key));
    }
    window->draw(view.frame(), Theme::dark());
    CHECK(window->saveFrame(base + "-arrange.bmp"));
    view.handle(Key::typed(U'-'));
    view.handle(Key::typed(U'-'));
    window->draw(view.frame(), Theme::dark());
    CHECK(window->saveFrame(base + "-arrange-out.bmp"));
    view.handle(Key::of(NamedKey::Tab));
    window->draw(view.frame(), Theme::dark());
    CHECK(window->saveFrame(base + "-samples.bmp"));
}

TEST_CASE("a window can open at a size in cells")
{
    auto opened = Window::open({.title = "test",
                                .columns = 30,
                                .rows = 10,
                                .width = 0,
                                .height = 0,
                                .offscreen = true,
                                .font = {},
                                .minimumColumns = 0,
                                .minimumRows = 0});
    REQUIRE(std::holds_alternative<std::unique_ptr<Window>>(opened));
    const auto& window = *std::get<std::unique_ptr<Window>>(opened);
    CHECK(window.pixelWidth() == 30 * window.cellSize().width);
    CHECK(window.pixelHeight() == 10 * window.cellSize().height);
}

namespace
{

void pushKey(SDL_Keycode code, SDL_Keymod mod = SDL_KMOD_NONE)
{
    SDL_Event event{};
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.mod = mod;
    event.key.down = true;
    REQUIRE(SDL_PushEvent(&event));
}

void pushText(const char* text)
{
    SDL_Event event{};
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    REQUIRE(SDL_PushEvent(&event));
}

} // namespace

TEST_CASE("key presses reach the key handler as characters typed and named keys")
{
    auto window = offscreen(320, 200);
    // As a keyboard sends them: a press, then the text it types, if any.
    pushKey(SDLK_4, SDL_KMOD_LSHIFT);
    pushText("$");
    pushKey(SDLK_D, SDL_KMOD_LCTRL); // types nothing
    pushKey(SDLK_J, SDL_KMOD_LALT);
    pushText("j"); // typed with Alt by some systems, and the same key
    pushKey(SDLK_E, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_RALT)); // AltGr
    pushText("€");
    pushKey(SDLK_ESCAPE);
    pushKey(SDLK_DOWN, SDL_KMOD_LSHIFT);
    pushKey(SDLK_F5);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));

    std::vector<Key> keys;
    int frames = 0;
    window->run([&](Window&) { ++frames; },
                [&](const Key& key)
                {
                    keys.push_back(key);
                    return key.named == NamedKey::Escape;
                });
    CHECK(keys == std::vector<Key>{Key::typed(U'$'), Key::typed(U'd', kCtrl),
                                   Key::typed(U'j', kAlt), Key::typed(U'€'),
                                   Key::of(NamedKey::Escape), Key::of(NamedKey::Down, kShift),
                                   Key::of(NamedKey::F5)});
    CHECK(frames >= 1);
}
