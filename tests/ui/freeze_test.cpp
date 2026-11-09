// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/freeze.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <string_view>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{
core::Song freezeSong(core::Ppqn ppqn)
{
    core::Song song{ppqn};
    for (int i = 0; i < 2; ++i)
    {
        const auto id = song.addTrack(i == 0 ? "Lead" : "Bass");
        song.updateTrack(id,
                         [&](core::Track& track)
                         {
                             track.transpose = i == 0 ? 12 : -12;
                             track.velocityOffset = 10;
                             track.timeOffset = core::TimeOffset::ticks(ppqn.quarter() / 4);
                             track.quantize.on = i == 0;
                         });
        song.updateEvents(id,
                          [&](core::EventList& events)
                          {
                              events.insert({ppqn.quarter() + ppqn.quarter() / 16,
                                             core::Note{60, 80, ppqn.quarter()}});
                          });
    }
    return song;
}

struct Freezing
{
    explicit Freezing(core::Ppqn ppqn)
        : document{freezeSong(ppqn)}
    {
        registry.add(windowCommands());
        ws.setLineHandler([&](const std::string& line) { ws.report(registry.runLine(line)); });
        ws.resize(1200, 600, {10, 20});
        ws.show(ViewKind::Tracks);
    }
    void type(std::u32string_view keys)
    {
        for (auto key : keys)
            ws.handle(Key::typed(key), 0);
    }
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }
    const core::Track& track(std::size_t i = 0) const { return *document.song().tracks()[i]; }
    std::vector<TextRun> menu() const
    {
        for (const auto& region : ws.frame())
        {
            if (!region.text.empty() && region.text.front().text == U"Freeze playback transforms")
                return region.text;
        }
        return {};
    }
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};
} // namespace

TEST_CASE("Freeze offers a list with All selected and unavailable time explained")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto original = f.document.song();
    const auto cursor = f.ws.tracks().cursor();
    f.type(U",tf");
    const auto rows = f.menu();
    REQUIRE(rows.size() == 6);
    CHECK(rows[1].text == U"All");
    CHECK(rows[1].style.background == Role::Cursor);
    CHECK(rows[2].text == U"Transpose");
    CHECK(rows[3].text == U"Velocity");
    CHECK(rows[4].text == U"Quantise");
    CHECK(rows[5].text.find(U"Freeze quantise first") != std::u32string::npos);
    CHECK(rows[5].style.foreground == Role::Dim);
    f.type(U" jd"); // unrelated keys, including transport, do nothing
    CHECK(f.ws.tracks().cursor() == cursor);
    CHECK(f.document.song() == original);
    f.press(NamedKey::Down);
    CHECK(f.menu()[2].style.background == Role::Cursor);
    f.press(NamedKey::Up);
    CHECK(f.menu()[1].style.background == Role::Cursor);
    f.press(NamedKey::Escape);
    CHECK(f.menu().empty());
    CHECK(f.document.song() == original);
    CHECK_FALSE(f.document.canUndo());
}

TEST_CASE("Freeze All applies and resets settings as one undoable edit")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto original = f.document.song();
    const auto id = f.track().id();
    f.type(U",tf");
    f.press(NamedKey::Enter);
    CHECK(f.menu().empty());
    CHECK(f.track().transpose == 0);
    CHECK(f.track().velocityOffset == 0);
    CHECK(f.track().timeOffset.amount() == 0);
    CHECK_FALSE(f.track().quantize.on);
    const auto& event = f.document.song().events(id)[0];
    CHECK(event.tick == ppqn.quarter() + ppqn.quarter() / 4);
    CHECK(std::get<core::Note>(event.data).pitch == 72);
    CHECK(std::get<core::Note>(event.data).velocity == 90);
    CHECK(f.ws.message() == "Freeze All on 1 track");
    const auto frozen = f.document.song();
    f.type(U"u");
    CHECK(f.document.song() == original);
    CHECK_FALSE(f.document.canUndo());
    f.ws.handle(Key::typed(U'r', kCtrl), 0);
    CHECK(f.document.song() == frozen);
}

TEST_CASE("Freeze can commit quantise alone and time afterwards from the list")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto id = f.track().id();
    const auto settings = f.track();
    f.type(U",tf");
    for (int i = 0; i < 3; ++i)
        f.press(NamedKey::Down);
    REQUIRE(f.menu()[4].style.background == Role::Cursor);
    f.press(NamedKey::Enter);
    CHECK_FALSE(f.track().quantize.on);
    CHECK(f.track().transpose == settings.transpose);
    CHECK(f.track().velocityOffset == settings.velocityOffset);
    CHECK(f.track().timeOffset == settings.timeOffset);
    CHECK(f.document.song().events(id)[0].tick == ppqn.quarter());
    f.type(U",tf");
    for (int i = 0; i < 4; ++i)
        f.press(NamedKey::Down);
    REQUIRE(f.menu()[5].style.background == Role::Cursor);
    f.press(NamedKey::Enter);
    CHECK(f.track().timeOffset.amount() == 0);
    CHECK(f.document.song().events(id)[0].tick == ppqn.quarter() + ppqn.quarter() / 4);
}

TEST_CASE("Freeze validates selected rows together and leaves a refused batch unchanged")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto second = f.track(1).id();
    SECTION("successful batch")
    {
        const auto original = f.document.song();
        f.type(U"Vj,tf");
        f.press(NamedKey::Enter);
        CHECK(f.track(0).transpose == 0);
        CHECK(f.track(1).transpose == 0);
        CHECK(f.ws.mode() == Mode::Normal);
        CHECK(f.ws.message() == "Freeze All on 2 tracks");
        f.type(U"u");
        CHECK(f.document.song() == original);
        CHECK_FALSE(f.document.canUndo());
    }
    SECTION("a bad pitch in the second track refuses the whole batch")
    {
        f.document.edit(
            "Too low", {}, [&](core::Song& song)
            { song.updateTrack(second, [](core::Track& track) { track.transpose = -100; }); });
        const auto original = f.document.song();
        const auto version = f.document.version();
        f.type(U"Vj,tf");
        const auto cursor = f.ws.tracks().cursor();
        const auto rows = f.menu();
        REQUIRE(rows.size() == 6);
        CHECK(rows[1].style.foreground == Role::Dim);
        CHECK(rows[2].style.foreground == Role::Dim);
        CHECK(rows[1].text.find(U"Bass") != std::u32string::npos);
        CHECK(rows[3].style.foreground != Role::Dim); // Velocity is independent of pitch
        f.press(NamedKey::Enter);
        CHECK(f.document.song() == original);
        CHECK(f.document.version() == version);
        CHECK(f.ws.tracks().cursor() == cursor);
        CHECK(f.ws.mode() == Mode::Visual);
        CHECK(f.ws.message().empty());
        CHECK(f.menu()[1].text.find(U"outside MIDI") != std::u32string::npos);
        CHECK_FALSE(f.menu().empty());
        f.press(NamedKey::Escape);
        CHECK(f.ws.mode() == Mode::Visual);
    }
}

TEST_CASE("Freeze colon choices work from track views and refuse partial event selections")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto view =
        GENERATE(ViewKind::Tracks, ViewKind::Arrangement, ViewKind::PianoRoll, ViewKind::EventList);
    f.ws.show(view);
    f.type(U":track-freeze transpose");
    f.press(NamedKey::Enter);
    CHECK(f.track().transpose == 0);
    CHECK(f.track().quantize.on);
    CHECK(f.track().velocityOffset == 10);
    CHECK(f.track().timeOffset.amount() == ppqn.quarter() / 4);
    if (view != ViewKind::Tracks)
    {
        const auto original = f.document.song();
        f.type(U"v:track-freeze velocity");
        f.press(NamedKey::Enter);
        CHECK(f.document.song() == original);
        CHECK(f.ws.message().find("whole tracks") != std::string::npos);
    }
}

TEST_CASE("Freeze refuses while playing or stopping and respects quantise bypass")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    player.update(f.document.song());
    f.ws.setPlayer(&player);
    const auto original = f.document.song();
    SECTION("playing and stopping")
    {
        REQUIRE(player.play());
        f.type(U",tf");
        CHECK(f.menu().empty());
        CHECK(f.ws.message().find("Stop playback") != std::string::npos);
        REQUIRE(player.stop());
        f.type(U":track-freeze all");
        f.press(NamedKey::Enter);
        CHECK(f.ws.message().find("Stop playback") != std::string::npos);
    }
    SECTION("bypassed quantise can still leave other transforms available")
    {
        player.setBypassQuantize(true);
        f.type(U":track-freeze quantise");
        f.press(NamedKey::Enter);
        CHECK(f.ws.message().find("bypass") != std::string::npos);
        f.type(U",tf");
        REQUIRE(f.menu().size() == 6);
        CHECK(f.menu()[1].style.foreground == Role::Dim);
        CHECK(f.menu()[4].style.foreground == Role::Dim);
        f.press(NamedKey::Escape);
    }
    CHECK(f.document.song() == original);
    f.ws.setPlayer(nullptr);
}

TEST_CASE("Freeze chooser keys can be repeated as one complete change")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    f.type(U",tf");
    f.press(NamedKey::Down); // Transpose
    f.press(NamedKey::Enter);
    CHECK(f.track().transpose == 0);
    f.type(U"j.");
    CHECK(f.track(1).transpose == 0);
    CHECK(f.track(1).velocityOffset == 10);
    CHECK(f.menu().empty());
}

TEST_CASE("Freeze rejects the conductor and strip and opens its list from the colon line")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    SECTION("the conductor is not a track to freeze")
    {
        f.type(U"gc:track-freeze all");
        f.press(NamedKey::Enter);
        CHECK(f.ws.message().find("conductor") != std::string::npos);
        CHECK(f.menu().empty());
        CHECK_FALSE(f.document.canUndo());
    }
    SECTION("the strip is not a track to freeze")
    {
        f.type(U"gags:track-freeze all");
        f.press(NamedKey::Enter);
        CHECK(f.ws.message().find("section strip") != std::string::npos);
        CHECK_FALSE(f.document.canUndo());
    }
    SECTION("the colon list remembers selected rows after the prompt closes")
    {
        f.type(U"Vj:track-freeze");
        f.press(NamedKey::Enter);
        REQUIRE_FALSE(f.menu().empty());
        f.press(NamedKey::Enter);
        CHECK(f.track(0).transpose == 0);
        CHECK(f.track(1).transpose == 0);
        CHECK(f.ws.message() == "Freeze All on 2 tracks");
    }
}

TEST_CASE("Freeze reports quantise warnings as a completed change")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto id = f.track().id();
    f.document.edit("Clashing starts", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(id,
                                          [&](core::EventList& events)
                                          {
                                              events = {};
                                              events.insert({0, core::Note{60, 100, 1}});
                                              events.insert(
                                                  {ppqn.quarter() / 16, core::Note{60, 100, 1}});
                                          });
                    });
    f.type(U":track-freeze quantise");
    f.press(NamedKey::Enter);
    CHECK_FALSE(f.track().quantize.on);
    CHECK(f.ws.messageKind() == MessageKind::Warning);
    CHECK_FALSE(f.ws.refused());
    CHECK(f.ws.message().find("notes left where stored") != std::string::npos);
}

TEST_CASE("Freeze dims choices whose results would start before zero")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const bool musical = GENERATE(false, true);
    Freezing f{ppqn};
    const auto id = f.track().id();
    f.document.edit(
        "Earlier than zero", {},
        [&](core::Song& song)
        {
            song.updateTrack(id,
                             [&](core::Track& track)
                             {
                                 track.quantize.on = false;
                                 track.timeOffset =
                                     musical ? core::TimeOffset::ticks(-ppqn.quarter() / 4)
                                             : core::TimeOffset{std::chrono::milliseconds{-40}};
                             });
            song.updateEvents(id,
                              [&](core::EventList& events)
                              {
                                  events = {};
                                  events.insert({0, core::Note{60, 80, ppqn.quarter()}});
                              });
        });
    const auto original = f.document.song();
    const auto version = f.document.version();
    f.type(U",tf");
    const auto rows = f.menu();
    REQUIRE(rows.size() == 6);
    CHECK(rows[1].style.foreground == Role::Dim);
    CHECK(rows[5].style.foreground == Role::Dim);
    CHECK(rows[1].text.find(U"before zero") != std::u32string::npos);
    CHECK(rows[5].text.find(U"before zero") != std::u32string::npos);
    CHECK(rows[2].style.foreground != Role::Dim);
    CHECK(rows[3].style.foreground != Role::Dim);
    f.press(NamedKey::Enter);
    CHECK(f.document.song() == original);
    CHECK(f.document.version() == version);
    CHECK(f.ws.message().empty());
    f.press(NamedKey::Down);
    CHECK(f.menu()[2].style.background == Role::Cursor);
    f.press(NamedKey::Up); // All remains selectable even though it is unavailable
    CHECK(f.menu()[1].style.background == Role::Cursor);
    for (int i = 0; i < 4; ++i)
        f.press(NamedKey::Down);
    REQUIRE(f.menu()[5].style.background == Role::Cursor);
    f.press(NamedKey::Enter);
    CHECK(f.ws.message().empty());
    CHECK(f.document.song() == original);
    CHECK(f.document.version() == version);
    f.press(NamedKey::Escape);
    f.type(U":track-freeze all");
    f.press(NamedKey::Enter);
    CHECK(f.ws.message().find("before zero") != std::string::npos);
    CHECK(f.document.song() == original);
    CHECK(f.document.version() == version);
}

TEST_CASE("Freeze dims transforms with no active playback setting")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto id = f.track().id();
    f.document.edit("No transforms", {},
                    [&](core::Song& song)
                    {
                        song.updateTrack(id,
                                         [](core::Track& track)
                                         {
                                             track.transpose = 0;
                                             track.velocityOffset = 0;
                                             track.quantize.on = false;
                                             track.timeOffset = core::TimeOffset::ticks(0);
                                         });
                    });
    const auto original = f.document.song();
    const auto version = f.document.version();
    const auto cursor = f.ws.tracks().cursor();
    f.type(U",tf");
    const auto rows = f.menu();
    REQUIRE(rows.size() == 6);
    for (std::size_t i = 1; i < rows.size(); ++i)
    {
        CHECK(rows[i].style.foreground == Role::Dim);
        CHECK(rows[i].text.find(U"Nothing to freeze") != std::u32string::npos);
    }
    for (std::size_t i = 2; i < rows.size(); ++i)
    {
        f.press(NamedKey::Down);
        CHECK(f.menu()[i].style.background == Role::Cursor);
        f.press(NamedKey::Enter);
        CHECK(f.ws.message().empty());
        CHECK(f.document.version() == version);
        REQUIRE_FALSE(f.menu().empty());
    }
    f.press(NamedKey::Down); // stop at the last entry
    CHECK(f.menu()[5].style.background == Role::Cursor);
    for (std::size_t i = 4; i > 0; --i)
    {
        f.press(NamedKey::Up);
        CHECK(f.menu()[i].style.background == Role::Cursor);
    }
    f.press(NamedKey::Enter);
    CHECK(f.ws.message().empty());
    CHECK(f.document.song() == original);
    CHECK(f.document.version() == version);
    CHECK(f.ws.tracks().cursor() == cursor);
    f.press(NamedKey::Escape);
    f.type(U":track-freeze time");
    f.press(NamedKey::Enter);
    CHECK(f.ws.message() == "Nothing to freeze");
    CHECK(f.document.song() == original);
}

TEST_CASE("Freeze leaves a choice available if any selected row has work")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto first = f.track().id();
    f.document.edit(
        "One velocity offset", {}, [&](core::Song& song)
        { song.updateTrack(first, [](core::Track& track) { track.velocityOffset = 0; }); });
    const auto original = f.document.song();
    f.type(U"Vj,tf");
    REQUIRE(f.menu().size() == 6);
    CHECK(f.menu()[3].style.foreground != Role::Dim);
    f.press(NamedKey::Down);
    f.press(NamedKey::Down);
    f.press(NamedKey::Enter);
    CHECK(f.track().velocityOffset == 0);
    CHECK(f.track(1).velocityOffset == 0);
    CHECK(std::get<core::Note>(f.document.song().events(first)[0].data).velocity == 80);
    const auto second = f.track(1).id();
    CHECK(std::get<core::Note>(f.document.song().events(second)[0].data).velocity == 90);
    CHECK(f.ws.message() == "Freeze Velocity on 2 tracks");
    f.type(U"u");
    CHECK(f.document.song() == original);
}

TEST_CASE("Freeze availability stays current and confirmation rechecks the operation")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    player.update(f.document.song());
    f.ws.setPlayer(&player);
    f.type(U",tf");
    REQUIRE(f.menu().size() == 6);
    REQUIRE(f.menu()[1].style.foreground != Role::Dim);
    SECTION("the song changes after the list was drawn")
    {
        const auto id = f.track().id();
        f.document.edit(
            "Out of range", {}, [&](core::Song& song)
            { song.updateTrack(id, [](core::Track& track) { track.transpose = 100; }); });
        const auto original = f.document.song();
        const auto version = f.document.version();
        f.press(NamedKey::Enter); // no redraw between the edit and confirmation
        CHECK(f.document.song() == original);
        CHECK(f.document.version() == version);
        CHECK(f.ws.message().empty());
        CHECK(f.menu()[1].text.find(U"outside MIDI") != std::u32string::npos);
        CHECK(f.menu()[1].style.foreground == Role::Dim);
        REQUIRE(f.document.undo());
        CHECK(f.menu()[1].style.foreground != Role::Dim);
    }
    SECTION("bypass changes after the list was drawn")
    {
        player.setBypassQuantize(true);
        CHECK(f.menu()[1].style.foreground == Role::Dim);
        CHECK(f.menu()[4].style.foreground == Role::Dim);
        player.setBypassQuantize(false);
        CHECK(f.menu()[1].style.foreground != Role::Dim);
    }
    SECTION("playback starts after the list was drawn")
    {
        const auto original = f.document.song();
        REQUIRE(player.play());
        CHECK(f.menu()[1].style.foreground == Role::Dim);
        f.press(NamedKey::Enter);
        CHECK(f.document.song() == original);
        CHECK(f.ws.message().empty());
        CHECK(f.menu()[1].text.find(U"Stop playback") != std::u32string::npos);
    }
    f.press(NamedKey::Escape);
    f.ws.setPlayer(nullptr);
}

TEST_CASE("Freeze ignores Enter on a dimmed choice without a message or change")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Freezing f{ppqn};
    const auto original = f.document.song();
    const auto version = f.document.version();
    const auto cursor = f.ws.tracks().cursor();
    f.type(U",tf");
    for (int i = 0; i < 4; ++i)
        f.press(NamedKey::Down);
    const auto rows = f.menu();
    REQUIRE(rows.size() == 6);
    CHECK(rows[5].style.background == Role::Cursor);
    CHECK(rows[5].style.foreground == Role::Dim);
    f.press(NamedKey::Enter);
    CHECK(f.ws.message().empty());
    CHECK_FALSE(f.ws.refused());
    const auto status = f.ws.frame().back();
    CHECK(std::ranges::none_of(status.text, [](const TextRun& run)
                               { return run.style.foreground == Role::Warning; }));
    f.press(NamedKey::Enter);
    CHECK(f.ws.message().empty());
    CHECK(f.document.song() == original);
    CHECK(f.document.version() == version);
    CHECK(f.ws.tracks().cursor() == cursor);
    CHECK_FALSE(f.document.canUndo());
    REQUIRE_FALSE(f.menu().empty());
    CHECK(f.menu()[5].style.background == Role::Cursor);
    f.press(NamedKey::Up);
    CHECK(f.menu()[4].style.background == Role::Cursor);
    f.press(NamedKey::Escape);
    CHECK(f.menu().empty());
    CHECK(f.document.song() == original);
}
