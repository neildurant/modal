#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/window_commands.hpp>

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
        view.resize(80 * kCell.width, 24 * kCell.height, kCell);
        player.update(document.song());
        view.setPlayer(&player);
    }

    void press(const Key& key) { view.handle(key, now); }
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
        now = view.deadline().value_or(now);
        view.wake(now);
    }

    std::u32string status() const
    {
        std::u32string text;
        const auto frame = view.frame();
        for (const auto& run : frame.back().text)
        {
            text += run.text;
        }
        return text;
    }

    core::Tick tickOf(std::size_t row) const { return view.list().rows()[row].tick; }

    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    core::Document document{demoSong()};
    CommandRegistry registry;
    EventListView view{registry, document};
    std::int64_t now = 100;
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("Space plays and stops; with rewind, the cursor stays and the view follows")
{
    Playing p;
    REQUIRE(p.view.rewind());
    p.type(U"5j");
    CHECK(p.view.deadline() == std::nullopt);
    CHECK(contains(p.status(), U"Rewind on"));

    p.type(U" ");
    CHECK(p.player.playing());
    CHECK(p.player.started() == 0);
    CHECK(p.view.playheadRow() == 3); // the last of the rows at the start: section, meter, tempo
    CHECK(p.view.deadline() == p.now + EventListView::kFrameInterval);

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

    // Stopped, back to the cursor; Space again plays from where playback started.
    p.type(U" ");
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.top() <= 5);
    CHECK_FALSE(contains(p.status(), U"▶"));
    p.runFor(1ms);
    CHECK(p.view.deadline() == std::nullopt); // no more frames
    p.type(U" ");
    CHECK(p.player.started() == 0);
}

TEST_CASE("without rewind, the cursor follows the playhead and Space resumes where it stopped")
{
    Playing p;
    p.view.setRewind(false);
    CHECK(contains(p.status(), U"Rewind off"));
    p.type(U" ");
    p.runFor(10s);
    const auto row = *p.view.playheadRow();
    CHECK(p.view.row() == row);
    CHECK(row == p.view.top() + static_cast<std::size_t>(p.view.viewRows()) / 3); // as with rewind

    p.type(U" ");
    p.runFor(1ms); // the engine says where it stopped
    CHECK(p.view.row() == row);
    CHECK(p.player.stopped() >= p.tickOf(row));
    p.type(U" ");
    CHECK(p.player.started() == p.player.stopped());
}

TEST_CASE("Ctrl-Space or Ctrl-Enter plays from the cursor, whatever rewind says")
{
    Playing p;
    p.type(U"9j");
    p.press(Key::of(NamedKey::Enter, kCtrl));
    CHECK(p.player.playing());
    CHECK(p.player.started() == p.tickOf(9));
    CHECK(p.view.playheadRow() == 9);
    p.type(U"j");
    p.press(Key::typed(U' ', kCtrl));
    CHECK(p.player.started() == p.tickOf(10));
}

TEST_CASE("near the list's end, the playhead goes on down the view")
{
    Playing p;
    p.type(U"G5k");
    const auto end = p.view.row() + 5;
    p.press(Key::of(NamedKey::Enter, kCtrl));
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
    p.press(Key::of(NamedKey::Enter, kCtrl));
    p.runFor(30s);
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.row() == end - 1);
}

TEST_CASE("the leader then t and Space turns rewind on and off")
{
    Playing p;
    p.type(U",t ");
    CHECK_FALSE(p.view.rewind());
    CHECK(p.view.message() == "Rewind off: Space resumes where playback stopped");
    CHECK_FALSE(p.player.playing());
    p.type(U",t ");
    CHECK(p.view.rewind());
}

TEST_CASE("Ctrl-. panics, at the prompt too")
{
    Playing p;
    p.type(U" ");
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());

    p.type(U" :");
    REQUIRE(p.view.mode() == Mode::CommandLine);
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.mode() == Mode::CommandLine);
    CHECK(p.view.commandLine().text().empty()); // not typed
}

TEST_CASE("without a player, the transport says there's nothing to play to")
{
    Playing p;
    p.view.setPlayer(nullptr);
    p.type(U" ");
    CHECK(p.view.message() == "Nothing to play to");
}
