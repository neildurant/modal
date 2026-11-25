// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/edit_track.hpp"
#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/player.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
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
    CHECK(p.player.play(song));
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

TEST_CASE("recording retains each input's tempo mapping across live edits and delayed collection")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto offsetKind = GENERATE(0, 1, 2, 3, 4);
    const auto offset = offsetKind == 1   ? TimeOffset::ticks(q / 4)
                        : offsetKind == 2 ? TimeOffset::ticks(-q / 4)
                        : offsetKind == 3 ? TimeOffset{125ms}
                        : offsetKind == 4 ? TimeOffset{-125ms}
                                          : TimeOffset{};
    auto song = songAt(ppqn);
    const auto id = song.addTrack("Recorded");
    song.updateTrack(id,
                     [&](Track& track)
                     {
                         track.arm = true;
                         track.timeOffset = offset;
                     });
    song.countIn.mode = CountInSettings::Mode::None;
    Playing p{song};
    REQUIRE(p.player.record(song));
    p.engine.step();
    const auto leadIn = compilePlan(song, byPortName).leadIn;
    p.clock.time += 1s;
    // Queued before the edit: it must be stamped against the old mapping.
    const auto oldTick = offset.undo(song.tempo(), p.clock.time - kStart - leadIn);
    REQUIRE(p.engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0xB0, 11, 80}, p.clock.time));
    REQUIRE(p.engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0x90, 88, 90}, p.clock.time));
    song.updateTempo([](TempoMap& tempo) { tempo.set(0, Tempo{1'000'000}); });
    p.player.update(song);
    p.engine.step();

    p.clock.time += 500ms;
    p.engine.step();
    REQUIRE(p.engine.status().position);
    const auto heard = song.tempo().toTime(*p.engine.status().position);
    const auto start = offset.undo(song.tempo(), heard);
    REQUIRE(p.engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0x80, 88, 64}, p.clock.time));
    REQUIRE(p.engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0x90, 99, 100}, p.clock.time));
    p.engine.step();
    p.clock.time += 500ms;
    REQUIRE(p.engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0x80, 99, 70}, p.clock.time));
    p.engine.step();
    // Replace and reclaim the plans before collecting any input. Its mapping must survive.
    for (int i = 0; i < 4; ++i)
    {
        p.player.update(song);
        p.engine.step();
    }
    p.clock.time += 500ms;
    REQUIRE(p.player.stop());
    p.engine.step();
    p.player.poll();
    const auto take = p.player.finishRecording(song);
    REQUIRE(take);
    const auto events = take->song.events(id).all();
    REQUIRE(events.size() == 3);
    CHECK(events[0] == Event{oldTick, ControlChange{11, 80}});
    CHECK(events[1] == Event{oldTick, Note{88, 90, start - oldTick, 64}});
    CHECK(events[2] == Event{start, Note{99, 100, q / 2, 70}});
}

TEST_CASE("the metronome set to always clicks in plain playback, and set to while recording "
          "doesn't")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn, "a");
    song.metronome.port = "b"; // destination 2, apart from the track's
    const auto clicks = [](const Playing& p)
    {
        return std::ranges::count_if(p.sink.sent,
                                     [](const auto& sent)
                                     {
                                         return sent.destination == DestinationId{2} &&
                                                (sent.bytes[0] & 0xF0) == 0x90 && sent.bytes[2] > 0;
                                     });
    };

    song.metronome.when = MetronomeSettings::When::Always;
    Playing always{song};
    REQUIRE(always.player.play(song));
    always.runFor(1100ms); // three beats at 120 BPM
    CHECK(clicks(always) == 3);

    song.metronome.when = MetronomeSettings::When::Recording;
    Playing recording{song};
    REQUIRE(recording.player.play(song));
    recording.runFor(1100ms);
    CHECK(clicks(recording) == 0);
}

TEST_CASE("playback starts from the marker, which stays put with rewind")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    const Tick q = ppqn.quarter();
    CHECK(p.player.marker() == 0);
    CHECK(p.player.rewind());

    p.player.setMarker(q);
    CHECK(p.player.toggle(song));
    CHECK(p.player.playing());
    p.runFor(750ms); // a quarter and a half on
    CHECK(p.player.position() == q + q + q / 2);
    CHECK(p.player.toggle(song));
    CHECK_FALSE(p.player.playing());
    CHECK(p.player.position() == std::nullopt);
    p.runFor(1ms);
    CHECK_FALSE(p.player.stopping());
    CHECK(p.player.marker() == q);
    CHECK(p.player.toggle(song));
    p.runFor(1ms);
    CHECK(p.player.position() >= q);
    CHECK(p.player.position() < q + q / 10);
}

TEST_CASE("without rewind, the marker moves to where playback stops")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    const Tick q = ppqn.quarter();
    p.player.setRewind(false);
    p.player.setMarker(q);
    CHECK(p.player.play(song));
    p.runFor(750ms);
    CHECK(p.player.stop());
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
    const auto song = fourNotes(ppqn);
    Playing p{song};
    const Tick q = ppqn.quarter();
    p.player.setMarker(q);

    SECTION("with rewind, the marker stays put")
    {
        CHECK(p.player.play(song));
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
        CHECK(p.player.play(song));
        CHECK(p.runFor(3s));
        CHECK_FALSE(p.runFor(1s));
        CHECK(p.player.marker() == 0);
    }
}

TEST_CASE("playback uses its own count-in once, including at the start of a loop")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto song = fourNotes(ppqn);
    song.metronome.port = "b";
    song.metronome.when = MetronomeSettings::When::Off;
    song.countIn.mode = CountInSettings::Mode::PreRoll;
    song.playbackLeadIn = {.mode = CountInSettings::Mode::CountIn, .beats = 2};
    Playing p{song};
    p.player.setMarker(q);

    SECTION("ordinary playback")
    {
        REQUIRE(p.player.play(song));
    }
    SECTION("loop playback")
    {
        REQUIRE(p.player.playLoop(song, q, 2 * q));
    }

    const auto beginning = p.clock.time;
    p.runFor(999ms);
    CHECK(p.player.leadInPhase() == Player::LeadInPhase::CountIn);
    const auto notes = [&](DestinationId destination)
    {
        std::vector<Sent> result;
        for (const auto& event : p.sink.sent)
            if (event.destination == destination && (event.bytes[0] & 0xF0) == 0x90)
                result.push_back(event);
        return result;
    };
    CHECK(notes(DestinationId{1}).empty());
    const auto clicks = notes(DestinationId{2});
    REQUIRE(clicks.size() == 2);
    CHECK(clicks.front().due == beginning);
    CHECK(clicks.back().due == beginning + 500ms);

    p.runFor(502ms);
    CHECK_FALSE(p.player.leadInPhase());
    CHECK(notes(DestinationId{2}) == clicks);
    const auto played = notes(DestinationId{1});
    REQUIRE(played.size() == 2);
    CHECK(played[0].bytes[1] == 62);
    CHECK(played[0].due == beginning + 1s);
    CHECK(played[1].bytes[1] == (p.player.looping() ? 62 : 64));
    CHECK(played[1].due == beginning + 1500ms);
    CHECK(p.player.marker() == q);
}

TEST_CASE("a playback loop plays its musical pre-roll once before wrapping to its own start")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto song = fourNotes(ppqn);
    song.playbackLeadIn = {.mode = CountInSettings::Mode::PreRoll, .preRollBeats = 1};
    Playing p{song};
    const auto beginning = p.clock.time;
    REQUIRE(p.player.playLoop(song, q, 3 * q));
    p.runFor(100ms);
    CHECK(p.player.leadInPhase() == Player::LeadInPhase::PreRoll);
    p.runFor(1901ms);
    CHECK_FALSE(p.player.leadInPhase());

    std::vector<std::pair<int, std::chrono::nanoseconds>> played;
    for (const auto& event : p.sink.sent)
        if ((event.bytes[0] & 0xF0) == 0x90)
            played.emplace_back(event.bytes[1], event.due - beginning);
    CHECK(played == std::vector<std::pair<int, std::chrono::nanoseconds>>{
                        {60, 0ms}, {62, 500ms}, {64, 1000ms}, {62, 1500ms}, {64, 2000ms}});
}

TEST_CASE("every loop pass can count in and Space can stop in the repeated count-in")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto song = fourNotes(ppqn);
    song.metronome.port = "b";
    song.metronome.when = MetronomeSettings::When::Off;
    song.playbackLeadIn = {.mode = CountInSettings::Mode::CountIn, .beats = 2};
    song.repeatPlaybackLeadIn = true;
    song.updateEvents(song.tracks().front()->id(),
                      [&](EventList& events) { events.insert({q / 2, Note{55, 100, q}}); });
    Playing p{song};
    const auto beginning = p.clock.time;
    REQUIRE(p.player.playLoop(song, q, 2 * q));
    p.runFor(1600ms);
    REQUIRE(p.player.leadInPhase() == Player::LeadInPhase::CountIn);
    // The loop's entry chase uses the latest plan, even after earlier snapshots are released.
    p.player.update(song);
    p.runFor(100ms);
    p.player.update(song);
    p.runFor(2500ms);

    std::vector<std::chrono::nanoseconds> clicks;
    std::vector<std::pair<int, std::chrono::nanoseconds>> notes;
    for (const auto& event : p.sink.sent)
    {
        if ((event.bytes[0] & 0xF0) != 0x90)
            continue;
        if (event.destination == DestinationId{2})
            clicks.push_back(event.due - beginning);
        else
            notes.emplace_back(event.bytes[1], event.due - beginning);
    }
    CHECK(clicks ==
          std::vector<std::chrono::nanoseconds>{0ms, 500ms, 1500ms, 2000ms, 3000ms, 3500ms});
    CHECK(notes ==
          std::vector<std::pair<int, std::chrono::nanoseconds>>{
              {55, 1000ms}, {62, 1000ms}, {55, 2500ms}, {62, 2500ms}, {55, 4000ms}, {62, 4000ms}});

    p.runFor(301ms);
    REQUIRE(p.player.leadInPhase() == Player::LeadInPhase::CountIn);
    const auto sent = p.sink.sent.size();
    REQUIRE(p.player.toggle(song));
    p.runFor(2s);
    CHECK_FALSE(p.player.playing());
    REQUIRE(p.sink.sent.size() == sent + 1);
    CHECK(p.sink.sent.back().destination == DestinationId{2});
    CHECK((p.sink.sent.back().bytes[0] & 0xF0) == 0x80);
}

TEST_CASE("every loop pass can replay musical pre-roll without replaying track setup")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto song = fourNotes(ppqn);
    song.updateTrack(song.tracks().front()->id(),
                     [](Track& track) { track.programSetup.program = 9; });
    song.playbackLeadIn = {.mode = CountInSettings::Mode::PreRoll, .preRollBeats = 1};
    song.repeatPlaybackLeadIn = true;
    Playing p{song};
    const auto beginning = p.clock.time;
    REQUIRE(p.player.playLoop(song, q, 2 * q));
    p.runFor(1100ms);
    CHECK(p.player.leadInPhase() == Player::LeadInPhase::PreRoll);
    p.runFor(1401ms);
    CHECK_FALSE(p.player.leadInPhase());

    std::vector<std::pair<int, std::chrono::nanoseconds>> notes;
    int programChanges = 0;
    for (const auto& event : p.sink.sent)
    {
        if ((event.bytes[0] & 0xF0) == 0x90)
            notes.emplace_back(event.bytes[1], event.due - beginning);
        if ((event.bytes[0] & 0xF0) == 0xC0)
            ++programChanges;
    }
    CHECK(notes ==
          std::vector<std::pair<int, std::chrono::nanoseconds>>{
              {60, 0ms}, {62, 500ms}, {60, 1000ms}, {62, 1500ms}, {60, 2000ms}, {62, 2500ms}});
    CHECK(programChanges == 1);
}

TEST_CASE("every-pass lead-in keeps loops seamless when Before playback is None")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto song = fourNotes(ppqn);
    song.repeatPlaybackLeadIn = true;
    Playing p{song};
    const auto beginning = p.clock.time;
    REQUIRE(p.player.playLoop(song, q, 2 * q));
    p.runFor(1100ms);
    std::vector<std::chrono::nanoseconds> times;
    for (const auto& event : p.sink.sent)
        if ((event.bytes[0] & 0xF0) == 0x90)
        {
            CHECK(event.bytes[1] == 62);
            times.push_back(event.due - beginning);
        }
    CHECK(times == std::vector<std::chrono::nanoseconds>{0ms, 500ms, 1000ms});
    CHECK_FALSE(p.player.leadInPhase());
}

TEST_CASE("playing while playing starts again from the marker")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    CHECK(p.player.play(song));
    p.runFor(600ms);
    p.player.setMarker(3 * ppqn.quarter());
    CHECK(p.player.play(song));
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
    const auto song = fourNotes(ppqn);
    Playing p{song};
    CHECK(p.player.play(song));
    p.runFor(100ms);
    CHECK(p.player.panic());
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
    CHECK(p.player.play(song));
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

TEST_CASE("a plan the engine can't take yet is kept and handed over later, the newest")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    // With the engine not running, its queue fills.
    auto later = song;
    for (int i = 0; i < 1000 && !p.player.planWaiting(); ++i)
    {
        later = fourNotes(ppqn);
        later.updateEvents(
            later.tracks()[0]->id(), [&](EventList& events)
            { events.insert({0, Note{static_cast<std::uint8_t>(70 + i % 10), 100, 1}}); });
        p.player.update(later);
    }
    REQUIRE(p.player.planWaiting());
    auto last = fourNotes(ppqn);
    last.updateEvents(last.tracks()[0]->id(),
                      [&](EventList& events) { events.insert({0, Note{90, 100, 1}}); });
    p.player.update(last); // replaces the one waiting
    p.runFor(1ms);
    CHECK_FALSE(p.player.planWaiting());
    CHECK(p.player.play(last));
    p.runFor(1ms);
    bool heard = false;
    for (const auto& sent : p.sink.sent)
    {
        heard = heard || (sent.bytes.size() == 3 && sent.bytes[1] == 90);
    }
    CHECK(heard);
}

TEST_CASE("a record the engine can't take leaves the song planned for playing")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn);
    song.countIn.mode = CountInSettings::Mode::None;
    song.updateTrack(song.tracks()[0]->id(), [](Track& track) { track.arm = true; });
    Playing p{song};
    // With the engine not running, its queue fills.
    for (int i = 0; i < 1000 && !p.player.planWaiting(); ++i)
    {
        p.player.update(song);
    }
    REQUIRE(p.player.planWaiting());
    CHECK_FALSE(p.player.record(song));
    CHECK_FALSE(p.player.recording());
    CHECK(p.player.recordingTracks().empty());

    // What waits for the engine isn't the pass's plan, the armed track silent as replacing
    // leaves it, but the song as it plays.
    p.runFor(1ms);
    CHECK_FALSE(p.player.planWaiting());
    CHECK(p.player.play(song));
    p.runFor(100ms);
    bool heard = false;
    for (const auto& sent : p.sink.sent)
    {
        heard = heard || (sent.bytes.size() == 3 && sent.bytes[1] == 60);
    }
    CHECK(heard);
}

TEST_CASE("a play or stop the engine can't take changes nothing")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    for (int i = 0; i < 1000; ++i)
    {
        const bool was = p.player.playing();
        if (!p.player.toggle(song))
        {
            CHECK(p.player.playing() == was);
            return;
        }
        CHECK(p.player.playing() != was);
    }
    FAIL("the engine's queue never filled");
}

TEST_CASE("player resolves named inputs on each update without affecting playback destinations")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn);
    const auto id = song.tracks()[0]->id();
    song.updateTrack(id,
                     [](Track& track)
                     {
                         track.arm = true;
                         track.input = "Keys";
                     });
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::optional<InputId> input = InputId{1};
    Player player{engine, [](std::string_view) { return DestinationId{1}; },
                  [&](std::string_view name)
                  {
                      CHECK(name == "Keys");
                      return input;
                  }};
    player.update(song);
    CHECK(player.missingInputs().empty());
    engine.step();
    REQUIRE(engine.receive(InputId{0}, std::array<std::uint8_t, 3>{0x90, 60, 100}, clock.time));
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 62, 95}, clock.time));
    engine.step();
    REQUIRE(sink.sent.size() == 1);
    CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x90, 62, 95});
    CHECK(player.takeInput() == 2); // monitoring sees all inputs, before per-track filtering
    REQUIRE(player.lastInput());
    CHECK(player.lastInput()->bytes == std::array<std::uint8_t, 3>{0x90, 62, 95});
    input.reset();
    player.update(song);
    CHECK(player.missingInputs() == std::vector<std::string>{"Keys"});
    engine.step();
    sink.sent.clear();
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 64, 100}, clock.time));
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x80, 62, 0}, clock.time));
    engine.step();
    REQUIRE(sink.sent.size() == 1); // the original held note still releases
    CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x80, 62, 0});
    REQUIRE(player.play(song));
    engine.step();
    CHECK(sink.sent.size() > 1); // missing input doesn't silence the stored song
    input = InputId{2};
    player.update(song);
    CHECK(player.missingInputs().empty());
    engine.step();
    sink.sent.clear();
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 65, 100}, clock.time));
    REQUIRE(engine.receive(InputId{2}, std::array<std::uint8_t, 3>{0x90, 67, 100}, clock.time));
    engine.step();
    REQUIRE(sink.sent.size() == 1);
    CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x90, 67, 100});
}

TEST_CASE("a track's empty input is the machine's default, and * every input")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn);
    const auto id = song.tracks()[0]->id();
    song.updateTrack(id, [](Track& track) { track.arm = true; });
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::optional<InputId> byDefault;
    Player player{engine, [](std::string_view) { return DestinationId{1}; },
                  [&](std::string_view name)
                  {
                      CHECK(name.empty());
                      return byDefault;
                  }};
    // No default input: the track hears nothing, and says so.
    player.update(song);
    CHECK(player.missingInputs() == std::vector<std::string>{""});
    engine.step();
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 60, 100}, clock.time));
    engine.step();
    CHECK(sink.sent.empty());

    byDefault = InputId{2};
    player.update(song);
    CHECK(player.missingInputs().empty());
    engine.step();
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 62, 100}, clock.time));
    REQUIRE(engine.receive(InputId{2}, std::array<std::uint8_t, 3>{0x90, 64, 100}, clock.time));
    engine.step();
    REQUIRE(sink.sent.size() == 1);
    CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x90, 64, 100});

    // Every input, with nothing to resolve.
    song.updateTrack(id, [](Track& track) { track.input = std::string{Track::kAllInputs}; });
    byDefault.reset();
    player.update(song);
    CHECK(player.missingInputs().empty());
    engine.step();
    sink.sent.clear();
    REQUIRE(engine.receive(InputId{1}, std::array<std::uint8_t, 3>{0x90, 65, 100}, clock.time));
    REQUIRE(engine.receive(InputId{3}, std::array<std::uint8_t, 3>{0x90, 67, 100}, clock.time));
    engine.step();
    CHECK(sink.sent.size() == 2);
}

TEST_CASE("the player keeps the last channel message in, but a note-off or system message")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    Player player{engine, [](std::string_view) { return DestinationId{1}; }};
    const auto in = [&](std::vector<std::uint8_t> bytes)
    {
        engine.step();
        REQUIRE(engine.receive(InputId{0}, bytes, clock.time));
        engine.step();
        player.takeInput();
        REQUIRE(player.lastInput());
        const auto& last = *player.lastInput();
        return std::vector<std::uint8_t>(last.bytes.begin(), last.bytes.begin() + last.size);
    };
    CHECK(in({0x91, 60, 100}) == std::vector<std::uint8_t>{0x91, 60, 100});
    CHECK(in({0x81, 60, 0}) == std::vector<std::uint8_t>{0x91, 60, 100});
    CHECK(in({0x91, 60, 0}) == std::vector<std::uint8_t>{0x91, 60, 100}); // a note-off too
    CHECK(in({0xF8}) == std::vector<std::uint8_t>{0x91, 60, 100});        // clock
    CHECK(in({0xB9, 64, 127}) == std::vector<std::uint8_t>{0xB9, 64, 127});
    CHECK(in({0xC0, 5}) == std::vector<std::uint8_t>{0xC0, 5});
    CHECK(in({0xD0, 64}) == std::vector<std::uint8_t>{0xD0, 64});
    CHECK(in({0xA0, 60, 30}) == std::vector<std::uint8_t>{0xA0, 60, 30});
    CHECK(in({0xE0, 0x7F, 0x7F}) == std::vector<std::uint8_t>{0xE0, 0x7F, 0x7F});
}

TEST_CASE("the player keeps each input's last message, found by the port's name")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    Player player{engine, [](std::string_view) { return DestinationId{1}; },
                  [](std::string_view name) -> std::optional<InputId>
                  {
                      if (name == "Keys")
                          return InputId{0};
                      if (name == "Pads")
                          return InputId{1};
                      return std::nullopt;
                  }};
    const auto in = [&](InputId input, std::vector<std::uint8_t> bytes)
    {
        engine.step();
        REQUIRE(engine.receive(input, bytes, clock.time));
        engine.step();
        player.takeInput();
    };
    const auto last = [&](std::string_view port) -> std::vector<std::uint8_t>
    {
        const auto message = player.lastInput(port);
        if (!message)
            return {};
        return {message->bytes.begin(), message->bytes.begin() + message->size};
    };
    CHECK(last("Keys").empty());
    in(InputId{0}, {0x90, 60, 100});
    in(InputId{1}, {0x99, 36, 120});
    in(InputId{0}, {0x80, 60, 0}); // a note-off leaves the note shown
    CHECK(last("Keys") == std::vector<std::uint8_t>{0x90, 60, 100});
    CHECK(last("Pads") == std::vector<std::uint8_t>{0x99, 36, 120});
    CHECK(last("Nowhere").empty());
    REQUIRE(player.lastInput()); // and the last from any
    CHECK(player.lastInput()->bytes == std::array<std::uint8_t, 3>{0x99, 36, 120});
}

TEST_CASE(
    "a track's note is auditioned on its port and channel, as it plays, for 150ms to a second")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn, "b");
    const auto id = song.tracks()[0]->id();
    song.updateTrack(id,
                     [](Track& track)
                     {
                         track.channel = 3;
                         track.transpose = 2;
                         track.velocityOffset = -10;
                     });
    Playing p{song};
    const auto& tempo = song.tempo();

    // A quarter note at the second beat, for as long as it lasts there.
    const Note quarter{62, 100, ppqn.quarter()};
    REQUIRE(p.player.auditionNote(song, id, ppqn.quarter(), quarter));
    p.engine.step();
    REQUIRE(p.sink.sent.size() == 1);
    CHECK(p.sink.sent[0].destination == DestinationId{2});
    CHECK(p.sink.sent[0].bytes == std::vector<std::uint8_t>{0x92, 64, 90});
    const auto start = p.sink.sent[0].due;
    runUntil(p.engine, p.clock, p.clock.time + 2s);
    REQUIRE(p.sink.sent.size() == 2);
    CHECK(p.sink.sent[1].bytes == std::vector<std::uint8_t>{0x82, 64, 0});
    CHECK(p.sink.sent[1].due - start ==
          tempo.toTime(2 * ppqn.quarter()) - tempo.toTime(ppqn.quarter()));

    // A long one for a second.
    p.sink.sent.clear();
    REQUIRE(p.player.auditionNote(song, id, 0, Note{60, 100, 16 * ppqn.quarter()}));
    runUntil(p.engine, p.clock, p.clock.time + 3s);
    REQUIRE(p.sink.sent.size() == 2);
    CHECK(p.sink.sent[1].due - p.sink.sent[0].due == 1s);

    // A very short one for long enough to hear, and for an instrument not to miss its end.
    p.sink.sent.clear();
    REQUIRE(p.player.auditionNote(song, id, 0, Note{60, 100, 1}));
    runUntil(p.engine, p.clock, p.clock.time + 1s);
    REQUIRE(p.sink.sent.size() == 2);
    CHECK(p.sink.sent[1].due - p.sink.sent[0].due == 150ms);

    // Not on a port that isn't there, nor transposed out of range.
    song.updateTrack(id, [](Track& track) { track.port = "gone"; });
    CHECK_FALSE(p.player.auditionNote(song, id, 0, quarter));
    song.updateTrack(id,
                     [](Track& track)
                     {
                         track.port = "b";
                         track.transpose = 100;
                     });
    CHECK_FALSE(p.player.auditionNote(song, id, 0, Note{60, 100, ppqn.quarter()}));
}

TEST_CASE("a track's controller, bend or pressure value is sent at once on its port and channel")
{
    const auto ppqn = MODAL_ANY_PPQN();
    auto song = fourNotes(ppqn, "b");
    const auto id = song.tracks()[0]->id();
    song.updateTrack(id, [](Track& track) { track.channel = 3; });
    Playing p{song};
    REQUIRE(p.player.sendValue(song, id, ControlChange{1, 90}));
    REQUIRE(p.player.sendValue(song, id, PitchBend{0}));
    REQUIRE(p.player.sendValue(song, id, PitchBend{PitchBend::kMax}));
    REQUIRE(p.player.sendValue(song, id, ChannelPressure{40}));
    p.engine.step();
    REQUIRE(p.sink.sent.size() == 4);
    CHECK(p.sink.sent[0].destination == DestinationId{2});
    CHECK(p.sink.sent[0].bytes == std::vector<std::uint8_t>{0xB2, 1, 90});
    CHECK(p.sink.sent[1].bytes == std::vector<std::uint8_t>{0xE2, 0, 64});
    CHECK(p.sink.sent[2].bytes == std::vector<std::uint8_t>{0xE2, 127, 127});
    CHECK(p.sink.sent[3].bytes == std::vector<std::uint8_t>{0xD2, 40});
    CHECK_FALSE(p.player.sendValue(song, id, ProgramChange{5}));
    song.updateTrack(id, [](Track& track) { track.port = "gone"; });
    CHECK_FALSE(p.player.sendValue(song, id, ControlChange{1, 90}));
}

TEST_CASE("a finished play cannot cancel its replacement before the engine starts it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = fourNotes(ppqn);
    Playing p{song};
    REQUIRE(p.player.play(song));
    REQUIRE(p.runFor(3s));
    REQUIRE_FALSE(p.player.playing());
    REQUIRE(p.engine.status().finished);
    const auto sent = p.sink.sent.size();
    REQUIRE(p.player.play(song));
    // The UI can poll before the engine has taken the replacement request.
    CHECK_FALSE(p.player.poll());
    CHECK(p.player.playing());
    p.runFor(1ms);
    CHECK(p.player.playing());
    CHECK(p.engine.status().playing);
    REQUIRE(p.sink.sent.size() > sent);
    CHECK(p.sink.sent[sent].bytes == std::vector<std::uint8_t>{0x90, 60, 100});
    CHECK(p.runFor(3s));
    CHECK_FALSE(p.player.playing());
}
