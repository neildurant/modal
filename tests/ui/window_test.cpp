// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/temp_directory.hpp"

#include <modal/core/document.hpp>
#include <modal/core/drum_map_file.hpp>
#include <modal/core/player.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/ports.hpp>
#include <modal/ui/remembered.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/themes.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

    const auto theme = *builtInTheme("dark");
    window->draw({}, theme);
    CHECK(window->pixel(0, 0) == theme[Role::Chrome]); // around the view
    CHECK(window->pixel(window->pixelWidth() - 1, window->pixelHeight() - 1) ==
          theme[Role::Chrome]);
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
    const auto theme = *builtInTheme("dark");
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
    const auto theme = *builtInTheme("dark");
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

TEST_CASE(
    "an inset background stops short of the cell's top and bottom, the region's showing there")
{
    auto window = offscreen(320, 200);
    const auto theme = *builtInTheme("dark");
    const auto cell = window->cellSize();
    Region region{.bounds = {0, 0, window->pixelWidth(), cell.height},
                  .background = Role::Bar,
                  .text = {},
                  .under = {},
                  .over = {}};
    region.text.push_back({0, 0, U"  ", Style{Role::Text, Role::Indicator, kInset}});
    region.text.push_back({cell.width * 3, 0, U"  ", Style{Role::Text, Role::Indicator, kPlain}});
    window->draw({region}, theme);
    CHECK(window->pixel(1, 0) == theme[Role::Bar]);
    CHECK(window->pixel(1, cell.height / 2) == theme[Role::Indicator]);
    CHECK(window->pixel(1, cell.height - 1) == theme[Role::Bar]);
    CHECK(window->pixel(cell.width * 3 + 1, 0) == theme[Role::Indicator]); // not inset
}

TEST_CASE("a region is clipped to its bounds, with shapes under and over its text")
{
    auto window = offscreen(320, 200);
    const auto theme = *builtInTheme("dark");
    Region region{.bounds = {10, 10, 40, 30},
                  .background = Role::Bar,
                  .text = {},
                  .under = {{{-100, -100, 1000, 1000}, Role::Note, 255}},
                  .over = {{{20, 0, 5, 30}, Role::Label, 255}}};
    window->draw({region}, theme);
    CHECK(window->pixel(9, 9) == theme[Role::Chrome]);
    CHECK(window->pixel(10, 10) == theme[Role::Note]);
    CHECK(window->pixel(49, 39) == theme[Role::Note]);
    CHECK(window->pixel(50, 40) == theme[Role::Chrome]);
    CHECK(window->pixel(32, 20) == theme[Role::Label]); // over, offset by the bounds
}

TEST_CASE("a bottom bar touches the bottom edge, whatever the window's height")
{
    auto window = offscreen(320, 203);
    auto theme = *builtInTheme("dark");
    theme.set(Role::Chrome, theme[Role::Background]); // the built-in chrome is the bar's colour
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
    window->draw({gridRegion(grid, {0, 0, 640, 260}, cell, Role::Background)},
                 *builtInTheme("dark"));
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    CHECK(window->saveFrame(path != nullptr ? path : "font-sample.bmp"));
}

// Hidden: writes the event list, the arrangement, the piano roll and the arrangement sketch
// to look at, to MODAL_SAMPLE_BMP with "-events", "-events-light", "-playing",
// "-playing-light", "-marker", "-arrangement", "-conductor", "-piano", "-piano-light",
// "-arrange" and "-arrange-out" (zoomed out) before its extension, or to sample.bmp and so on.
TEST_CASE("a sample of the event list, the arrangement and the piano roll", "[.sample]")
{
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));

    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    for (const char32_t key : std::u32string_view{U"12jll"})
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-events.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-events-light.bmp"));

    // Playing, three seconds in, the cursor left where it was.
    modal::test::RecordingSink sink;
    modal::test::FakeClock clock;
    modal::core::Engine engine{sink, clock};
    modal::core::Player player{engine,
                               [](std::string_view) { return modal::core::DestinationId{1}; }};
    player.update(document.song());
    view.setPlayer(&player);
    view.handle(Key::typed(U' '), 0);
    modal::test::runUntil(engine, clock, clock.time + std::chrono::seconds{3});
    view.wake(*view.deadline());
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-playing.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-playing-light.bmp"));
    CHECK(player.stop());
    view.wake(*view.deadline());
    for (const char32_t key : std::u32string_view{U"6km j"}) // the marker a row above the cursor
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-marker.bmp"));
    view.run("view-tracks", 0);
    const auto beforeTracks = document.song();
    document.edit("Example track settings", {},
                  [](modal::core::Song& song)
                  {
                      song.updateTrack(
                          song.tracks()[0]->id(),
                          [&](modal::core::Track& track)
                          {
                              track.mute = true;
                              track.timeOffset = modal::core::TimeOffset::ticks(
                                  modal::core::NoteFraction{1, 16}.toTicks(song.ppqn()) - 14);
                          });
                      song.updateTrack(song.tracks()[1]->id(),
                                       [](modal::core::Track& track)
                                       {
                                           track.solo = true;
                                           track.arm = true;
                                           track.transpose = -12;
                                       });
                      song.updateTrack(song.tracks()[3]->id(),
                                       [](modal::core::Track& track) { track.thru = false; });
                      song.updateTrack(song.tracks()[2]->id(),
                                       [](modal::core::Track& track)
                                       {
                                           track.chaseNotes = false;
                                           track.velocityOffset = 15;
                                       });
                  });
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-tracks.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-tracks-light.bmp"));
    document.edit("Restore sample song", {}, [&](modal::core::Song& song) { song = beforeTracks; });
    // The Ports view, with a machine's ports: some enabled, aliased, one the default, one gone.
    modal::ui::PortTable ports;
    using modal::ui::PortDirection;
    ports.detected(PortDirection::Output, std::vector<std::string>{"Old Synth"});
    ports.detected(PortDirection::Input, std::vector<std::string>{"Xkey 1", "loopMIDI Port 1"});
    ports.detected(PortDirection::Output,
                   std::vector<std::string>{"loopMIDI Port 1", "Babyface Midi Port 1",
                                            "Microsoft GS Wavetable Synth"});
    ports.setEnabled(PortDirection::Input, "Xkey 1", true);
    ports.setAlias(PortDirection::Input, "Xkey 1", "Keys");
    ports.setEnabled(PortDirection::Output, "loopMIDI Port 1", true);
    ports.setAlias(PortDirection::Output, "loopMIDI Port 1", "Host");
    ports.setEnabled(PortDirection::Output, "Babyface Midi Port 1", true);
    ports.setAlias(PortDirection::Output, "Babyface Midi Port 1", "Babyface Out");
    ports.setEnabled(PortDirection::Output, "Old Synth", true);
    ports.setDefault(PortDirection::Output, "loopMIDI Port 1");
    ports.setDefault(PortDirection::Input, "Xkey 1");
    view.setPortControl({.table = [&] { return &ports; },
                         .change = [&](const modal::ui::PortTable& changed) { ports = changed; },
                         .look = [] {}});
    view.run("view-ports", 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-ports.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-ports-light.bmp"));
    // The metronome page, the cursor on the downbeat's note.
    view.run("view-metronome", 0);
    for (const char32_t key : std::u32string_view{U"3j"})
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-metronome.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-metronome-light.bmp"));
    // Its port's drop-down list open.
    view.handle(Key::typed(U'k'), 0);
    view.handle(Key::typed(U'k'), 0);
    view.handle(Key::of(NamedKey::Enter), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-metronome-list.bmp"));
    view.handle(Key::of(NamedKey::Escape), 0);
    // Tracks on those ports: Default and All dim, a port by alias, one gone and one disabled.
    view.run("view-tracks", 0);
    const auto beforePorts = document.song();
    document.edit("Example ports", {},
                  [](modal::core::Song& song)
                  {
                      const auto set = [&](std::size_t i, std::string port, std::string input)
                      {
                          song.updateTrack(song.tracks()[i]->id(),
                                           [&](modal::core::Track& track)
                                           {
                                               track.port = port;
                                               track.input = input;
                                           });
                      };
                      set(1, "Babyface Out", "Keys");
                      set(2, "Old Synth", std::string{modal::core::Track::kAllInputs});
                      set(3, "Microsoft GS Wavetable Synth", "Pads");
                  });
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-tracks-ports.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-tracks-ports-light.bmp"));
    document.edit("Restore sample song", {}, [&](modal::core::Song& song) { song = beforePorts; });
    view.run("view-arrangement", 0);
    for (const char32_t key : std::u32string_view{U"jj8l"})
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-arrangement.bmp"));
    // The palette, its line at the top, and the : prompt's completion popup.
    view.handle(Key::typed(U'p', kCtrl), 0);
    for (const char32_t key : std::u32string_view{U"zo"})
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-palette.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-palette-light.bmp"));
    view.handle(Key::of(NamedKey::Escape), 0);
    for (const char32_t key : std::u32string_view{U":bar-insert "})
    {
        view.handle(Key::typed(key), 0);
    }
    view.handle(Key::of(NamedKey::Tab), 0);
    view.handle(Key::of(NamedKey::Tab), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-complete.bmp"));
    view.handle(Key::of(NamedKey::Escape), 0);
    view.setPlayer(nullptr);
    view.run("track-previous", 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-conductor.bmp"));
    view.run("view-piano-roll", 0);
    view.handle(Key::of(NamedKey::Tab), 0); // onto a note
    view.handle(Key::of(NamedKey::Tab), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-piano.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-piano-light.bmp"));
    // Eight bars about the Verse's start, with the play-from marker in view.
    view.setPlayer(&player);
    for (const char32_t key : std::u32string_view{U"8Gzzm zo"})
    {
        view.handle(Key::typed(key), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-piano-sections.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-piano-sections-light.bmp"));
    view.setPlayer(nullptr);

    const auto layout =
        layoutWindow(window->pixelWidth(), window->pixelHeight(), window->cellSize(), 1, 1);
    ArrangePage arrange{demoSong()};
    arrange.resize(layout.main, window->cellSize());
    arrange.moveTracks(6);
    arrange.moveBars(9);
    const auto draw = [&]
    {
        Frame frame;
        arrange.addRegions(frame);
        window->draw(frame, *builtInTheme("dark"));
    };
    draw();
    CHECK(window->saveFrame(base + "-arrange.bmp"));
    arrange.zoomBy(3); // to two whole notes to a column
    draw();
    CHECK(window->saveFrame(base + "-arrange-out.bmp"));
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

// A key pressed, as SDL reports it: the key code with Shift applied, and the scancode of the
// key itself, if given.
void pushKey(SDL_Keycode code, SDL_Keymod mod = SDL_KMOD_NONE,
             SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN)
{
    SDL_Event event{};
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.scancode = scancode;
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
    // With Shift as well, SDL's key code is shifted, but the key is still d, or =.
    const auto ctrlShift = static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT);
    pushKey(SDLK_D | SDLK_EXTENDED_MASK, ctrlShift, SDL_SCANCODE_D); // a code SDL wouldn't give
    pushKey(SDLK_PLUS, ctrlShift, SDL_SCANCODE_EQUALS);
    pushKey(SDLK_KP_PLUS, SDL_KMOD_LCTRL); // the keypad's own, with Ctrl
    pushKey(SDLK_KP_PLUS);                 // and its character without
    pushText("+");
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));

    std::vector<Key> keys;
    int frames = 0;
    window->run({.redraw = [&](Window&) { ++frames; },
                 .key =
                     [&](const Key& key, std::int64_t)
                 {
                     keys.push_back(key);
                     return key.named == NamedKey::Escape;
                 },
                 .deadline = {},
                 .wake = {}});
    CHECK(keys == std::vector<Key>{Key::typed(U'$'), Key::typed(U'd', kCtrl),
                                   Key::typed(U'j', kAlt), Key::typed(U'€'),
                                   Key::of(NamedKey::Escape), Key::of(NamedKey::Down, kShift),
                                   Key::of(NamedKey::F5), Key::typed(U'd', kCtrl | kShift),
                                   Key::typed(U'=', kCtrl | kShift),
                                   Key::of(NamedKey::KeypadPlus, kCtrl), Key::typed(U'+')});
    CHECK(frames >= 1);
}

TEST_CASE("a window wakes at its handlers' deadline with no events, and draws if asked")
{
    auto window = offscreen(320, 200);
    const auto start = Window::now();
    std::optional<std::int64_t> deadline = start + 30;
    std::int64_t wokeAt = 0;
    int frames = 0;
    window->run({.redraw = [&](Window&) { ++frames; },
                 .key = {},
                 .deadline = [&] { return deadline; },
                 .wake =
                     [&](std::int64_t now)
                 {
                     wokeAt = now;
                     deadline.reset();
                     SDL_Event quit{};
                     quit.type = SDL_EVENT_QUIT;
                     SDL_PushEvent(&quit);
                     return true;
                 }});
    CHECK(wokeAt >= start + 30);
    CHECK(frames >= 2); // the first, and the one the wake-up asked for
}

TEST_CASE("Ctrl with Space, from either Ctrl key, plays from the cursor")
{
    auto window = offscreen(320, 200);
    pushKey(SDLK_SPACE, SDL_KMOD_LCTRL, SDL_SCANCODE_SPACE);
    pushKey(SDLK_SPACE, SDL_KMOD_RCTRL, SDL_SCANCODE_SPACE);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));
    std::vector<Key> keys;
    window->run({.redraw = [](Window&) {},
                 .key =
                     [&](const Key& key, std::int64_t)
                 {
                     keys.push_back(key);
                     return false;
                 },
                 .deadline = {},
                 .wake = {}});
    REQUIRE(keys.size() == 2);
    const auto keymap = Workspace::defaultKeymap();
    for (const auto& key : keys)
    {
        CHECK(key == Key::typed(U' ', kCtrl));
        const auto match = keymap.match(Mode::Normal, std::vector<Key>{key});
        REQUIRE(match.binding != nullptr);
        CHECK(match.binding->command == "play-cursor");
    }
}

TEST_CASE("events are handled between frames even when the deadline is always past")
{
    // As while playing, when a frame takes longer to draw than the time between frames.
    auto window = offscreen(320, 200);
    pushKey(SDLK_ESCAPE);
    int keys = 0;
    int wakes = 0;
    constexpr int kGiveUp = 200; // then stop waking, so a loop that starves events still ends
    window->run({.redraw = [](Window&) {},
                 .key =
                     [&](const Key&, std::int64_t)
                 {
                     ++keys;
                     SDL_Event quit{};
                     quit.type = SDL_EVENT_QUIT;
                     SDL_PushEvent(&quit);
                     return false;
                 },
                 .deadline = [&]() -> std::optional<std::int64_t>
                 {
                     if (wakes >= kGiveUp)
                     {
                         return std::nullopt;
                     }
                     return Window::now() - 1;
                 },
                 .wake =
                     [&](std::int64_t)
                 {
                     ++wakes;
                     return true;
                 }});
    CHECK(keys == 1);
    CHECK(wakes < kGiveUp);
}

TEST_CASE("a window asked to close asks its handler first, and stays open if told to")
{
    auto window = offscreen(320, 200);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));
    REQUIRE(SDL_PushEvent(&quit));
    int asked = 0;
    window->run({.redraw = [](Window&) {},
                 .key = {},
                 .deadline = {},
                 .wake = {},
                 .closing = [&] { return ++asked == 2; }}); // not the first time
    CHECK(asked == 2);
}

TEST_CASE("a window's title can be changed")
{
    auto window = offscreen(320, 200);
    window->setTitle("song.modal [+] – Modal");
    CHECK(window->title() == "song.modal [+] – Modal");
}

TEST_CASE("closing a window ends its event loop once the events in hand are done")
{
    auto window = offscreen(320, 200);
    pushKey(SDLK_ESCAPE);
    pushKey(SDLK_ESCAPE);
    int keys = 0;
    window->run({.redraw = [](Window&) {},
                 .key =
                     [&](const Key&, std::int64_t)
                 {
                     ++keys;
                     window->close();
                     return false;
                 },
                 .deadline = {},
                 .wake = {}});
    CHECK(keys >= 1); // and it returned, with no quit event
}

// Hidden: writes the status line's indicators to look at, in the built-in themes and in
// variants of their colours, to MODAL_SAMPLE_BMP with "-status-…" before its extension, or to
// sample-status-….bmp: as a song opens, and armed to record with the click and a setting off.
TEST_CASE("a sample of the status line's indicators", "[.sample]")
{
    auto window = offscreen(1900, 120);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));

    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    modal::test::RecordingSink sink;
    modal::test::FakeClock clock;
    modal::core::Engine engine{sink, clock};
    modal::core::Player player{engine,
                               [](std::string_view) { return modal::core::DestinationId{1}; }};
    player.update(document.song());
    view.setPlayer(&player);

    // Variants: as built in; the settings in a muted blue with light text; each its own colour.
    const auto variants = [](Theme theme, bool dark)
    {
        std::vector<std::pair<std::string, Theme>> all{{"", theme}};
        auto muted = theme;
        const auto quiet = *parseColour(dark ? "#3d59a1" : "#9fb3e0");
        for (const auto role :
             {Role::IndicatorThru, Role::IndicatorAudition, Role::IndicatorRewind})
            muted.set(role, quiet);
        muted.set(Role::IndicatorLitText, *parseColour(dark ? "#e6e9f5" : "#1f2a52"));
        all.emplace_back("-muted", muted);
        auto own = theme;
        own.set(Role::IndicatorThru, theme[Role::TrackThru]);
        own.set(Role::IndicatorAudition, theme[Role::TrackSolo]);
        own.set(Role::IndicatorRewind, theme[Role::TrackChase]);
        own.set(Role::IndicatorCount, theme[Role::TrackQuantize]);
        all.emplace_back("-own", own);
        return all;
    };
    const auto save = [&](const std::string& state)
    {
        for (const auto& [name, dark] : {std::pair{"dark", true}, std::pair{"light", false}})
            for (const auto& [variant, theme] : variants(*builtInTheme(name), dark))
            {
                window->draw(view.frame(), theme);
                CHECK(window->saveFrame(base + "-status-" + state + (dark ? "" : "-light") +
                                        variant + ".bmp"));
            }
    };
    save("open");
    for (const char32_t key : std::u32string_view{U",ra,Ta"})
        view.handle(Key::typed(key), 0);
    view.handle(Key::of(NamedKey::Escape), 0);
    save("armed");
    // The message row: a message, a warning, the prompt, and a Visual selection.
    view.setMessage("Armed Bass to record", MessageKind::Information);
    save("message");
    view.setMessage("Bass is being recorded: change it once recording stops", MessageKind::Warning);
    save("warning");
    view.handle(Key::of(NamedKey::Escape), 0);
    for (const char32_t key : std::u32string_view{U":countin 2"})
        view.handle(Key::typed(key), 0);
    save("prompt");
    view.handle(Key::of(NamedKey::Escape), 0);
    for (const char32_t key : std::u32string_view{U"Vjj"})
        view.handle(Key::typed(key), 0);
    save("visual");
    view.handle(Key::of(NamedKey::Escape), 0);
    // Counting in to record, then the piano roll, with its grid in its own bar.
    view.handle(Key::typed(U'R'), 0);
    modal::test::runUntil(engine, clock, clock.time + std::chrono::milliseconds{10});
    view.wake(*view.deadline());
    save("recording");
    view.handle(Key::typed(U' '), 0);
    modal::test::runUntil(engine, clock, clock.time + std::chrono::milliseconds{10});
    view.wake(view.deadline().value_or(0));
    view.show(ViewKind::PianoRoll);
    save("piano");
    view.show(ViewKind::EventList);
    // Narrower and narrower, as what's least needed gives way, with a section name long enough
    // to be cut short.
    document.edit("Rename section", {},
                  [](modal::core::Song& song) { song.renameSection(0, "Introduction"); });
    for (const int width : {1900, 1500, 1350, 1200, 1100, 1000, 900, 800, 700, 600})
    {
        view.resize(width, window->pixelHeight(), window->cellSize());
        window->draw(view.frame(), *builtInTheme("dark"));
        CHECK(window->saveFrame(base + "-status-narrow-" + std::to_string(width) + ".bmp"));
    }
}
TEST_CASE("a sample of the Metronome page and the Ports and Tracks views", "[.sample]")
{
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace view{registry, document};
    PortTable table;
    table.detected(PortDirection::Output, std::vector<std::string>{"Gone Synth"});
    table.detected(PortDirection::Input, std::vector<std::string>{"Xkey 1", "loopMIDI Port 1"});
    table.detected(PortDirection::Output,
                   std::vector<std::string>{"loopMIDI Port 1", "Babyface Midi Port 1"});
    table.setEnabled(PortDirection::Input, "Xkey 1", true);
    table.setEnabled(PortDirection::Output, "Gone Synth", true);
    table.setEnabled(PortDirection::Output, "Babyface Midi Port 1", true);
    table.setDefault(PortDirection::Output, "Babyface Midi Port 1");
    view.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
            view.handle(Key::typed(key), 0);
    };
    // As large as a window that's been made big, and at the smallest a window can be.
    for (const auto& [name, width, height] : {std::tuple{"large", 1600, 900}, {"small", 1200, 600}})
    {
        auto window = offscreen(width, height);
        view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
        const auto save = [&](const std::string& page)
        {
            window->draw(view.frame(), *builtInTheme("dark"));
            CHECK(window->saveFrame(base + "-" + page + "-" + name + ".bmp"));
        };
        type(U",ms");
        save("metronome");
        type(U"jjjl");
        save("metronome-test");
        view.handle(Key::of(NamedKey::Escape), 0);
        type(U",vm");
        save("ports");
        type(U"jj");
        save("ports-gone");
        type(U"gt");
        save("tracks");
    }
}

TEST_CASE("a sample of the file picker and its Places", "[.sample]")
{
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::test::TempDirectory folder{"modal-picker-sample"};
    std::filesystem::create_directories(folder / "Config");
    std::filesystem::create_directories(folder / "Ideas");
    for (const auto* name : {"Ballad.modal", "Groove.modal", "Morning phrase.modal", "Drums.mid"})
        REQUIRE(modal::core::writeSongFile(demoSong(), folder / name).empty());
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace view{registry, document};
    view.setFolders(folder / "Config", folder.path());
    for (const auto& theme : {"dark", "light"})
    {
        auto window = offscreen(1200, 600);
        view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
        const auto save = [&](const std::string& name)
        {
            window->draw(view.frame(), *builtInTheme(theme));
            CHECK(window->saveFrame(base + "-picker-" + name + "-" + theme + ".bmp"));
        };
        view.pickFile(Workspace::PickerKind::Song, std::nullopt, [](auto) {});
        view.handle(Key::of(NamedKey::Down), 0);
        save("files");
        view.handle(Key::of(NamedKey::Down, kAlt), 0);
        save("places");
        view.handle(Key::of(NamedKey::Escape), 0);
        view.handle(Key::of(NamedKey::Escape), 0);
    }
}

TEST_CASE("a sample of the recent songs", "[.sample]")
{
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace view{registry, document};
    const std::int64_t now = 1'763'662'000; // 20 November 2025, in the evening
    view.setClock([=] { return now; });
    modal::test::TempDirectory folder{"modal-recent-sample"};
    for (const auto* name :
         {"Songs/Groove.modal", "Songs/Ballad.modal", "Archive/Groove 2024.modal"})
    {
        std::filesystem::create_directories((folder / name).parent_path());
        REQUIRE(modal::core::writeSongFile(demoSong(), folder / name).empty());
    }
    Remembered remembered;
    const auto day = std::int64_t{24 * 60 * 60};
    const auto at = [&](const char* name) { return utf8Text(folder / name); };
    remembered.usedSong(at("Archive/Groove 2024.modal"), now - 400 * day, 20);
    remembered.usedSong(at("Desktop/Demo ideas.modal"), now - 5 * day, 20); // gone
    remembered.usedSong(at("Songs/Ballad.modal"), now - day, 20, "Slow one");
    remembered.usedSong(at("Songs/Groove.modal"), now - 600, 20, "Groove in D");
    view.setRemembered(remembered, {});
    for (const auto& theme : {"dark", "light"})
    {
        auto window = offscreen(1200, 600);
        view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
        view.report(registry.runLine("song-recent"));
        window->draw(view.frame(), *builtInTheme(theme));
        CHECK(window->saveFrame(base + "-recent-" + theme + ".bmp"));
        view.handle(Key::of(NamedKey::Escape), 0);
    }
}

TEST_CASE("a sample of the open songs", "[.sample]")
{
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace view{registry, document};
    for (const auto& theme : {"dark", "light"})
    {
        auto window = offscreen(1200, 600);
        view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
        view.chooseSong({{"Morning", "Dawn.modal", "/home/me/Music/Songs", true},
                         {"", "Groove.modal", "/home/me/Music/Songs", false},
                         {"Test arrangement", "demo.mid", "/home/me/Downloads", false, true},
                         {"Sketch", "Untitled 4", "Unsaved", true}},
                        0, [](std::size_t) {});
        window->draw(view.frame(), *builtInTheme(theme));
        CHECK(window->saveFrame(base + "-open-songs-" + theme + ".bmp"));
        view.handle(Key::of(NamedKey::Escape), 0);
    }
}

TEST_CASE("a sample of the track-import chooser", "[.sample]")
{
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::test::TempDirectory folder{"modal-import-sample"};
    const auto file = folder / "Groove.modal";
    REQUIRE(modal::core::writeSongFile(demoSong(), file).empty());
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace view{registry, document};
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
            view.handle(Key::typed(key), 0);
    };
    for (const auto& theme : {"dark", "light"})
    {
        auto window = offscreen(1200, 600);
        view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
        const auto save = [&](const std::string& name)
        {
            window->draw(view.frame(), *builtInTheme(theme));
            CHECK(window->saveFrame(base + "-import-" + name + "-" + theme + ".bmp"));
        };
        view.report(registry.runLine("track-import " + utf8Text(file)));
        save("open");
        type(U" jj jj ");
        save("ticked");
        view.handle(Key::of(NamedKey::Escape), 0);
    }
}

TEST_CASE("a sample of marks in each view", "[.sample]")
{
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));

    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            view.handle(Key::typed(key), 0);
        }
    };
    type(U"12jjmajjjmb"); // the event list: two rows
    view.run("view-arrangement", 0);
    type(U"gg3}mcj2}md"); // the arrangement: bars 4 and 6
    view.run("view-piano-roll", 0);
    view.handle(Key::of(NamedKey::Tab), 0); // onto a note
    type(U"meh");                           // a mark at the note's pitch
    view.run("view-events", 0);
    type(U"jjjjmfjjmg"); // two rows of this track's
    view.run("view-conductor", 0);
    type(U"jjjmi"); // one of the conductor's
    // A label at the second section's start, which its name shares.
    document.edit("Label", {}, [](modal::core::Song& song)
                  { song.setLabel(song.sectionStart(1), "fix the fill"); });
    view.run("view-events", 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-marks-events.bmp"));
    view.run("view-arrangement", 0);
    type(U"lmj"); // the bar after d's, close to it once zoomed out
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-marks-arrangement.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-marks-arrangement-light.bmp"));
    for (int zoom = 1; zoom <= 4; ++zoom)
    {
        view.run("zoom-out", 0);
        window->draw(view.frame(), *builtInTheme("dark"));
        CHECK(window->saveFrame(base + "-marks-arrangement-out" + std::to_string(zoom) + ".bmp"));
    }
    view.run("view-piano-roll", 0);
    type(U"llmk"); // a note's, two steps on
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-marks-piano.bmp"));
}

TEST_CASE("a sample of search matches", "[.sample]")
{
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));

    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    // Other notes on the piano among its Cs, which don't match.
    document.edit("Notes", {},
                  [](modal::core::Song& song)
                  {
                      const auto q = song.ppqn().quarter();
                      song.updateEvents(
                          song.tracks()[2]->id(),
                          [&](modal::core::EventList& events)
                          {
                              for (int i = 0; i < 12; ++i)
                              {
                                  const auto pitch = static_cast<std::uint8_t>(55 + (i * 5) % 12);
                                  events.insert(
                                      {4 * q + i * q, modal::core::Note{pitch, 90, q / 2}});
                              }
                          });
                  });
    view.run("view-arrangement", 0);
    view.handle(Key::typed(U'j'), 0); // the piano
    view.handle(Key::typed(U'j'), 0);
    view.run("view-piano-roll", 0);
    for (const char32_t key : std::u32string_view{U"/c4"})
    {
        view.handle(Key::typed(key), 0);
    }
    view.handle(Key::of(NamedKey::Enter), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-search-piano.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-search-piano-light.bmp"));
    view.run("view-events", 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-search-events.bmp"));
}

TEST_CASE("a sample of the velocity lane", "[.sample]")
{
    // The piano roll's velocity lane with the cursor in it, chords fanned out, then a Ctrl-v
    // box taking the louder notes.
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            view.handle(Key::typed(key), 0);
        }
    };
    // Chords on the piano: its notes from bar 2 with a third and fifth above, softer, and the
    // fifth a little late, as played.
    document.edit(
        "Chords", {},
        [](modal::core::Song& song)
        {
            const auto id = song.tracks()[2]->id();
            std::vector<modal::core::Event> added;
            for (const auto& event : song.events(id).all())
            {
                if (const auto* note = std::get_if<modal::core::Note>(&event.data))
                {
                    added.push_back(
                        {event.tick, modal::core::Note{static_cast<std::uint8_t>(note->pitch + 4),
                                                       70, note->duration}});
                    added.push_back({event.tick + 5,
                                     modal::core::Note{static_cast<std::uint8_t>(note->pitch + 7),
                                                       85, note->duration}});
                }
            }
            song.updateEvents(id,
                              [&](modal::core::EventList& events)
                              {
                                  for (const auto& event : added)
                                      events.insert(event);
                              });
        });
    type(U"gr");
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0); // the piano
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    type(U"gl");
    view.handle(Key::of(NamedKey::Tab), 0);
    view.handle(Key::of(NamedKey::Tab), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-lane.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-lane-light.bmp"));
    view.handle(Key::typed(U'v', kCtrl), 0);
    type(U"4lJ");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-lane-box.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-lane-box-light.bmp"));
}

TEST_CASE("a sample of controller lanes", "[.sample]")
{
    // The piano's lane showing a mod-wheel swell, then pitch bend, in the cursor's lane.
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    registry.add(windowCommands());
    view.setLineHandler([&](const std::string& line) { registry.runLine(line); });
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    document.edit(
        "Controllers", {},
        [](modal::core::Song& song)
        {
            const auto id = song.tracks()[2]->id();
            const auto q = song.ppqn().quarter();
            song.updateEvents(
                id,
                [&](modal::core::EventList& events)
                {
                    for (int i = 0; i < 32; ++i)
                    {
                        const auto value =
                            static_cast<std::uint8_t>(i < 16 ? i * 8 : (32 - i) * 8 - 1);
                        events.insert({4 * q + i * q / 4, modal::core::ControlChange{1, value}});
                    }
                    for (int i = 0; i < 8; ++i)
                    {
                        events.insert({8 * q + i * q / 2,
                                       modal::core::PitchBend{
                                           static_cast<std::int16_t>((i % 4 - 2) * 2000)}});
                    }
                });
        });
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            view.handle(Key::typed(key), 0);
        }
    };
    type(U"gr");
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    type(U":lane cc1");
    view.handle(Key::of(NamedKey::Enter), 0);
    type(U"gl]b");
    for (int i = 0; i < 6; ++i)
    {
        view.handle(Key::of(NamedKey::Tab), 0);
    }
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-cc.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-cc-light.bmp"));
    // A nudge or two on, between events.
    type(U"LLL");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-between.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-between-light.bmp"));
    // Half the view, for precise work.
    type(U",ch,ch");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-cc-half.bmp"));
    type(U",ch");
    type(U"]c");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-bend.bmp"));
    type(U",cc");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-choose.bmp"));
}

TEST_CASE("a sample of controller ramps", "[.sample]")
{
    // A swell of the expression controller across bar 2, curved, its draft open in the
    // inspector; then kept, with a fade made from a selection in bar 3, in a taller lane.
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    registry.add(windowCommands());
    view.setLineHandler([&](const std::string& line) { registry.runLine(line); });
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    document.edit("Expression", {},
                  [](modal::core::Song& song)
                  {
                      const auto id = song.tracks()[2]->id();
                      const auto q = song.ppqn().quarter();
                      song.updateEvents(id,
                                        [&](modal::core::EventList& events)
                                        {
                                            for (int i = 0; i < 12; ++i)
                                            {
                                                events.insert(
                                                    {i * q, modal::core::ControlChange{
                                                                11, static_cast<std::uint8_t>(
                                                                        90 + (i % 3) * 5)}});
                                            }
                                        });
                  });
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            view.handle(key == U'\t'   ? Key::of(NamedKey::Tab)
                        : key == U'\n' ? Key::of(NamedKey::Enter)
                                       : Key::typed(key),
                        0);
        }
    };
    type(U"gr");
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    type(U":lane cc11\ngl]b");
    type(U",cr95\t20\t");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-ramp-shape.bmp"));
    type(U"c\t40\t");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-ramp-draft.bmp"));
    view.handle(Key::of(NamedKey::Enter, kCtrl), 0);
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-ramp-kept.bmp"));
    type(U"]bvll");
    view.handle(Key::of(NamedKey::Right, kNoModifiers), 0);
    type(U",cr\t0\n");
    view.handle(Key::of(NamedKey::Enter, kCtrl), 0);
    type(U",ch,ch");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-ramps.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-ramps-light.bmp"));
}

TEST_CASE("a sample of the sustain lane", "[.sample]")
{
    // The piano's sustain as regions: plain pedalling in bar 2, half-pedalling within a region
    // in bar 3, and a continuous pedal's stream of values in bar 4.
    auto window = offscreen(1000, 600);
    const char* path = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = path != nullptr ? path : "sample.bmp";
    base = base.substr(0, base.rfind('.'));
    modal::core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace view{registry, document};
    registry.add(windowCommands());
    view.setLineHandler([&](const std::string& line) { registry.runLine(line); });
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    document.edit("Pedalling", {},
                  [](modal::core::Song& song)
                  {
                      const auto id = song.tracks()[2]->id();
                      const auto q = song.ppqn().quarter();
                      const auto bar = [&](int n) { return 4 * q * (n - 1); };
                      song.updateEvents(
                          id,
                          [&](modal::core::EventList& events)
                          {
                              // Clear what the demo has.
                              std::vector<std::size_t> pedals;
                              for (std::size_t i = 0; i < events.size(); ++i)
                                  if (const auto* c =
                                          std::get_if<modal::core::ControlChange>(&events[i].data);
                                      c && c->controller == 64)
                                      pedals.push_back(i);
                              for (auto it = pedals.rbegin(); it != pedals.rend(); ++it)
                                  events.erase(*it);
                              const auto pedal = [&](modal::core::Tick t, int v)
                              {
                                  events.insert({t, modal::core::ControlChange{
                                                        64, static_cast<std::uint8_t>(v)}});
                              };
                              // Bar 2: down on each beat's change, as a pianist pedals.
                              for (int beat = 0; beat < 4; ++beat)
                              {
                                  pedal(bar(2) + beat * q + q / 8, 127);
                                  pedal(bar(2) + (beat + 1) * q - q / 16, 0);
                              }
                              // Bar 3: down, eased to half, back down, then up.
                              pedal(bar(3) + q / 8, 127);
                              pedal(bar(3) + q, 90);
                              pedal(bar(3) + 2 * q, 72);
                              pedal(bar(3) + 3 * q, 127);
                              pedal(bar(4) - q / 16, 0);
                              // Bar 4: a continuous pedal, a stream rising and falling.
                              for (int i = 0; i <= 32; ++i)
                              {
                                  const int v = i <= 16 ? i * 8 : (32 - i) * 8;
                                  pedal(bar(4) + i * q / 8, std::min(v, 127));
                              }
                          });
                  });
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            view.handle(Key::typed(key), 0);
        }
    };
    type(U"gr");
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    view.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    type(U":lane cc64");
    view.handle(Key::of(NamedKey::Enter), 0);
    type(U"gl");
    // The regions, with tails after the notes they hold.
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-sustain.bmp"));
    window->draw(view.frame(), *builtInTheme("light"));
    CHECK(window->saveFrame(base + "-sustain-light.bmp"));
    // A quarter of the view.
    type(U",ch");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-sustain-quarter.bmp"));
    type(U",ch,ch");
    // The tint too, and the values.
    view.setSustainShown(true, true);
    type(U",cv");
    window->draw(view.frame(), *builtInTheme("dark"));
    CHECK(window->saveFrame(base + "-sustain-values.bmp"));
}

TEST_CASE("a sample of drum names and saved-map comparison", "[.sample][.manual-images]")
{
    using namespace modal;
    const char* manual = std::getenv("MODAL_MANUAL_IMAGES");
    const char* sample = std::getenv("MODAL_SAMPLE_BMP");
    std::string base = sample != nullptr ? sample : "sample.bmp";
    base = base.substr(0, base.rfind('.')) + "-drum-map";
    if (manual != nullptr)
    {
        std::filesystem::create_directories(utf8Path(manual));
        base = utf8Text(utf8Path(manual) / "drum-map");
    }
    const test::TempDirectory temporary{"modal-drum-map-samples"};
    const auto folder = temporary.path() / "drum-maps";
    std::filesystem::create_directories(folder);
    core::DrumMap map;
    map.name = "Studio kit";
    map.notes[36] = "Kick";
    map.notes[38] = "Snare";
    map.notes[42] = "Closed hi-hat";
    map.notes[46] = "Open hi-hat";
    auto saved = map;
    saved.notes[36] = "Acoustic kick";
    saved.notes[38] = "Snare centre";
    REQUIRE(core::writeDrumMapFile(saved, folder / "Studio kit.drum-map").empty());
    core::Song song;
    const auto id = song.addTrack("Drums");
    song.updateTrack(id,
                     [&](core::Track& track)
                     {
                         track.drumMap = map;
                         track.channel = 10;
                     });
    song.updateEvents(
        id,
        [&](core::EventList& events)
        {
            const auto q = song.ppqn().quarter();
            for (int bar = 0; bar < 2; ++bar)
            {
                const auto start = bar * 4 * q;
                for (int i = 0; i < 8; ++i)
                    events.insert(
                        {start + i * q / 2,
                         core::Note{static_cast<std::uint8_t>(i == 7 ? 46 : 42), 70, q / 4}});
                events.insert({start, core::Note{36, 100, q / 4}});
                events.insert({start + 2 * q, core::Note{36, 90, q / 4}});
                events.insert({start + q, core::Note{38, 100, q / 4}});
                events.insert({start + 3 * q, core::Note{38, 100, q / 4}});
            }
        });
    song.fitToContent();
    core::Document document{song};
    CommandRegistry registry;
    Workspace view{registry, document};
    registry.add(windowCommands());
    view.setFolders(temporary.path(), temporary.path());
    view.takeDrumMapNotice();
    auto window = offscreen(1200, 760);
    view.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    view.show(ViewKind::Arrangement);
    const auto type = [&](std::u32string_view keys)
    {
        for (const auto c : keys)
            view.handle(c == U'\n' ? Key::of(NamedKey::Enter) : Key::typed(c), 0);
    };
    const auto save = [&](const std::string& suffix, std::string_view theme = "dark")
    {
        window->draw(view.frame(), *builtInTheme(theme));
        CHECK(window->saveFrame(base + suffix + ".bmp"));
    };
    type(U"gr");
    view.handle(Key::of(NamedKey::Tab), 0);
    view.handle(Key::of(NamedKey::Tab, kShift), 0);
    save("");
    save("-light", "light");
    type(U",tk");
    save("-chooser");
    save("-chooser-light", "light");
    type(U"\n");
    save("-review");
    save("-review-light", "light");
    view.handle(Key::of(NamedKey::Escape), 0);
    type(U"gt0");
    for (const auto& column : trackColumns())
    {
        if (column.field == TrackField::DrumMap)
            break;
        type(U"l");
    }
    save("-tracks");
    save("-tracks-light", "light");
}
