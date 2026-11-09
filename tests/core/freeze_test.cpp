// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/freeze.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <variant>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;

namespace
{
FrozenTrack prepared(const Song& song, TrackId id, FreezePart part = FreezePart::All)
{
    auto result = freezeTrack(song, id, part);
    REQUIRE(std::holds_alternative<FrozenTrack>(result));
    return std::get<FrozenTrack>(std::move(result));
}

void apply(Song& song, const FrozenTrack& frozen)
{
    const auto id = frozen.settings.id();
    song.updateEvents(id, [&](EventList& events) { events = frozen.events; });
    song.updateTrack(id, [&](Track& track) { track = frozen.settings; });
}
} // namespace

TEST_CASE("Freeze All preserves planned messages and exact musical timing")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id =
        addTrack(song, 3,
                 [&](EditTrack& track)
                 {
                     track.transpose = 5;
                     track.velocityOffset = 40;
                     track.timeOffset = TimeOffset::ticks(q / 8);
                     track.quantize.on = true;
                     track.quantize.grid = NoteFraction{1, 8};
                     track.quantize.move = QuantizeMove::Both;
                     track.programSetup = {2, 3, 4};
                     track.events.insert({q + q / 8, Note{60, 100, q / 2, 23}});
                     track.events.insert({q + q / 4, ControlChange{64, 127}});
                     track.events.insert({2 * q, PolyPressure{60, 70}});
                     track.events.insert({3 * q, ProgramChange{7}});
                     track.events.insert({4 * q, makeSysEx({0xF0, 0x7D, 1, 0xF7})});
                     track.events.insert({5 * q, makeMetaText(MetaText::Kind::Lyric, "word")});
                 });
    song.updateTempo([&](TempoMap& tempo) { tempo.set(2 * q, Tempo::fromBpm(90)); });
    const auto original = song;
    const auto before = compilePlan(song, byPortName);
    const auto frozen = prepared(song, id);
    CHECK(song == original);
    CHECK(frozen.settings.transpose == 0);
    CHECK(frozen.settings.velocityOffset == 0);
    CHECK(frozen.settings.timeOffset.amount() == 0);
    CHECK_FALSE(frozen.settings.quantize.on);
    CHECK(frozen.settings.quantize.grid == song.track(id)->quantize.grid);
    CHECK(frozen.settings.programSetup == song.track(id)->programSetup);
    CHECK(frozen.settings.channel == 3);
    apply(song, frozen);
    const auto after = compilePlan(song, byPortName);
    REQUIRE(after.events.size() == before.events.size());
    for (std::size_t i = 0; i < before.events.size(); ++i)
    {
        CHECK(after.events[i].message == before.events[i].message);
        CHECK(after.events[i].sysex == before.events[i].sysex);
        CHECK(after.events[i].time == before.events[i].time);
        CHECK(after.events[i].noteOffTime == before.events[i].noteOffTime);
        CHECK(after.events[i].releaseVelocity == before.events[i].releaseVelocity);
    }
    CHECK(song.events(id)[song.events(id).size() - 1].tick == 5 * q + q / 8);
}

TEST_CASE("Freeze applies only the chosen transform and retains the others")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.transpose = 12;
                                 track.velocityOffset = -30;
                                 track.timeOffset = TimeOffset::ticks(q / 4);
                                 track.quantize.on = true;
                                 track.events.insert({q + q / 16, Note{60, 20, q, 91}});
                                 track.events.insert({2 * q, PolyPressure{60, 90}});
                             });
    const auto part = GENERATE(FreezePart::Transpose, FreezePart::Velocity, FreezePart::Quantize);
    const auto frozen = prepared(song, id, part);
    auto settings = *song.track(id);
    auto note = std::get<Note>(song.events(id)[0].data);
    Tick tick = q + q / 16;
    if (part == FreezePart::Transpose)
    {
        settings.transpose = 0;
        note.pitch = 72;
        CHECK(std::get<PolyPressure>(frozen.events[1].data).pitch == 72);
    }
    else if (part == FreezePart::Velocity)
    {
        settings.velocityOffset = 0;
        note.velocity = 1;
    }
    else
    {
        settings.quantize.on = false;
        tick = q;
    }
    CHECK(frozen.settings == settings);
    CHECK(frozen.events[0] == Event{tick, note});
    const auto before = compilePlan(song, byPortName);
    apply(song, frozen);
    const auto after = compilePlan(song, byPortName);
    REQUIRE(before.events.size() == after.events.size());
    for (std::size_t i = 0; i < before.events.size(); ++i)
    {
        CHECK(after.events[i].time == before.events[i].time);
        CHECK(after.events[i].noteOffTime == before.events[i].noteOffTime);
        CHECK(after.events[i].message == before.events[i].message);
    }
}

TEST_CASE("Freeze refuses time before active quantise and quantise while bypassed")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1, [](EditTrack& track) { track.quantize.on = true; });
    CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::TimeOffset)));
    CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::Quantize, true)));
    CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::All, true)));
    CHECK(std::holds_alternative<FrozenTrack>(freezeTrack(song, id, FreezePart::Transpose, true)));
    CHECK(std::holds_alternative<FrozenTrack>(freezeTrack(song, id, FreezePart::Velocity, true)));
}

TEST_CASE("Freeze refuses invalid pitches and starts without discarding or clipping events")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.events.insert({0, Note{60, 100, ppqn.quarter()}});
                                 track.events.insert({ppqn.quarter(), PolyPressure{127, 100}});
                             });
    SECTION("a note outside MIDI")
    {
        song.updateTrack(id, [](Track& track) { track.transpose = -61; });
    }
    SECTION("polyphonic pressure outside MIDI")
    {
        song.updateTrack(id, [](Track& track) { track.transpose = 1; });
    }
    SECTION("negative musical offset")
    {
        song.updateTrack(id, [](Track& track) { track.timeOffset = TimeOffset::ticks(-1); });
    }
    SECTION("negative millisecond offset even if it would round to zero")
    {
        song.updateTrack(id, [](Track& track) { track.timeOffset = -1ms; });
    }
    const auto original = song;
    CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::All)));
    CHECK(song == original);
}

TEST_CASE("Freeze moves across sections without splitting notes or changing boundaries")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar = ppqn.whole();
    Song song = songAt(ppqn);
    const auto end = song.end();
    const auto id =
        addTrack(song, 1,
                 [&](EditTrack& track)
                 {
                     track.timeOffset = TimeOffset::ticks(ppqn.quarter());
                     track.events.insert({bar - ppqn.quarter() / 2, Note{60, 100, bar}});
                     track.events.insert({end - 1, ControlChange{7, 90}});
                 });
    song.splitSection(bar, "Verse");
    const auto length = song.end();
    apply(song, prepared(song, id, FreezePart::TimeOffset));
    CHECK(song.sectionStart(1) == bar);
    CHECK(song.sections()[0]->name() == "Section 1");
    CHECK(song.sections()[1]->name() == "Verse");
    const auto* empty = song.sections()[0]->content(id);
    CHECK((empty == nullptr || empty->events.empty()));
    const auto& moved = song.sections()[1]->content(id)->events;
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].tick == ppqn.quarter() / 2);
    CHECK(std::get<Note>(moved[0].data).duration == bar);
    CHECK(song.end() > length);
}

TEST_CASE("millisecond Freeze rounds each end under the current tempo map")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.timeOffset = 40ms;
                                 track.events.insert({q - q / 16, Note{60, 100, q / 2}});
                             });
    song.updateTempo([&](TempoMap& map) { map.set(q, Tempo::fromBpm(60)); });
    const auto before = compilePlan(song, byPortName);
    apply(song, prepared(song, id, FreezePart::TimeOffset));
    const auto after = compilePlan(song, byPortName);
    REQUIRE(after.events.size() == 1);
    const auto closeTo = [&](auto actual, auto expected)
    {
        const auto tick = song.tempo().toTick(expected);
        const auto halfTick = (song.tempo().toTime(tick + 1) - song.tempo().toTime(tick)) / 2;
        CHECK(std::chrono::abs(actual - expected) <= halfTick + 1ns);
    };
    closeTo(after.events[0].time, before.events[0].time);
    closeTo(after.events[0].noteOffTime, before.events[0].noteOffTime);
    CHECK(song.track(id)->timeOffset.amount() == 0);
    CHECK(std::get<Note>(song.events(id)[0].data).duration != q / 2);
}

TEST_CASE("millisecond Freeze ties earlier and refuses collapsed notes or new collisions")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick at = ppqn.quarter();
    Song song = songAt(ppqn);
    song.updateTempo([&](TempoMap& map) { map.set(0, Tempo{ppqn.value * 2000LL}); });
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.timeOffset = 1ms;
                                 track.events.insert({at, Note{60, 100, 2}});
                             });
    CHECK(prepared(song, id).events[0] == song.events(id)[0]);
    SECTION("a positive note cannot collapse at a slower tempo")
    {
        song.updateTempo([&](TempoMap& map) { map.set(at + 1, Tempo{60'000'000}); });
        song.updateEvents(id,
                          [&](EventList& events)
                          {
                              events = {};
                              events.insert({at, Note{60, 100, 1}});
                          });
        song.updateTrack(id, [](Track& track) { track.timeOffset = 3ms; });
        CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::All)));
    }
    SECTION("distinct zero-length starts cannot become coincident")
    {
        song.updateTempo([&](TempoMap& map) { map.set(at + 1, Tempo{60'000'000}); });
        song.updateEvents(id,
                          [&](EventList& events)
                          {
                              events = {};
                              events.insert({at, Note{60, 100, 0}});
                              events.insert({at + 1, Note{60, 90, 0}});
                          });
        song.updateTrack(id, [](Track& track) { track.timeOffset = 3ms; });
        CHECK(std::holds_alternative<FreezeRefusal>(freezeTrack(song, id, FreezePart::All)));
    }
    SECTION("existing imported overlaps remain intact")
    {
        song.updateEvents(id, [&](EventList& events) { events.insert({at + 1, Note{60, 90, 3}}); });
        CHECK(prepared(song, id).events == song.events(id));
    }
}

TEST_CASE("Freeze uses playback quantise's notes left in place")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.quantize.on = true;
                                 track.quantize.grid = NoteFraction{1, 4};
                                 track.events.insert({0, Note{60, 100, 1}});
                                 track.events.insert({q / 4, Note{60, 100, 1}});
                             });
    const auto pinned = prepared(song, id, FreezePart::Quantize);
    CHECK(pinned.leftInPlace == 1);
    CHECK_FALSE(pinned.quantizeFallback);
    CHECK_FALSE(pinned.settings.quantize.on);
    CHECK(pinned.events == song.events(id));
}

TEST_CASE("Freeze reports the stored-timing fallback when quantise would reverse pedals")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.quantize.on = true;
                                 track.quantize.grid = NoteFraction{1, 4};
                                 track.events.insert({q - q / 8, Note{60, 100, 1}});
                                 track.events.insert({q - q / 16, ControlChange{64, 127}});
                                 track.events.insert({q + q / 8, Note{62, 100, 1}});
                                 track.events.insert({q + q / 8, ControlChange{64, 0}});
                             });
    const auto frozen = prepared(song, id, FreezePart::Quantize);
    CHECK(frozen.quantizeFallback);
    CHECK(frozen.events == song.events(id));
    CHECK_FALSE(frozen.settings.quantize.on);
}

TEST_CASE("Freeze can shift earlier without changing unrelated settings or zero-length notes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 10,
                             [&](EditTrack& track)
                             {
                                 track.mute = true;
                                 track.port = "Synth";
                                 track.quantize.grid = NoteFraction{1, 8};
                                 track.timeOffset = TimeOffset::ticks(-q / 2);
                                 track.events.insert({q, Note{36, 100, 0}});
                             });
    auto expected = *song.track(id);
    expected.timeOffset = {};
    const auto frozen = prepared(song, id, FreezePart::TimeOffset);
    CHECK(frozen.settings == expected);
    CHECK(frozen.events[0] == Event{q / 2, Note{36, 100, 0}});
}
