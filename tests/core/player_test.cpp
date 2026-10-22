#include "support/edit_track.hpp"
#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/player.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;

namespace
{

// Four quarter notes, 60, 62, 64 and 65, on a track with a port name.
Song fourNotes(Ppqn ppqn, std::string port = "")
{
    Song song = songAt(ppqn);
    const auto id =
        addTrack(song, 1,
                 [&](EditTrack& track)
                 {
                     Tick tick = 0;
                     for (const int pitch : {60, 62, 64, 65})
                     {
                         track.events.insert(
                             {tick, Note{static_cast<std::uint8_t>(pitch), 100, ppqn.quarter()}});
                         tick += ppqn.quarter();
                     }
                 });
    song.updateTrack(id, [&](Track& track) { track.port = std::move(port); });
    return song;
}

// A player whose ports are "a" (destination 1), "b" (2) and the default (1).
struct Playing
{
    explicit Playing(const Song& song) { player.update(song); }

    // Lets the engine run for a while, then polls the player as the editor would.
    bool runFor(std::chrono::nanoseconds time)
    {
        runUntil(engine, clock, clock.time + time);
        return player.poll();
    }

    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::vector<std::string> lookedUp;
    Player player{engine, [this](std::string_view port) -> std::optional<DestinationId>
                  {
                      lookedUp.emplace_back(port);
                      if (port.empty() || port == "a")
                      {
                          return DestinationId{1};
                      }
                      if (port == "b")
                      {
                          return DestinationId{2};
                      }
                      return std::nullopt;
                  }};
};

} // namespace

TEST_CASE("the player plays the song's tracks to their ports, naming those it can't find")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn, "b");
    addTrack(song, 2, [&](EditTrack& track) { track.events.insert({0, Note{40, 90, 1}}); });
    song.updateTrack(song.tracks()[1]->id(), [](Track& track) { track.port = "gone"; });
    addTrack(song, 3, [&](EditTrack& track) { track.events.insert({0, Note{41, 90, 1}}); });
    song.updateTrack(song.tracks()[2]->id(), [](Track& track) { track.port = "gone"; });

    Playing p{song};
    CHECK(p.player.missingPorts() == std::vector<std::string>{"gone"});
    p.player.play();
    p.runFor(1ms);
    REQUIRE_FALSE(p.sink.sent.empty());
    CHECK(p.sink.sent.front().destination == DestinationId{2});
    for (const auto& sent : p.sink.sent)
    {
        CHECK(sent.destination == DestinationId{2}); // the tracks with no port aren't played
    }

    // Each name is looked up once, until the player is told ports may have changed.
    p.player.update(song);
    CHECK(p.lookedUp == std::vector<std::string>{"b", "gone"});
    p.player.forgetPorts();
    p.player.update(song);
    CHECK(p.lookedUp.size() == 4);
}

TEST_CASE("playback starts from the marker, which stays put with rewind")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Playing p{fourNotes(ppqn)};
    const Tick q = ppqn.quarter();
    CHECK(p.player.marker() == 0);
    CHECK(p.player.rewind());

    p.player.setMarker(q);
    p.player.toggle();
    CHECK(p.player.playing());
    p.runFor(750ms); // a quarter and a half on
    CHECK(p.player.position() == q + q + q / 2);
    p.player.toggle();
    CHECK_FALSE(p.player.playing());
    CHECK(p.player.position() == std::nullopt);
    p.runFor(1ms);
    CHECK_FALSE(p.player.stopping());
    CHECK(p.player.marker() == q);
    p.player.toggle();
    p.runFor(1ms);
    CHECK(p.player.position() >= q);
    CHECK(p.player.position() < q + q / 10);
}

TEST_CASE("without rewind, the marker moves to where playback stops")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Playing p{fourNotes(ppqn)};
    const Tick q = ppqn.quarter();
    p.player.setRewind(false);
    p.player.setMarker(q);
    p.player.play();
    p.runFor(750ms);
    p.player.stop();
    CHECK(p.player.marker() == q + q + q / 2); // where it was last seen, until...
    CHECK(p.player.stopping());
    p.runFor(1ms);
    CHECK_FALSE(p.player.stopping());
    const Tick stopped = p.player.marker(); // ...where the engine says it stopped
    CHECK(stopped == p.engine.stoppedAt(p.engine.lastPlay()));
    CHECK(stopped >= q + q + q / 2);
}

TEST_CASE("playback stops by itself at the song's end")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Playing p{fourNotes(ppqn)};
    const Tick q = ppqn.quarter();
    p.player.setMarker(q);

    SECTION("with rewind, the marker stays put")
    {
        p.player.play();
        CHECK_FALSE(p.runFor(1s));
        CHECK(p.player.playing());
        CHECK(p.runFor(2s));
        CHECK_FALSE(p.player.playing());
        CHECK_FALSE(p.runFor(1s));
        CHECK(p.player.marker() == q);
    }
    SECTION("without, it goes back to the start, since resuming at the end would play nothing")
    {
        p.player.setRewind(false);
        p.player.play();
        CHECK(p.runFor(3s));
        CHECK_FALSE(p.runFor(1s));
        CHECK(p.player.marker() == 0);
    }
}

TEST_CASE("playing while playing starts again from the marker")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Playing p{fourNotes(ppqn)};
    p.player.play();
    p.runFor(600ms);
    p.player.setMarker(3 * ppqn.quarter());
    p.player.play();
    CHECK(p.player.playing());
    p.runFor(1ms);
    const auto position = p.player.position(); // a millisecond or so on
    REQUIRE(position);
    CHECK(*position >= 3 * ppqn.quarter());
    CHECK(*position < 3 * ppqn.quarter() + ppqn.quarter() / 10);
}

TEST_CASE("panic stops playback and resets every channel")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Playing p{fourNotes(ppqn)};
    p.player.play();
    p.runFor(100ms);
    p.player.panic();
    CHECK_FALSE(p.player.playing());
    p.sink.sent.clear();
    p.runFor(1ms);
    CHECK(p.sink.sent.size() >= 32); // All Notes Off and Reset All Controllers on 16 channels
}

TEST_CASE("an edit during playback carries on playing the new song")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn);
    Playing p{song};
    p.player.play();
    p.runFor(100ms);
    song.updateEvents(song.tracks()[0]->id(), [&](EventList& events)
                      { events.insert({2 * ppqn.quarter() + 1, Note{72, 100, 1}}); });
    p.player.update(song);
    p.sink.sent.clear();
    p.runFor(1500ms);
    CHECK(p.player.playing());
    const auto messages = p.sink.messages();
    CHECK(std::ranges::find(messages, std::vector<std::uint8_t>{0x90, 72, 100}) != messages.end());
}
