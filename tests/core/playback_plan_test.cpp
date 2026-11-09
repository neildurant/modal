// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/edit_track.hpp"
#include "support/ppqn.hpp"

#include <modal/core/playback_plan.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;
using std::chrono::nanoseconds;

namespace
{

constexpr DestinationId kPort{1};

std::optional<DestinationId> everythingToPort(const Track&)
{
    return kPort;
}

ShortMessage bytes(std::uint8_t a, std::uint8_t b)
{
    return {{a, b, 0}, 2};
}

ShortMessage bytes(std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    return {{a, b, c}, 3};
}

// A song at 120 BPM, so a quarter note is 500 ms at any resolution.
Song songAt(Ppqn ppqn)
{
    Song song{ppqn};
    song.updateTempo([&](TempoMap& tempo) { tempo.set(0, Tempo{500'000}); });
    return song;
}

TrackId addTrack(Song& song, int channel, const std::function<void(EditTrack&)>& edit = {})
{
    const auto id = song.addTrack();
    editTrack(song, id,
              [&](EditTrack& track)
              {
                  track.channel = channel;
                  if (edit)
                  {
                      edit(track);
                  }
              });
    return id;
}

std::vector<ShortMessage> messagesOf(const std::vector<PlannedEvent>& events)
{
    std::vector<ShortMessage> result;
    for (const auto& event : events)
    {
        result.push_back(event.message);
    }
    return result;
}

} // namespace

TEST_CASE("events become channel messages on the track's channel")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 10,
             [&](EditTrack& track)
             {
                 track.events.insert({0, Note{36, 100, q, 70}});
                 track.events.insert({q, ControlChange{7, 90}});
                 track.events.insert({q, ProgramChange{5}});
                 track.events.insert({q, PitchBend{0}});
                 track.events.insert({q, PitchBend{PitchBend::kMin}});
                 track.events.insert({q, PitchBend{PitchBend::kMax}});
                 track.events.insert({q, ChannelPressure{33}});
                 track.events.insert({q, PolyPressure{36, 44}});
                 track.events.insert({q, makeMetaText(MetaText::Kind::Lyric, "la")});
             });

    const auto plan = compilePlan(song, everythingToPort);
    REQUIRE(plan.events.size() == 8); // the lyric isn't sent
    CHECK(messagesOf(plan.events) == std::vector<ShortMessage>{
                                         bytes(0x99, 36, 100),
                                         bytes(0xB9, 7, 90),
                                         bytes(0xC9, 5),
                                         bytes(0xE9, 0x00, 0x40),
                                         bytes(0xE9, 0x00, 0x00),
                                         bytes(0xE9, 0x7F, 0x7F),
                                         bytes(0xD9, 33),
                                         bytes(0xA9, 36, 44),
                                     });

    const auto& note = plan.events[0];
    CHECK(note.isNoteOn());
    CHECK(note.time == 0ns);
    CHECK(note.endTick == q);
    CHECK(note.noteOffTime == 500ms);
    CHECK(note.releaseVelocity == 70);
    CHECK(note.destination == kPort);
    CHECK(plan.events[1].time == 500ms);
    CHECK_FALSE(plan.events[1].isNoteOn());
}

TEST_CASE("event times follow the tempo map")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    song.updateTempo([&](TempoMap& tempo)
                     { tempo.set(4 * q, Tempo{1'000'000}); }); // 60 BPM from bar 2
    addTrack(song, 1, [&](EditTrack& track) { track.events.insert({5 * q, Note{60, 100, q}}); });

    const auto plan = compilePlan(song, everythingToPort);
    REQUIRE(plan.events.size() == 1);
    CHECK(plan.events[0].time == 3s);
    CHECK(plan.events[0].noteOffTime == 4s);
}

TEST_CASE("SysEx is sent as stored")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const std::vector<std::uint8_t> reset{0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    addTrack(song, 1, [&](EditTrack& track) { track.events.insert({0, makeSysEx(reset)}); });

    const auto plan = compilePlan(song, everythingToPort);
    REQUIRE(plan.events.size() == 1);
    REQUIRE(plan.events[0].sysex);
    CHECK(*plan.events[0].sysex == reset);
    CHECK(plan.events[0].message.size == 0);
}

TEST_CASE("transpose and velocity offset apply at playback")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 track.transpose = 12;
                 track.velocityOffset = 40;
                 track.events.insert({0, Note{60, 100, q}});
                 track.events.insert({q, Note{120, 100, q}}); // 132: out of range, left out
                 track.events.insert({2 * q, PolyPressure{60, 50}});
             });

    auto plan = compilePlan(song, everythingToPort);
    REQUIRE(plan.events.size() == 2);
    CHECK(plan.events[0].message == bytes(0x90, 72, 127)); // velocity clamped
    CHECK(plan.events[1].message == bytes(0xA0, 72, 50));
    CHECK(plan.transposeWarnings == std::vector<TransposeWarning>{{song.tracks()[0]->id(), 1}});

    editTrack(song, song.tracks()[0]->id(),
              [](EditTrack& track)
              {
                  track.transpose = 0;
                  track.velocityOffset = -127;
              });
    plan = compilePlan(song, everythingToPort);
    CHECK(plan.events[0].message == bytes(0x90, 60, 1)); // never velocity 0, a note-off
    CHECK(plan.transposeWarnings.empty());
}

TEST_CASE("mute and solo")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto addNoteTrack = [&](int channel)
    {
        return addTrack(song, channel, [&](EditTrack& track)
                        { track.events.insert({0, Note{60, 100, ppqn.quarter()}}); });
    };
    const auto a = addNoteTrack(1);
    const auto b = addNoteTrack(2);
    const auto c = addNoteTrack(3);

    const auto tracksIn = [&](const PlaybackPlan& plan)
    {
        std::vector<TrackId> ids;
        for (const auto& track : plan.tracks)
        {
            ids.push_back(track.id);
        }
        return ids;
    };

    CHECK(tracksIn(compilePlan(song, everythingToPort)) == std::vector<TrackId>{a, b, c});

    editTrack(song, b, [](EditTrack& track) { track.mute = true; });
    CHECK(tracksIn(compilePlan(song, everythingToPort)) == std::vector<TrackId>{a, c});

    // Any solo plays only the soloed tracks, even a muted one.
    editTrack(song, b, [](EditTrack& track) { track.solo = true; });
    editTrack(song, c, [](EditTrack& track) { track.solo = true; });
    const auto plan = compilePlan(song, everythingToPort);
    CHECK(tracksIn(plan) == std::vector<TrackId>{b, c});
    CHECK(plan.events.size() == 2);
}

TEST_CASE("tracks without a destination are left out and reported")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto routed = addTrack(song, 1, [](EditTrack& track) { track.port = "Synth"; });
    const auto unrouted = addTrack(song, 2, [](EditTrack& track) { track.port = "Missing"; });

    const auto plan = compilePlan(song,
                                  [](const Track& track) -> std::optional<DestinationId>
                                  {
                                      if (track.port == "Synth")
                                      {
                                          return DestinationId{7};
                                      }
                                      return std::nullopt;
                                  });
    REQUIRE(plan.tracks.size() == 1);
    CHECK(plan.tracks[0].id == routed);
    CHECK(plan.tracks[0].destination == DestinationId{7});
    CHECK(plan.unrouted == std::vector<TrackId>{unrouted});
    CHECK(plan.track(unrouted) == nullptr);
}

TEST_CASE("track time offsets move events in time but not in ticks")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto strings = addTrack(song, 1,
                                  [&](EditTrack& track)
                                  {
                                      track.timeOffset = -40ms;
                                      track.events.insert({0, Note{60, 100, q}});
                                      track.events.insert({q, Note{64, 100, q}});
                                  });
    const auto late = addTrack(song, 2,
                               [&](EditTrack& track)
                               {
                                   track.timeOffset = 25ms;
                                   track.events.insert({q, Note{48, 100, q}});
                               });
    addTrack(song, 3, [&](EditTrack& track) { track.events.insert({q, Note{36, 100, q}}); });

    const auto plan = compilePlan(song, everythingToPort);
    CHECK(plan.leadIn == 40ms);
    REQUIRE(plan.events.size() == 4);

    // Sorted by when they're sent.
    CHECK(plan.events[0].time == -40ms);
    CHECK(plan.events[0].tick == 0);
    CHECK(plan.events[0].noteOffTime == 460ms);
    CHECK(plan.events[1].time == 460ms);
    CHECK(plan.events[1].track == strings);
    CHECK(plan.events[2].time == 500ms);
    CHECK(plan.events[3].time == 525ms);
    CHECK(plan.events[3].track == late);
    CHECK(plan.events[3].tick == q);
}

TEST_CASE("events at the same time keep track order, then stored order")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto first = addTrack(song, 1,
                                [&](EditTrack& track)
                                {
                                    track.events.insert({0, Note{60, 100, ppqn.quarter()}});
                                    track.events.insert({0, ProgramChange{1}});
                                });
    const auto second =
        addTrack(song, 2, [](EditTrack& track) { track.events.insert({0, ProgramChange{2}}); });

    const auto plan = compilePlan(song, everythingToPort);
    REQUIRE(plan.events.size() == 3);
    CHECK(plan.events[0].track == first);
    CHECK(plan.events[0].message == bytes(0xC0, 1));
    CHECK(plan.events[1].track == first);
    CHECK(plan.events[1].isNoteOn());
    CHECK(plan.events[2].track == second);
}

TEST_CASE("finding the first event at or after a time")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 for (Tick i = 0; i < 4; ++i)
                 {
                     track.events.insert({i * q, Note{60, 100, q}});
                 }
             });
    const auto plan = compilePlan(song, everythingToPort);
    CHECK(plan.firstAtOrAfter(0ns) == 0);
    CHECK(plan.firstAtOrAfter(1ns) == 1);
    CHECK(plan.firstAtOrAfter(500ms) == 1);
    CHECK(plan.firstAtOrAfter(1500ms) == 3);
    CHECK(plan.firstAtOrAfter(10s) == 4);
}

TEST_CASE("chasing sends the latest values before the start, in the order sent")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 track.events.insert({0, ControlChange{0, 1}}); // bank select
                 track.events.insert({0, ProgramChange{10}});
                 track.events.insert({0, ControlChange{7, 100}});
                 track.events.insert({q, ControlChange{7, 80}}); // replaces volume 100
                 track.events.insert({q, PitchBend{1000}});
                 track.events.insert({q, ControlChange{123, 0}}); // all notes off: not chased
                 track.events.insert({q, ChannelPressure{20}});
                 track.events.insert({2 * q, ControlChange{10, 64}}); // at the start: played
                 track.events.insert({3 * q, ProgramChange{11}});     // after the start
             });

    const auto plan = compilePlan(song, everythingToPort);
    const auto chased = chase(plan, 2 * q);
    CHECK(messagesOf(chased) == std::vector<ShortMessage>{
                                    bytes(0xB0, 0, 1),
                                    bytes(0xC0, 10),
                                    bytes(0xB0, 7, 80),
                                    bytes(0xE0, (1000 + 8192) & 0x7F, (1000 + 8192) >> 7),
                                    bytes(0xD0, 20),
                                });
    for (const auto& event : chased)
    {
        CHECK(event.time == 1s);
    }

    CHECK(chase(plan, 0).empty());
}

TEST_CASE("chasing restarts notes still sounding, for tracks that chase notes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto pad = addTrack(song, 1,
                              [&](EditTrack& track)
                              {
                                  track.transpose = 2;
                                  track.events.insert({0, Note{60, 90, 8 * q}}); // sounding
                                  track.events.insert({0, Note{48, 90, 2 * q}}); // ends at start
                                  track.events.insert({q, Note{64, 90, 4 * q}}); // sounding
                                  track.events.insert({2 * q, Note{67, 90, q}}); // at the start
                              });
    addTrack(song, 10,
             [&](EditTrack& track)
             {
                 track.chaseNotes = false;
                 track.events.insert({0, ProgramChange{1}});
                 track.events.insert({0, Note{49, 100, 8 * q}}); // a cymbal: not restarted late
             });

    const auto plan = compilePlan(song, everythingToPort);
    const auto chased = chase(plan, 2 * q);
    REQUIRE(chased.size() == 3);

    CHECK(chased[0].track == pad);
    CHECK(chased[0].message == bytes(0x90, 62, 90)); // transposed, as played
    CHECK(chased[0].time == 1s);
    CHECK(chased[0].noteOffTime == 4s); // ends when it would have
    CHECK(chased[1].message == bytes(0x90, 66, 90));
    CHECK(chased[1].noteOffTime == 2500ms);

    // The drum track still chases its program change.
    CHECK(chased[2].message == bytes(0xC9, 1));
}

TEST_CASE("chasing with time offsets uses positions, and starts early by the lead-in")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 track.timeOffset = 30ms;
                 track.events.insert({q - 1, ControlChange{7, 50}}); // sent after the start time
             });
    addTrack(song, 2,
             [&](EditTrack& track)
             {
                 track.timeOffset = -40ms;
                 track.events.insert({q, ControlChange{7, 60}}); // sent before the start time
             });

    const auto plan = compilePlan(song, everythingToPort);
    const auto chased = chase(plan, q);

    // The event stored before the start is chased even though it's sent after the start
    // time; the one stored at the start is played normally even though it's sent before.
    REQUIRE(chased.size() == 1);
    CHECK(chased[0].message == bytes(0xB0, 7, 50));
    CHECK(chased[0].time == 500ms - 40ms);
}

TEST_CASE("armed tracks with thru on are thru targets, muted or not")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto armed = addTrack(song, 3,
                                [](EditTrack& track)
                                {
                                    track.arm = true;
                                    track.port = "4";
                                    track.transpose = -12;
                                    track.velocityOffset = 5;
                                    track.timeOffset = std::chrono::milliseconds{-20};
                                });
    const auto mutedArmed = addTrack(song, 1,
                                     [](EditTrack& track)
                                     {
                                         track.arm = true;
                                         track.mute = true;
                                     });
    addTrack(song, 1); // not armed
    addTrack(song, 1,
             [](EditTrack& track)
             {
                 track.arm = true;
                 track.thru = false;
             });
    addTrack(song, 1,
             [](EditTrack& track)
             {
                 track.arm = true;
                 track.port = "Missing";
             });

    const auto plan = compilePlan(song,
                                  [](const Track& track) -> std::optional<DestinationId>
                                  {
                                      if (track.port == "Missing")
                                      {
                                          return std::nullopt;
                                      }
                                      return track.port == "4" ? DestinationId{4} : kPort;
                                  });
    CHECK(plan.thru == std::vector<ThruTarget>{
                           {armed, DestinationId{4}, 2, -12, 5},
                           {mutedArmed, DestinationId{1}, 0, 0, 0},
                       });
}

TEST_CASE("the metronome is planned when its settings say it plays, and it has a port")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1);
    song.updateMeter([&](MeterMap& meter) { meter.set(0, {3, 4}); });
    constexpr DestinationId kClick{9};
    using When = MetronomeSettings::When;

    const PlanOptions playing{.recording = false, .metronomeDestination = kClick};
    const PlanOptions recording{.recording = true, .metronomeDestination = kClick};
    const PlanOptions nowhere{.recording = true, .metronomeDestination = std::nullopt};

    CHECK(song.metronome.when == When::Recording);
    CHECK_FALSE(compilePlan(song, everythingToPort, playing).metronome);
    CHECK_FALSE(compilePlan(song, everythingToPort, nowhere).metronome);
    const auto plan = compilePlan(song, everythingToPort, recording);
    REQUIRE(plan.metronome);
    CHECK(plan.metronome->destination == kClick);
    CHECK(plan.metronome->channel == 9);
    CHECK(plan.metronome->downbeatNote == 76);
    CHECK(plan.metronome->downbeatVelocity == 110);
    CHECK(plan.metronome->beatNote == 77);
    CHECK(plan.metronome->beatVelocity == 80);
    CHECK(plan.metronome->length == ppqn.quarter() / 8);
    CHECK(plan.metronome->meter == song.meter());
    CHECK(plan.metronome->meter.signatureAt(0) == TimeSignature{3, 4});
    CHECK(plan.metronome->pastEnd);

    song.metronome.when = When::Always;
    const auto played = compilePlan(song, everythingToPort, playing);
    REQUIRE(played.metronome);
    CHECK_FALSE(played.metronome->pastEnd); // it stops when the song ends
    song.metronome.when = When::Off;
    CHECK_FALSE(compilePlan(song, everythingToPort, recording).metronome);
}

TEST_CASE("a track's setup is chased first, so later events can override it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 2,
             [&](EditTrack& track)
             {
                 track.programSetup = {1, 2, 3};
                 track.events.insert({0, ProgramChange{10}});
                 track.events.insert({3 * q, ProgramChange{11}});
             });
    const auto plan = compilePlan(song, everythingToPort);

    SECTION("from the song's start, only the setup goes ahead of the events")
    {
        const auto chased = chase(plan, 0);
        CHECK(messagesOf(chased) ==
              std::vector<ShortMessage>{bytes(0xB1, 0, 1), bytes(0xB1, 32, 2), bytes(0xC1, 3)});
        for (const auto& event : chased)
        {
            CHECK(event.time == 0s);
        }
    }
    SECTION("from mid-song, the track's own program comes after it")
    {
        CHECK(messagesOf(chase(plan, 2 * q)) ==
              std::vector<ShortMessage>{bytes(0xB1, 0, 1), bytes(0xB1, 32, 2), bytes(0xC1, 3),
                                        bytes(0xC1, 10)});
    }
}

TEST_CASE("only the parts of a setup that are set are sent")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    song.updateTrack(id, [](Track& track) { track.programSetup.bankMsb = 0; });
    CHECK(messagesOf(chase(compilePlan(song, everythingToPort), 0)) ==
          std::vector<ShortMessage>{bytes(0xB0, 0, 0)});

    song.updateTrack(id,
                     [](Track& track) { track.programSetup = {std::nullopt, std::nullopt, 0}; });
    CHECK(messagesOf(chase(compilePlan(song, everythingToPort), 0)) ==
          std::vector<ShortMessage>{bytes(0xC0, 0)});
}

TEST_CASE("a muted track's setup isn't sent")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 track.mute = true;
                 track.programSetup.program = 5;
             });
    CHECK(chase(compilePlan(song, everythingToPort), 0).empty());
}

namespace
{
// The notes' ticks as they play, and as they're stored, on the plan's first track.
std::vector<std::pair<Tick, Tick>> noteTicks(const PlaybackPlan& plan)
{
    std::vector<std::pair<Tick, Tick>> ticks;
    for (const auto& event : plan.events)
    {
        if (event.isNoteOn())
        {
            ticks.emplace_back(event.tick, event.stored);
        }
    }
    return ticks;
}
} // namespace

TEST_CASE("a quantised track plays its notes on the grid, stored where they were")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick s = ppqn.quarter() / 4; // a sixteenth
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.events.insert({s - 5, Note{60, 100, s}});
                                 track.events.insert({2 * s + 3, Note{62, 100, s}});
                                 track.events.insert({2 * s + 4, ControlChange{7, 90}});
                             });
    CHECK(noteTicks(compilePlan(song, everythingToPort)) ==
          std::vector<std::pair<Tick, Tick>>{{s - 5, s - 5}, {2 * s + 3, 2 * s + 3}});

    song.updateTrack(id,
                     [](Track& track)
                     {
                         track.quantize.on = true; // to its editing grid, 1/16
                     });
    const auto plan = compilePlan(song, everythingToPort);
    CHECK(noteTicks(plan) == std::vector<std::pair<Tick, Tick>>{{s, s - 5}, {2 * s, 2 * s + 3}});
    CHECK(plan.quantizeWarnings.empty());
    CHECK(song.events(id).all()[0].tick == s - 5); // the song unchanged
    // A controller stays where it is.
    const auto cc = std::ranges::find_if(plan.events, [](const PlannedEvent& e)
                                         { return (e.message.bytes[0] & 0xF0) == 0xB0; });
    REQUIRE(cc != plan.events.end());
    CHECK(cc->tick == 2 * s + 4);

    SECTION("its own grid and strength")
    {
        song.updateTrack(id,
                         [](Track& track)
                         {
                             track.quantize.grid = NoteFraction{1, 8};
                             track.quantize.strength = 50;
                         });
        // Halfway to the eighth lines, 0 and 2s, as quantize() works it out.
        const auto halfway = [](Tick from, Tick to) { return from + (to - from) * 50 / 100; };
        CHECK(noteTicks(compilePlan(song, everythingToPort)) ==
              std::vector<std::pair<Tick, Tick>>{{halfway(s - 5, 0), s - 5},
                                                 {halfway(2 * s + 3, 2 * s), 2 * s + 3}});
    }
    SECTION("bypassed, or off with its settings kept, as stored")
    {
        const auto stored =
            std::vector<std::pair<Tick, Tick>>{{s - 5, s - 5}, {2 * s + 3, 2 * s + 3}};
        CHECK(noteTicks(compilePlan(song, everythingToPort,
                                    PlanOptions{false, std::nullopt, true})) == stored);
        song.updateTrack(id, [](Track& track) { track.quantize.on = false; });
        CHECK(noteTicks(compilePlan(song, everythingToPort)) == stored);
    }
}

TEST_CASE("a quantised track's pedalling goes with its notes, and its events keep their order")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick s = ppqn.quarter() / 4;
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track)
             {
                 track.quantize.on = true;
                 track.events.insert({s + 4, Note{60, 100, s}});
                 track.events.insert({s + 6, ControlChange{64, 127}});
                 track.events.insert({4 * s, ProgramChange{3}});
                 track.events.insert({4 * s - 2, Note{64, 100, s}});
             });
    const auto plan = compilePlan(song, everythingToPort);
    std::vector<std::pair<Tick, std::uint8_t>> sent;
    for (const auto& event : plan.events)
    {
        sent.emplace_back(event.tick, event.message.bytes[0]);
    }
    CHECK(sent == std::vector<std::pair<Tick, std::uint8_t>>{
                      {s, 0x90}, {s + 2, 0xB0}, {4 * s, 0xC0}, {4 * s, 0x90}});
}

TEST_CASE("of two notes of a pitch that would start together, the further stays where it is")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick s = ppqn.quarter() / 4;
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.quantize.on = true;
                                 track.events.insert({s - 2, Note{60, 100, 1}});
                                 track.events.insert({s + 2, Note{60, 100, 1}});
                                 track.events.insert({s + 3, Note{62, 100, 1}}); // another pitch
                                 track.events.insert({3 * s, Note{60, 100, 1}}); // on its line
                                 track.events.insert({3 * s + 1, Note{60, 100, 1}});
                             });
    const auto plan = compilePlan(song, everythingToPort);
    // As far from the line, the later is left; the one already on its line keeps it, and the next,
    // which would have joined it, is left.
    CHECK(noteTicks(plan) ==
          std::vector<std::pair<Tick, Tick>>{
              {s, s - 2}, {s, s + 3}, {s + 2, s + 2}, {3 * s, 3 * s}, {3 * s + 1, 3 * s + 1}});
    CHECK(plan.quantizeWarnings == std::vector<QuantizeWarning>{{id, s + 2, 2}});
    REQUIRE(plan.tracks.front().quantize);
}

TEST_CASE("on a coarse grid, a busy track is still quantised, but for its clashes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick e = ppqn.quarter() / 2;
    Song song = songAt(ppqn);
    addTrack(song, 10,
             [&](EditTrack& track)
             {
                 track.quantize = {true, NoteFraction{1, 4}, 100, 50, QuantizeMove::Starts};
                 for (int i = 0; i < 8; ++i)
                 {
                     track.events.insert({i * e + 5, Note{36, 100, 1}}); // eighths, a little late
                 }
             });
    const auto plan = compilePlan(song, everythingToPort);
    // The notes played just after a beat take it; those just after the off-beats between, which
    // would move further to it, stay late, but for the last, with no beat to share.
    std::vector<Tick> left;
    std::size_t onLines = 0;
    for (const auto& [tick, stored] : noteTicks(plan))
    {
        onLines += tick % (2 * e) == 0 ? 1 : 0;
        if (tick == stored)
        {
            left.push_back(tick);
        }
    }
    CHECK(onLines == 5);
    CHECK(left == std::vector<Tick>{e + 5, 3 * e + 5, 5 * e + 5});
    REQUIRE(plan.quantizeWarnings.size() == 1);
    CHECK(plan.quantizeWarnings.front().leftInPlace == 3);
}

TEST_CASE("pedalling follows a note left in place by a playback quantise clash")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto controller = static_cast<std::uint8_t>(GENERATE(64, 66, 67));
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id =
        addTrack(song, 1,
                 [&](EditTrack& track)
                 {
                     track.quantize = {true, NoteFraction{1, 4}, 100, 50, QuantizeMove::Starts};
                     track.events.insert({0, Note{60, 100, 1}});
                     track.events.insert({q / 8, Note{62, 100, 1}});
                     track.events.insert({q / 4, Note{60, 100, q / 2}});
                     track.events.insert({5 * q / 16, ControlChange{controller, 127}});
                     track.events.insert({7 * q / 16, ControlChange{controller, 0}});
                     track.events.insert({2 * q + q / 8, Note{64, 100, 1}});
                     track.events.insert({2 * q + q / 4, ControlChange{controller, 127}});
                 });
    const auto plan = compilePlan(song, everythingToPort);
    CHECK(plan.quantizeWarnings == std::vector<QuantizeWarning>{{id, q / 4, 1}});
    std::vector<std::pair<Tick, Tick>> pedals;
    for (const auto& event : plan.events)
    {
        if (event.message.bytes[0] == 0xB0 && event.message.bytes[1] == controller)
        {
            pedals.emplace_back(event.tick, event.stored);
        }
    }
    CHECK(pedals == std::vector<std::pair<Tick, Tick>>{
                        {5 * q / 16, 5 * q / 16},
                        {7 * q / 16, 7 * q / 16},
                        {2 * q + q / 8, 2 * q + q / 4},
                    });
}
