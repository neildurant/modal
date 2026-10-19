#include "support/edit_track.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/count_in.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;

namespace
{

constexpr DestinationId kClick{9};
using Mode = CountInSettings::Mode;

std::vector<bool> accentsOf(const PlannedCountIn& countIn)
{
    std::vector<bool> result;
    for (const auto& click : countIn.clicks)
    {
        result.push_back(click.accent);
    }
    return result;
}

} // namespace

TEST_CASE("by default, a one-bar count-in clicks each beat before the record point")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar2 = ppqn.whole();
    const Song song = songAt(ppqn);

    const auto start = planRecordStart(song, bar2, kClick);
    CHECK(start.playFrom == bar2);
    REQUIRE(start.countIn);
    const auto& countIn = *start.countIn;
    CHECK(countIn.destination == kClick);
    CHECK(countIn.channel == 9);
    CHECK(countIn.accentNote == 76);
    CHECK(countIn.accentVelocity == 110);
    CHECK(countIn.beatNote == 77);
    CHECK(countIn.beatVelocity == 80);
    CHECK(countIn.clickLength == 62500us);
    CHECK(countIn.length == 2s);
    CHECK(countIn.clicks == std::vector<CountInClick>{
                                {2000ms, true},
                                {1500ms, false},
                                {1000ms, false},
                                {500ms, false},
                            });
}

TEST_CASE("a count-in uses the tempo and meter at the record point")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1);
    song.updateMeter([&](MeterMap& meter) { meter.set(4 * q, {6, 8}); });
    song.updateTempo([&](TempoMap& tempo)
                     { tempo.set(4 * q, Tempo{1'000'000}); }); // 60 BPM from bar 2
    song.countIn.bars = 2;

    const auto start = planRecordStart(song, 4 * q, kClick);
    REQUIRE(start.countIn);
    CHECK(start.countIn->length == 6s);
    CHECK(start.countIn->clicks.size() == 12);
    CHECK(start.countIn->clicks.front().before == 6s);
    CHECK(start.countIn->clicks.back().before == 500ms);
    CHECK(start.countIn->clickLength == 125ms);
    CHECK(accentsOf(*start.countIn) == std::vector<bool>{true, false, false, false, false, false,
                                                         true, false, false, false, false, false});
}

TEST_CASE("from mid-bar, a count-in's accents fall where bars would start")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Song song = songAt(ppqn);

    // From beat 3 of 4/4: beats 3, 4, 1, 2.
    const auto start = planRecordStart(song, ppqn.whole() + 2 * q, kClick);
    REQUIRE(start.countIn);
    CHECK(accentsOf(*start.countIn) == std::vector<bool>{false, false, true, false});
}

TEST_CASE("a custom count-in is a number of beats")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    song.countIn.beats = 3;

    const auto start = planRecordStart(song, 0, kClick); // before bar 1
    REQUIRE(start.countIn);
    CHECK(start.countIn->clicks == std::vector<CountInClick>{
                                       {1500ms, false},
                                       {1000ms, false},
                                       {500ms, false},
                                   });
}

TEST_CASE("a click is cut short where the count-in's beat is shorter")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1);
    song.updateMeter([&](MeterMap& meter) { meter.set(0, {4, 64}); });

    const auto start = planRecordStart(song, 0, kClick);
    REQUIRE(start.countIn);
    CHECK(start.countIn->clickLength == 31250us);
}

TEST_CASE("without a count-in, or a metronome port, recording starts at the record point")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const Tick bar2 = ppqn.whole();

    const auto noPort = planRecordStart(song, bar2, std::nullopt);
    CHECK(noPort.playFrom == bar2);
    CHECK_FALSE(noPort.countIn);

    song.countIn.mode = Mode::None;
    const auto none = planRecordStart(song, bar2, kClick);
    CHECK(none.playFrom == bar2);
    CHECK_FALSE(none.countIn);

    song.countIn.mode = Mode::CountIn;
    song.countIn.bars = 0;
    CHECK_FALSE(planRecordStart(song, bar2, kClick).countIn);
}

TEST_CASE("a pre-roll starts bars before the record point, but not before the song")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1);
    song.updateMeter([&](MeterMap& meter) { meter.set(8 * q, {3, 4}); });
    song.countIn.mode = Mode::PreRoll;

    CHECK(song.countIn.preRollBars == 1);
    const auto fromBar4 = planRecordStart(song, 14 * q, kClick); // bar 5 (3/4 from bar 3)
    CHECK(fromBar4.playFrom == 11 * q);
    CHECK_FALSE(fromBar4.countIn);
    CHECK(planRecordStart(song, 15 * q, kClick).playFrom == 12 * q); // keeps the beat
    CHECK(planRecordStart(song, 8 * q, kClick).playFrom == 4 * q);   // a 4/4 bar back

    song.countIn.preRollBars = 2;
    CHECK(planRecordStart(song, 4 * q, kClick).playFrom == 0); // only one bar to go back
    CHECK(planRecordStart(song, 0, kClick).playFrom == 0);
}
