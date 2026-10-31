// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace modal;
using namespace modal::ui;
using namespace std::chrono_literals;

namespace
{

constexpr CellSize kCell{10, 20};

// The demo song's event list, playing through an engine on a clock that stands still until
// moved, every port going to one destination.
struct Playing
{
    Playing()
    {
        registry.add(windowCommands());
        ws.resize(80 * kCell.width, 24 * kCell.height, kCell);
        player.update(document.song());
        ws.setPlayer(&player);
    }

    void press(const Key& key) { ws.handle(key, now); }
    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            press(Key::typed(key));
        }
    }

    // Lets the engine play for a while, then wakes the view at its deadline, as the window
    // would to draw a frame.
    void runFor(std::chrono::nanoseconds time)
    {
        test::runUntil(engine, clock, clock.time + time);
        now = ws.deadline().value_or(now);
        ws.wake(now);
    }

    std::u32string status() const
    {
        std::u32string text;
        const auto frame = ws.frame();
        for (const auto& run : frame.back().text)
        {
            text += run.text;
        }
        return text;
    }

    core::Tick tickOf(std::size_t row) const { return view.list().rows()[row].tick; }

    // The row the marker shows on, as a run in the list's margin, if it's in view.
    std::optional<std::size_t> markerRow() const
    {
        const auto frame = ws.frame();
        for (const auto& run : frame.front().text)
        {
            if (run.x == 0 && run.text.starts_with(U"▶"))
            {
                // The first line is the headings.
                return view.top() + static_cast<std::size_t>(run.y / kCell.height) - 1;
            }
        }
        return std::nullopt;
    }

    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
    EventListView& view = ws.eventList();
    std::int64_t now = 100;
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("Space plays from the marker and stops where it is; with rewind, the marker stays")
{
    Playing p;
    REQUIRE(p.ws.rewind());
    p.type(U"5j");
    CHECK(p.ws.deadline() == std::nullopt);
    CHECK(contains(p.status(), U"Rewind on"));

    CHECK(contains(p.status(), U"From 1:1:000"));
    CHECK(p.markerRow() == 3); // the last of the rows at the start: section, meter, tempo
    p.type(U" ");
    CHECK(p.player.playing());
    CHECK(p.view.playheadRow() == 3); // the last of the rows at the start: section, meter, tempo
    CHECK(p.ws.deadline() == p.now + Workspace::kFrameInterval);

    p.runFor(1s);
    REQUIRE(p.view.playheadRow());
    const auto early = *p.view.playheadRow();
    CHECK(early > 3);
    CHECK(p.view.row() == 5); // the cursor stays
    CHECK(contains(p.status(), U"▶"));

    // Once the playhead is a third of the way down, the list scrolls under it.
    const auto third = static_cast<std::size_t>(p.view.viewRows()) / 3;
    p.runFor(10s);
    const auto row = *p.view.playheadRow();
    CHECK(row > early);
    CHECK(row == p.view.top() + third);
    CHECK(p.view.row() == 5);

    // Stopped where it is, the cursor landing there, and the marker where it was.
    p.type(U" ");
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.row() == row);
    CHECK(p.view.top() > 5);
    CHECK_FALSE(contains(p.status(), U"▶"));
    p.runFor(1ms);
    CHECK(p.ws.deadline() == std::nullopt); // no more frames
    CHECK(p.player.marker() == 0);
    p.type(U"gg");
    CHECK(p.markerRow() == 3);
}

TEST_CASE("without rewind, the cursor follows the playhead, and the marker moves where it stops")
{
    Playing p;
    p.ws.setRewind(false);
    CHECK(contains(p.status(), U"Rewind off"));
    p.type(U" ");
    p.runFor(10s);
    const auto row = *p.view.playheadRow();
    CHECK(p.view.row() == row);
    CHECK(row == p.view.top() + static_cast<std::size_t>(p.view.viewRows()) / 3); // as with rewind

    p.type(U" ");
    p.runFor(1ms); // the engine says where it stopped
    CHECK_FALSE(p.player.stopping());
    CHECK(p.ws.deadline() == std::nullopt);
    CHECK(p.view.row() == row);
    CHECK(p.player.marker() >= p.tickOf(row));
    CHECK(p.markerRow() == row);
}

TEST_CASE("m Space puts the marker at the cursor, and Ctrl-Space plays from there too")
{
    Playing p;
    p.type(U"9jm ");
    CHECK_FALSE(p.player.playing());
    CHECK(p.player.marker() == p.tickOf(9));
    CHECK(p.markerRow() == 9);
    p.type(U"j");
    p.press(Key::typed(U' ', kCtrl));
    CHECK(p.player.playing());
    CHECK(p.player.marker() == p.tickOf(10));
    CHECK(p.view.playheadRow() == 10);
}

TEST_CASE("moving the cursor while playing lets go of the playhead, until zf or playing again")
{
    Playing p;
    p.type(U" ");
    p.runFor(11s);
    REQUIRE(p.view.top() > 0); // scrolled with the playhead
    p.type(U"gg");
    CHECK(p.view.top() == 0);
    p.runFor(2s);
    CHECK(p.view.top() == 0); // staying with the cursor
    p.type(U"zf");
    CHECK(p.view.top() > 0);

    // Stopping goes back to the cursor, even without rewind, once moved away.
    p.type(U"gg");
    p.ws.setRewind(false);
    p.type(U" ");
    p.runFor(1ms);
    CHECK(p.view.row() == 0);

    // Playing again follows again.
    p.type(U" ");
    p.runFor(11s);
    CHECK(p.view.top() > 0);
}

TEST_CASE("the piano roll shows the play-from marker in its ruler")
{
    Playing p;
    p.type(U"gr}m ");
    bool shown = false;
    const auto frame = p.ws.frame();
    for (const auto& region : frame)
    {
        for (const auto& run : region.text)
        {
            shown = shown || (run.text == U"\u25B6" && region.bounds.y > 0);
        }
    }
    CHECK(shown);
}

TEST_CASE("near the list's end, the playhead goes on down the view")
{
    Playing p;
    p.type(U"G5k");
    const auto end = p.view.row() + 5;
    p.press(Key::typed(U' ', kCtrl));
    p.runFor(1ms);
    const auto rows = static_cast<std::size_t>(p.view.viewRows());
    CHECK(p.view.top() == end + 1 - rows); // the last row at the bottom
    CHECK(*p.view.playheadRow() > p.view.top() + rows / 3);
}

TEST_CASE("playback stopping by itself at the song's end goes back to the cursor")
{
    Playing p;
    p.type(U"G");
    const auto end = p.view.row();
    p.type(U"k");
    p.press(Key::typed(U' ', kCtrl));
    p.runFor(30s);
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.row() == end - 1);
}

TEST_CASE("the leader then t and Space turns rewind on and off")
{
    Playing p;
    p.type(U",t ");
    CHECK_FALSE(p.ws.rewind());
    CHECK(p.ws.message() == "Rewind off: the play-from marker moves to where playback stops");
    CHECK_FALSE(p.player.rewind());
    CHECK_FALSE(p.player.playing());
    p.type(U",t ");
    CHECK(p.ws.rewind());
}

TEST_CASE("Ctrl-. panics, at the prompt too")
{
    Playing p;
    p.type(U" ");
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());

    p.type(U" :");
    REQUIRE(p.ws.mode() == Mode::CommandLine);
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());
    CHECK(p.ws.mode() == Mode::CommandLine);
    CHECK(p.ws.commandLine().text().empty()); // not typed
}

TEST_CASE("without a player, the transport says there's nothing to play to")
{
    Playing p;
    p.ws.setPlayer(nullptr);
    p.type(U" ");
    CHECK(p.ws.message() == "Nothing to play to");
}

TEST_CASE("Shift-Space stops and goes back to where the cursor was when playback started")
{
    Playing p;
    p.type(U"5j ");
    p.runFor(11s);
    REQUIRE(p.view.top() > 5);
    p.press(Key::typed(U' ', kShift));
    CHECK_FALSE(p.player.playing());
    p.runFor(1ms);
    CHECK(p.view.row() == 5);
    CHECK(p.view.top() <= 5);
    CHECK(p.player.marker() == 0); // with rewind, where it was

    // Without rewind too, the marker moving to where it stopped.
    p.ws.setRewind(false);
    p.type(U" ");
    p.runFor(11s);
    p.press(Key::typed(U' ', kShift));
    p.runFor(1ms);
    CHECK(p.view.row() == 5);
    CHECK(p.player.marker() > 0);

    // Stopped, it plays, as Space does.
    p.press(Key::typed(U' ', kShift));
    CHECK(p.player.playing());
}
