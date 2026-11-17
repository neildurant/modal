// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/themes.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace
{

using namespace modal;
using namespace modal::ui;

core::Song tutorial(core::Ppqn ppqn)
{
    const auto loaded = core::readSongFile(
        std::filesystem::path{MODAL_MANUAL_SOURCE_DIR} / "examples/first-phrase.modal", ppqn);
    INFO(loaded.error);
    REQUIRE(loaded.song);
    return *loaded.song;
}

struct Manual
{
    explicit Manual(core::Ppqn ppqn)
        : document{tutorial(ppqn)}
    {
        registry.add(windowCommands());
        ws.setLineHandler([&](const std::string& line) { registry.runLine(line); });
        ws.resize(1200, 760, {10, 20});
        ws.show(ViewKind::Arrangement);
    }

    void type(std::u32string_view keys)
    {
        for (const auto key : keys)
        {
            ws.handle(Key::typed(key), 0);
            INFO(ws.message());
            REQUIRE_FALSE(ws.refused());
        }
    }

    void press(NamedKey key, Modifiers modifiers = kNoModifiers)
    {
        ws.handle(Key::of(key, modifiers), 0);
        INFO(ws.message());
        REQUIRE_FALSE(ws.refused());
    }

    void piano()
    {
        type(U"gtgggrgg");
        press(NamedKey::Tab);
        press(NamedKey::Tab, kShift);
    }

    // The controllers chapter's swell: expression across the opening phrase, its draft open on
    // To with 110 typed.
    void swell()
    {
        piano();
        type(U":lane cc11");
        press(NamedKey::Enter);
        type(U"glv}}},cr40");
        press(NamedKey::Tab);
        type(U"110");
    }

    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE("the manual's tutorial song and worked edits agree", "[manual]")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Manual m{ppqn};
    const auto original = m.document.song();
    REQUIRE(original.tracks().size() == 2);
    REQUIRE(original.sections().size() == 2);
    CHECK(original.sections()[0]->length() == 4 * ppqn.whole());
    CHECK(original.sections()[1]->length() == 4 * ppqn.whole());
    const auto melody = original.tracks()[0]->id();
    const auto bass = original.tracks()[1]->id();
    REQUIRE(original.events(melody).size() == 14);
    REQUIRE(original.events(bass).size() == 8);
    m.piano();

    SECTION("selecting a bar and transposing equals the direct object")
    {
        m.type(U"vib>");
        const auto selected = m.document.song();
        CHECK(std::get<core::Note>(selected.events(melody)[0].data).pitch == 61);
        CHECK(selected.events(bass) == original.events(bass));
        m.press(NamedKey::Escape);
        m.type(U"u");
        CHECK(m.document.song() == original);
        m.type(U">ib");
        CHECK(m.document.song() == selected);
        m.type(U"u");
        CHECK(m.document.song() == original);
    }

    SECTION("a motion from beat two differs from the whole bar")
    {
        m.press(NamedKey::Tab);
        CHECK(m.ws.pianoRoll().cursor().tick == ppqn.quarter());
        m.type(U"d]b");
        const auto& events = m.document.song().events(melody);
        REQUIRE(events.size() == 12);
        CHECK(events[0] == original.events(melody)[0]);
        m.type(U"u");
        m.type(U"dib");
        CHECK(m.document.song().events(melody).size() == 11);
        m.type(U"u");
        CHECK(m.document.song() == original);
    }

    SECTION("count before transpose is the amount")
    {
        m.type(U"3>ib");
        CHECK(std::get<core::Note>(m.document.song().events(melody)[0].data).pitch == 63);
        CHECK(m.document.song().events(melody)[3] == original.events(melody)[3]);
    }

    SECTION("count after transpose is the number of bars")
    {
        m.type(U">3ib");
        const auto& events = m.document.song().events(melody);
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const auto before = std::get<core::Note>(original.events(melody)[i].data).pitch;
            const auto after = std::get<core::Note>(events[i].data).pitch;
            CHECK(after == before + (events[i].tick < 3 * ppqn.whole() ? 1 : 0));
        }
    }

    SECTION("contents leave silence, a bar as time closes up")
    {
        m.type(U"dib");
        CHECK(m.document.song().events(melody)[0].tick == ppqn.whole());
        m.type(U"u");
        m.type(U"dab");
        CHECK(m.document.song().events(melody)[0].tick == 0);
        CHECK(m.document.song().sections()[0]->length() == original.sections()[0]->length());
        CHECK(m.document.song().sections()[1] == original.sections()[1]);
        m.type(U"u");
        CHECK(m.document.song() == original);
    }

    SECTION("the strip's section object reaches both tracks")
    {
        m.type(U"gaggvisd");
        CHECK(m.document.song().events(melody).size() == 6);
        CHECK(m.document.song().events(bass) == original.events(bass));
        m.press(NamedKey::Escape);
        m.type(U"ugsggvisd");
        CHECK(m.document.song().events(melody).size() == 6);
        CHECK(m.document.song().events(bass).size() == 4);
        m.press(NamedKey::Escape);
        m.type(U"u");
        CHECK(m.document.song() == original);
    }

    SECTION("a copied bar merges into bar two and undoes")
    {
        m.type(U"yib2Gp");
        CHECK(m.document.song().events(melody).size() == 17);
        CHECK(m.document.song().events(bass) == original.events(bass));
        m.type(U"u");
        CHECK(m.document.song() == original);
    }

    SECTION("a swell of expression is one ramp, kept or cancelled whole")
    {
        m.swell();
        m.press(NamedKey::Enter, kCtrl);
        const auto ramps = m.document.song().ramps(melody);
        REQUIRE(ramps.size() == 1);
        CHECK(ramps[0].lane == core::Lane{.kind = core::Lane::Kind::Controller, .controller = 11});
        CHECK(ramps[0].start == 0);
        CHECK(ramps[0].end() == 3 * ppqn.whole());
        CHECK(ramps[0].from == 40);
        CHECK(ramps[0].to == 110);
        CHECK(m.document.song().events(bass) == original.events(bass));
        // Choosing the lane is the track's, and stays; the ramp is undone whole.
        m.type(U"u");
        CHECK(m.document.song().ramps(melody).empty());
        CHECK(m.document.song().events(melody) == original.events(melody));

        // Escape puts back what's typed, and then makes no ramp.
        m.type(U"ggv}}},cr40");
        m.press(NamedKey::Escape);
        CHECK(m.document.song().ramps(melody).size() == 1);
        m.press(NamedKey::Escape);
        CHECK(m.document.song().ramps(melody).empty());
        CHECK(m.document.song().events(melody) == original.events(melody));
    }
}

TEST_CASE("render the manual's worked examples offscreen", "[.manual-images]")
{
    using namespace modal::ui::sdl;
    const char* output = std::getenv("MODAL_MANUAL_IMAGES");
    REQUIRE(output != nullptr);
    const std::filesystem::path directory{output};
    std::filesystem::create_directories(directory);
    auto opened = Window::open({.width = 1200, .height = 760, .offscreen = true, .font = {}});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        FAIL(*error);
    }
    auto window = std::move(std::get<std::unique_ptr<Window>>(opened));
    modal::test::RecordingSink sink;
    modal::test::FakeClock clock;
    modal::core::Engine engine{sink, clock};
    modal::core::Player player{engine,
                               [](std::string_view) { return modal::core::DestinationId{1}; }};
    Manual m{modal::core::kPpqn};
    player.update(m.document.song());
    m.ws.setPlayer(&player);
    m.ws.resize(window->pixelWidth(), window->pixelHeight(), window->cellSize());
    const auto theme = *builtInTheme("dark");
    const auto save = [&](std::string_view name)
    {
        window->draw(m.ws.frame(), theme);
        CHECK(window->saveFrame(utf8Text(directory / (std::string{name} + ".bmp"))));
    };
    save("arrangement");
    m.piano();
    save("piano-roll");
    m.type(U"gl");
    save("velocity-lane");
    m.press(NamedKey::Escape);
    m.type(U"vib");
    save("selection");
    m.press(NamedKey::Escape);
    m.type(U"gg>");
    m.ws.wake(500);
    save("operator-hints");
    m.press(NamedKey::Escape);
    m.ws.handle(Key::typed(U'p', kCtrl), 600);
    m.type(U"piano");
    save("palette");

    // Guide labels are extra drawing regions in a gutter, not changes to the application.
    // Keep them in this render so an interface change can be reviewed with its annotations.
    struct Callout
    {
        int x;
        int y;
        std::u32string_view title;
    };
    const auto cell = window->cellSize();
    const int width = window->pixelWidth();
    const int height = window->pixelHeight();
    const int gutter = 24 * cell.width;
    const int viewWidth = width - gutter;
    const int labelX = viewWidth + cell.width;
    const auto guide = [&](std::string_view name, std::initializer_list<Callout> callouts)
    {
        m.ws.resize(viewWidth, height, cell);
        auto frame = m.ws.frame();
        for (const auto& callout : callouts)
        {
            frame.push_back({.bounds = {callout.x, callout.y, labelX - callout.x, 2},
                             .background = Role::Accent,
                             .text = {},
                             .under = {},
                             .over = {}});
            frame.push_back(
                {.bounds = {labelX, callout.y - cell.height / 2, width - labelX, cell.height},
                 .background = Role::Chrome,
                 .text = {{0, 0, std::u32string{callout.title}, {Role::Text, Role::Chrome, kBold}}},
                 .under = {},
                 .over = {}});
        }
        window->draw(frame, theme);
        CHECK(window->saveFrame(utf8Text(directory / (std::string{name} + ".bmp"))));
        m.ws.resize(width, height, cell);
    };
    m.press(NamedKey::Escape);
    m.type(U"gtgg");
    save("tracks");
    m.type(U",ms");
    save("metronome");
    m.type(U"gagg");
    guide("arrangement-guide",
          {{viewWidth, cell.height / 2, U"1 Track / position"},
           {viewWidth - 8 * cell.width, 3 * cell.height, U"2 Inspector"},
           {2 * viewWidth / 3, 8 * cell.height, U"3 Section strip"},
           {viewWidth / 2, 11 * cell.height, U"4 Track rows"},
           {viewWidth - 3 * cell.width, height - 3 * cell.height / 2, U"5 Message row"},
           {viewWidth - 3 * cell.width, height - cell.height / 2, U"6 Status line"}});
    m.piano();
    guide("piano-roll-guide",
          {{viewWidth, cell.height / 2, U"1 Track / position"},
           {viewWidth - 8 * cell.width, 3 * cell.height, U"2 Inspector"},
           {viewWidth / 2, 9 * cell.height, U"7 Time ruler"},
           {viewWidth / 3, 19 * cell.height, U"8 Notes"},
           {viewWidth / 2, height - 3 * cell.height, U"9 Controller lane"},
           {viewWidth - 3 * cell.width, height - 3 * cell.height / 2, U"5 Message row"},
           {viewWidth - 3 * cell.width, height - cell.height / 2, U"6 Status line"}});

    // The swell with its draft still open, shaped as a late-blooming curve, in a half-height
    // lane.
    m.piano();
    m.type(U",ch,ch");
    m.swell();
    m.press(NamedKey::Tab);
    m.press(NamedKey::Down);
    m.press(NamedKey::Tab);
    m.type(U"40");
    m.press(NamedKey::Enter);
    save("controller-ramp");
}
