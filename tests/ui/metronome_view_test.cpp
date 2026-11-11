// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

using Setting = MetronomeView::Setting;

// The metronome page, with a player to hear the click through.
struct Clicking
{
    Clicking()
    {
        registry.add(windowCommands());
        ws.resize(1200, 600, {10, 20});
        player.update(document.song());
        ws.setPlayer(&player);
        type(U",ms");
    }
    void type(std::u32string_view keys)
    {
        for (auto c : keys)
            ws.handle(Key::typed(c), 0);
    }
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }
    // Enter, what's typed, then Enter.
    void enter(std::u32string_view typed)
    {
        press(NamedKey::Enter);
        type(typed);
        press(NamedKey::Enter);
    }
    std::u32string text() const
    {
        std::u32string all;
        for (const auto& region : ws.frame())
            for (const auto& run : region.text)
                all += run.text + U"|";
        return all;
    }
    const core::MetronomeSettings& metronome() const { return document.song().metronome; }
    const core::CountInSettings& countIn() const { return document.song().countIn; }

    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the leader then m and s, or :metronome, shows every metronome and count-in setting")
{
    Clicking c;
    CHECK(c.ws.shown() == ViewKind::Metronome);
    CHECK(c.ws.where() == Where::MetronomePage);
    const auto text = c.text();
    for (const auto* part :
         {U"Metronome|", U"Before recording|", U"Plays|", U"While recording", U"Port|", U"Default",
          U"Channel|", U"Downbeat|", U"E5", U"Hi Wood Block", U"110", U"Beat|", U"Low Wood Block",
          U"Length|", U"1/32", U"Before|", U"Count-in", U"1 bar", U"Pre-roll|"})
    {
        CAPTURE(encodeUtf8(part));
        CHECK(contains(text, part));
    }
    CHECK(contains(text, U"When the metronome clicks")); // the inspector says what it is
    c.type(U"ga");
    c.registry.runLine("metronome");
    CHECK(c.ws.shown() == ViewKind::Metronome);
}

TEST_CASE("on the metronome page, Enter edits a setting, Up and Down step it, each an undo step")
{
    Clicking c;
    c.type(U"jj");
    REQUIRE(c.ws.metronomeView().setting() == Setting::Channel);
    c.enter(U"9");
    CHECK(c.metronome().channel == 9);
    CHECK(c.ws.message() == "Click channel 9");
    c.press(NamedKey::Enter);
    c.press(NamedKey::Up);
    c.press(NamedKey::Enter);
    CHECK(c.metronome().channel == 10);
    c.enter(U"17");
    CHECK(c.ws.message() == "The channel is from 1 to 16");
    c.press(NamedKey::Escape);
    CHECK(c.metronome().channel == 10);
    c.type(U"u");
    CHECK(c.metronome().channel == 9);

    // The choosers step round, and take the start of a word.
    c.type(U"gg");
    c.press(NamedKey::Enter);
    c.press(NamedKey::Up);
    c.press(NamedKey::Enter);
    CHECK(c.metronome().when == core::MetronomeSettings::When::Always);
    c.enter(U"of");
    CHECK(c.metronome().when == core::MetronomeSettings::When::Off);

    // What comes before recording.
    c.type(U"G");
    REQUIRE(c.ws.metronomeView().setting() == Setting::PreRoll);
    c.enter(U"2");
    CHECK(c.countIn().preRollBars == 2);
    c.type(U"k");
    c.enter(U"3 beats");
    CHECK(c.countIn().beats == 3);
    CHECK(contains(c.text(), U"3 beats"));
    c.type(U"k");
    c.enter(U"pre");
    CHECK(c.countIn().mode == core::CountInSettings::Mode::PreRoll);
    CHECK(c.ws.message() == "Pre-roll 2 bars");
}

TEST_CASE("changing a click's note or velocity on the metronome page plays it")
{
    Clicking c;
    c.type(U"3j");
    REQUIRE(c.ws.metronomeView().setting() == Setting::Downbeat);
    c.press(NamedKey::Enter);
    c.press(NamedKey::Up);
    c.engine.step();
    CHECK(c.metronome().downbeatNote == 77);
    CHECK(contains(c.text(), U"Low Wood Block"));
    REQUIRE_FALSE(c.sink.sent.empty());
    CHECK(c.sink.sent.back().bytes == std::vector<std::uint8_t>{0x99, 77, 110});
    c.press(NamedKey::Enter);

    // Its velocity, beside it.
    c.type(U"l");
    CHECK(c.ws.metronomeView().field() == MetronomeView::Field::Velocity);
    c.enter(U"90");
    c.engine.step();
    CHECK(c.metronome().downbeatVelocity == 90);
    CHECK(c.sink.sent.back().bytes == std::vector<std::uint8_t>{0x99, 77, 90});
    // The beat's note, by name.
    c.type(U"j");
    c.type(U"h");
    c.enter(U"C2");
    CHECK(c.metronome().beatNote == 36);
    CHECK(contains(c.text(), U"Bass Drum 1"));
}

TEST_CASE("the click's port is chosen from the enabled outputs, and warned of with nowhere to go")
{
    Clicking c;
    PortTable table;
    table.detected(PortDirection::Output, std::vector<std::string>{"Synth", "Drums"});
    table.setEnabled(PortDirection::Output, "Synth", true);
    table.setEnabled(PortDirection::Output, "Drums", true);
    table.setAlias(PortDirection::Output, "Drums", "Kit");
    c.ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    c.type(U"ga,mp");
    CHECK(c.ws.shown() == ViewKind::Metronome);
    REQUIRE(c.ws.metronomeView().setting() == Setting::Port);
    CHECK(contains(c.text(), U"No default output"));
    c.enter(U"ki");
    CHECK(c.metronome().port == "Kit");
    CHECK(c.ws.message() == "Click port: Kit");
    CHECK_FALSE(contains(c.text(), U"No default output"));
    c.press(NamedKey::Enter);
    c.press(NamedKey::Up);
    c.press(NamedKey::Enter);
    CHECK(c.metronome().port.empty()); // round to Default
    c.enter(U"Nowhere");
    CHECK(c.ws.message() == "No enabled output is called Nowhere");
}
