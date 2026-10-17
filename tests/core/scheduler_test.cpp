#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

using namespace modal::core;
using namespace std::chrono_literals;
using namespace modal::test;
using std::chrono::nanoseconds;

namespace
{

// An arbitrary time on the engine's clock to start playing at, so times relative to it show.
constexpr nanoseconds kNow = 1000s;

// Calls sendDue() at each wake time up to the end, as the engine would.
void runUntil(Scheduler& scheduler, nanoseconds end)
{
    while (const auto wake = scheduler.nextWakeTime())
    {
        if (*wake > end)
        {
            return;
        }
        scheduler.sendDue(*wake);
    }
}

using Bytes = std::vector<std::uint8_t>;

} // namespace

TEST_CASE("events are sent at their times, and note-offs at the notes' ends")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, Note{60, 100, q, 70}});
                 track.events.insert({q, ControlChange{7, 90}});
                 track.events.insert({2 * q, Note{62, 80, q / 2}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);

    CHECK(scheduler.nextWakeTime() == kNow);
    scheduler.sendDue(kNow + 499ms);
    CHECK(sink.sent == std::vector<Sent>{{{1}, {0x90, 60, 100}, kNow}});
    CHECK(scheduler.nextWakeTime() == kNow + 500ms);
    CHECK(scheduler.soundingNotes() == 1);

    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{1}, {0x80, 60, 70}, kNow + 500ms},
                           {{1}, {0xB0, 7, 90}, kNow + 500ms},
                           {{1}, {0x90, 62, 80}, kNow + 1000ms},
                           {{1}, {0x80, 62, 64}, kNow + 1250ms},
                       });
    CHECK(scheduler.finished());
    CHECK(scheduler.nextWakeTime() == std::nullopt);
}

TEST_CASE("events sent late keep their order and their intended times")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 2,
             [&](Track& track)
             {
                 track.events.insert({0, Note{60, 100, q}});
                 track.events.insert({q / 2, ProgramChange{3}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 2s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x91, 60, 100}, kNow},
                           {{1}, {0xC1, 3}, kNow + 250ms},
                           {{1}, {0x81, 60, 64}, kNow + 500ms},
                       });
}

TEST_CASE("a note ending as the same pitch starts is ended first")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, Note{60, 100, q}});
                 track.events.insert({q, Note{60, 90, q}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.messages() == std::vector<Bytes>{
                                 {0x90, 60, 100},
                                 {0x80, 60, 64},
                                 {0x90, 60, 90},
                                 {0x80, 60, 64},
                             });
}

TEST_CASE("overlapping notes retrigger and end at the latest end")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, Note{60, 100, 4 * q, 10}});
                 track.events.insert({q, Note{60, 90, q, 20}});         // inside the first
                 track.events.insert({3 * q, Note{60, 80, 2 * q, 30}}); // past its end
             });
    // A track on another channel doesn't overlap with them.
    addTrack(song, 2, [&](Track& track) { track.events.insert({q, Note{60, 70, q}}); });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{1}, {0x80, 60, 10}, kNow + 500ms},
                           {{1}, {0x90, 60, 90}, kNow + 500ms},
                           {{1}, {0x91, 60, 70}, kNow + 500ms},
                           {{1}, {0x81, 60, 64}, kNow + 1000ms},
                           {{1}, {0x80, 60, 10}, kNow + 1500ms},
                           {{1}, {0x90, 60, 80}, kNow + 1500ms},
                           {{1}, {0x80, 60, 30}, kNow + 2500ms},
                       });
}

TEST_CASE("notes on different destinations don't overlap")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, Note{60, 100, 2 * q}}); });
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.port = "2";
                 track.events.insert({q, Note{60, 100, q}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{2}, {0x90, 60, 100}, kNow + 500ms},
                           {{1}, {0x80, 60, 64}, kNow + 1000ms},
                           {{2}, {0x80, 60, 64}, kNow + 1000ms},
                       });
}

TEST_CASE("SysEx is sent whole")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const Bytes bytes{0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, makeSysEx(bytes)}); });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    CHECK(sink.messages() == std::vector<Bytes>{bytes});
}

TEST_CASE("starting mid-song sends the chased events first, then plays from the start")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, ProgramChange{4}});
                 track.events.insert({0, Note{48, 100, 3 * q}});
                 track.events.insert({q, Note{50, 100, q / 2}});
                 track.events.insert({2 * q, Note{52, 100, q}});
             });
    // Sent after the start point, but stored before it, so it's chased rather than played.
    addTrack(song, 2,
             [&](Track& track)
             {
                 track.timeOffset = 100ms;
                 track.events.insert({2 * q - 1, ControlChange{1, 64}});
             });
    const auto plan = compilePlan(song, byPortName);
    const auto chased = chase(plan, 2 * q);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(2 * q, chased, kNow);
    CHECK(scheduler.nextWakeTime() == kNow);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0xC0, 4}, kNow},
                           {{1}, {0x90, 48, 100}, kNow},
                           {{1}, {0xB1, 1, 64}, kNow},
                           {{1}, {0x90, 52, 100}, kNow},
                           {{1}, {0x80, 48, 64}, kNow + 500ms},
                           {{1}, {0x80, 52, 64}, kNow + 500ms},
                       });
}

TEST_CASE("a negative time offset starts playback early by the lead-in")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({q, Note{60, 100, q}}); });
    addTrack(song, 2,
             [&](Track& track)
             {
                 track.timeOffset = -40ms;
                 track.events.insert({q, Note{64, 100, q}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(q, chase(plan, q), kNow);
    CHECK(scheduler.position(kNow) == plan.tempo.toTick(500ms - 40ms));
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x91, 64, 100}, kNow},
                           {{1}, {0x90, 60, 100}, kNow + 40ms},
                           {{1}, {0x81, 64, 64}, kNow + 500ms},
                           {{1}, {0x80, 60, 64}, kNow + 540ms},
                       });
}

TEST_CASE("the position during a lead-in from the song start is tick 0")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.timeOffset = -40ms;
                 track.events.insert({0, Note{60, 100, ppqn.quarter()}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    CHECK(scheduler.position(kNow) == std::nullopt);
    scheduler.play(0, {}, kNow);
    CHECK(scheduler.position(kNow) == 0);
    CHECK(scheduler.position(kNow + 39ms) == 0);
    CHECK(scheduler.position(kNow + 540ms) == ppqn.quarter());
    scheduler.sendDue(kNow);
    CHECK(sink.sent == std::vector<Sent>{{{1}, {0x90, 60, 100}, kNow}});
}

TEST_CASE("ticks for input during a lead-in from the song start are before tick 0")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.timeOffset = -40ms; });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    CHECK(scheduler.tickAt(kNow) == std::nullopt);
    scheduler.play(0, {}, kNow);
    CHECK(scheduler.tickAt(kNow) == plan.tempo.toTickExtended(-40ms));
    CHECK(scheduler.tickAt(kNow) < 0);
    CHECK(scheduler.tickAt(kNow + 40ms) == 0);
    CHECK(scheduler.tickAt(kNow + 540ms) == ppqn.quarter());
    CHECK(scheduler.songTimeAt(kNow) == -40ms);
    CHECK(scheduler.songTimeAt(kNow + 540ms) == 500ms);
    scheduler.stop(kNow + 1s);
    CHECK(scheduler.tickAt(kNow + 1s) == std::nullopt);
    CHECK(scheduler.songTimeAt(kNow + 1s) == std::nullopt);
}

TEST_CASE("stop ends the notes sounding, with optional All Notes Off and sustain reset")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, Note{60, 100, 4 * q}}); });
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, Note{64, 100, 4 * q}}); });
    addTrack(song, 3,
             [&](Track& track)
             {
                 track.port = "2";
                 track.events.insert({4 * q, Note{67, 100, q}});
             });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);

    SECTION("by default, only note-offs")
    {
        scheduler.play(0, {}, kNow);
        scheduler.sendDue(kNow + 1s);
        sink.sent.clear();
        scheduler.stop(kNow + 1200ms);
        CHECK(sink.sent == std::vector<Sent>{
                               {{1}, {0x80, 64, 64}, kNow + 1200ms},
                               {{1}, {0x80, 60, 64}, kNow + 1200ms},
                           });
    }

    SECTION("with both options, once for each channel the plan uses")
    {
        scheduler.setStopOptions({.allNotesOff = true, .resetSustain = true});
        scheduler.play(0, {}, kNow);
        scheduler.sendDue(kNow + 1s);
        sink.sent.clear();
        scheduler.stop(kNow + 1200ms);
        CHECK(sink.messages() == std::vector<Bytes>{
                                     {0x80, 64, 64},
                                     {0x80, 60, 64},
                                     {0xB0, 64, 0},
                                     {0xB0, 123, 0},
                                     {0xB2, 64, 0},
                                     {0xB2, 123, 0},
                                 });
        CHECK(sink.sent[4].destination == DestinationId{2});
    }

    CHECK_FALSE(scheduler.isPlaying());
    CHECK(scheduler.soundingNotes() == 0);
    CHECK(scheduler.nextWakeTime() == std::nullopt);
    sink.sent.clear();
    scheduler.sendDue(kNow + 10s);
    scheduler.stop(kNow + 10s);
    CHECK(sink.sent.empty());
}

TEST_CASE("panic resets every channel of every destination any plan has used")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](Track& track)
                             {
                                 track.port = "5";
                                 track.events.insert({0, Note{60, 100, ppqn.quarter()}});
                             });
    const auto first = compilePlan(song, byPortName);
    song.updateTrack(id, [](Track& track) { track.port = "6"; });
    const auto second = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(first, kNow);
    scheduler.setPlan(second, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    sink.sent.clear();
    scheduler.panic(kNow + 1ms);

    REQUIRE(sink.sent.size() == 1 + 2 * 16 * 2);
    CHECK(sink.sent[0] == Sent{{6}, {0x80, 60, 64}, kNow + 1ms});
    std::size_t i = 1;
    for (const DestinationId destination : {DestinationId{5}, DestinationId{6}})
    {
        for (std::uint8_t channel = 0; channel < 16; ++channel)
        {
            const auto status = static_cast<std::uint8_t>(0xB0 | channel);
            CHECK(sink.sent[i++] == Sent{destination, {status, 123, 0}, kNow + 1ms});
            CHECK(sink.sent[i++] == Sent{destination, {status, 121, 0}, kNow + 1ms});
        }
    }
    CHECK_FALSE(scheduler.isPlaying());
}

TEST_CASE("playing again while playing ends the notes sounding first")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, Note{60, 100, 4 * q}}); });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    scheduler.play(0, {}, kNow + 1s);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{1}, {0x80, 60, 64}, kNow + 1s},
                           {{1}, {0x90, 60, 100}, kNow + 1s},
                           {{1}, {0x80, 60, 64}, kNow + 3s},
                       });
}

TEST_CASE("a new plan during playback applies to the events not yet sent")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto kept = addTrack(song, 1,
                               [&](Track& track)
                               {
                                   track.events.insert({0, Note{60, 100, 4 * q}});
                                   track.events.insert({2 * q, Note{62, 100, q}});
                               });
    const auto muted =
        addTrack(song, 2, [&](Track& track) { track.events.insert({0, Note{64, 100, 4 * q}}); });
    const auto before = compilePlan(song, byPortName);

    // Halfway through the second beat: lengthen the sounding note (which still ends as
    // planned), move the later note and add one before the position, and mute a track.
    song.updateTrack(kept,
                     [&](Track& track)
                     {
                         track.events = {};
                         track.events.insert({0, Note{60, 100, 8 * q}});
                         track.events.insert({q, Note{65, 100, q}});
                         track.events.insert({3 * q, Note{62, 100, q}});
                     });
    song.updateTrack(muted, [](Track& track) { track.mute = true; });
    const auto after = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(before, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    scheduler.setPlan(after, kNow + 750ms);
    CHECK(scheduler.position(kNow + 750ms) == q + q / 2);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{1}, {0x91, 64, 100}, kNow},
                           {{1}, {0x81, 64, 64}, kNow + 750ms},
                           {{1}, {0x90, 62, 100}, kNow + 1500ms},
                           {{1}, {0x80, 60, 64}, kNow + 2000ms},
                           {{1}, {0x80, 62, 64}, kNow + 2000ms},
                       });
}

TEST_CASE("a tempo change during playback keeps the position and re-times what's left")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, Note{60, 100, 2 * q}});
                 track.events.insert({2 * q, Note{62, 100, q}});
             });
    addTrack(song, 2,
             [&](Track& track)
             {
                 track.timeOffset = 10ms;
                 track.events.insert({0, Note{64, 100, 2 * q}});
             });
    const auto before = compilePlan(song, byPortName);
    song.tempo().set(0, Tempo{1'000'000}); // half speed: a quarter note is now a second
    const auto after = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(before, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 250ms); // an eighth note in
    scheduler.setPlan(after, kNow + 250ms);
    CHECK(scheduler.position(kNow + 250ms) == q / 2);
    CHECK(scheduler.position(kNow + 750ms) == q);
    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0x90, 60, 100}, kNow},
                           {{1}, {0x91, 64, 100}, kNow + 10ms},
                           {{1}, {0x80, 60, 64}, kNow + 1750ms},
                           {{1}, {0x90, 62, 100}, kNow + 1750ms},
                           {{1}, {0x81, 64, 64}, kNow + 1760ms},
                           {{1}, {0x80, 62, 64}, kNow + 2750ms},
                       });
}

TEST_CASE("note-ons beyond the table of sounding notes aren't sent")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    // Three destinations of 16 channels with 128 pitches each: 6,144 notes at once.
    for (int port = 1; port <= 3; ++port)
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            addTrack(song, channel,
                     [&](Track& track)
                     {
                         track.port = std::to_string(port);
                         for (std::uint8_t pitch = 0; pitch < 128; ++pitch)
                         {
                             track.events.insert({0, Note{pitch, 100, ppqn.quarter()}});
                         }
                     });
        }
    }
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    CHECK(scheduler.soundingNotes() == Scheduler::kMaxSoundingNotes);
    CHECK(scheduler.droppedNotes() == 3 * 16 * 128 - Scheduler::kMaxSoundingNotes);
    CHECK(sink.sent.size() == Scheduler::kMaxSoundingNotes);

    runUntil(scheduler, kNow + 10s);
    CHECK(sink.sent.size() == 2 * Scheduler::kMaxSoundingNotes);
    CHECK(scheduler.finished());
}

TEST_CASE("without a plan, nothing plays")
{
    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.play(0, {}, kNow);
    CHECK_FALSE(scheduler.isPlaying());
    scheduler.panic(kNow);
    CHECK(sink.sent.empty());
}

namespace
{

constexpr DestinationId kClick{9};

// The song's plan with the metronome on, to destination 9, by default for recording.
PlaybackPlan withMetronome(Song song, bool recording = true)
{
    song.metronome.when = MetronomeSettings::When::Always;
    return compilePlan(song, byPortName, {.recording = recording, .metronomeDestination = kClick});
}

// A metronome click (channel 10) and its note-off, by default 1/32 later at 120 BPM.
std::vector<Sent> click(std::uint8_t pitch, std::uint8_t velocity, nanoseconds at,
                        nanoseconds length = 62500us)
{
    return {{kClick, {0x99, pitch, velocity}, at}, {kClick, {0x89, pitch, 64}, at + length}};
}

std::vector<Sent> clicks(std::initializer_list<std::vector<Sent>> parts)
{
    std::vector<Sent> result;
    for (const auto& part : parts)
    {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

std::vector<Sent> sentTo(const RecordingSink& sink, DestinationId destination)
{
    std::vector<Sent> result;
    std::ranges::copy_if(sink.sent, std::back_inserter(result),
                         [&](const Sent& sent) { return sent.destination == destination; });
    return result;
}

} // namespace

TEST_CASE("the metronome clicks on each beat, accenting the first of each bar")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto plan = withMetronome(songAt(ppqn));

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 2100ms);
    CHECK(sink.sent == clicks({
                           click(76, 110, kNow),
                           click(77, 80, kNow + 500ms),
                           click(77, 80, kNow + 1000ms),
                           click(77, 80, kNow + 1500ms),
                           click(76, 110, kNow + 2000ms),
                       }));
    CHECK_FALSE(scheduler.finished()); // when recording, it carries on past the song's end
}

TEST_CASE("when not recording, the metronome stops when the song ends")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, Note{60, 100, 2 * q}}); });
    const auto plan = withMetronome(song, false);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 10s);
    CHECK(sentTo(sink, kClick) == clicks({
                                      click(76, 110, kNow),
                                      click(77, 80, kNow + 500ms),
                                  })); // none with the note-off at 1000 ms
    CHECK(scheduler.finished());
    CHECK(scheduler.nextWakeTime() == std::nullopt);
}

TEST_CASE("in 6/8 the metronome clicks on every eighth")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1, [](Track& track) { track.meter.set(0, {6, 8}); });
    const auto plan = withMetronome(song);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 1600ms);
    CHECK(sink.sent == clicks({
                           click(76, 110, kNow),
                           click(77, 80, kNow + 250ms),
                           click(77, 80, kNow + 500ms),
                           click(77, 80, kNow + 750ms),
                           click(77, 80, kNow + 1000ms),
                           click(77, 80, kNow + 1250ms),
                           click(76, 110, kNow + 1500ms),
                       }));
}

TEST_CASE("starting mid-beat, the metronome first clicks on the next beat")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto plan = withMetronome(songAt(ppqn));

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(q + q / 2, {}, kNow);
    runUntil(scheduler, kNow + 1400ms);
    CHECK(sink.sent == clicks({
                           click(77, 80, kNow + 250ms),
                           click(77, 80, kNow + 750ms),
                           click(76, 110, kNow + 1250ms),
                       }));
}

TEST_CASE("a click ends by the next beat when the beat is shorter")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1, [](Track& track) { track.meter.set(0, {4, 64}); });
    const auto plan = withMetronome(song);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    runUntil(scheduler, kNow + 62500us);
    CHECK(sink.sent == clicks({
                           click(76, 110, kNow, 31250us),
                           click(77, 80, kNow + 31250us, 31250us),
                           {{kClick, {0x99, 77, 80}, kNow + 62500us}}, // after the note-off
                       }));
}

TEST_CASE("the metronome isn't offset with the tracks")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.timeOffset = -100ms;
                 track.events.insert({0, Note{60, 100, ppqn.quarter() / 4}});
             });
    const auto plan = withMetronome(song);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow); // with a lead-in of 100 ms
    runUntil(scheduler, kNow + 200ms);
    CHECK(sentTo(sink, DestinationId{1}).front() == Sent{{1}, {0x90, 60, 100}, kNow});
    CHECK(sentTo(sink, kClick) == click(76, 110, kNow + 100ms));
}

TEST_CASE("stopping ends the click sounding, and the metronome")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto plan = withMetronome(songAt(ppqn));

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 10ms);
    scheduler.stop(kNow + 10ms);
    CHECK(sink.sent == click(76, 110, kNow, 10ms));
    CHECK(scheduler.nextWakeTime() == std::nullopt);
}

TEST_CASE("after a tempo change the metronome carries on from the next beat")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto before = withMetronome(song);
    song.tempo().set(0, Tempo{1'000'000}); // 60 BPM
    const auto after = withMetronome(song);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(before, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 531250us); // the second click is sounding, a 1/64 into its beat
    scheduler.setPlan(after, kNow + 531250us);
    runUntil(scheduler, kNow + 2000ms);

    // A 1/64 into the second beat is 31.25 ms at 120 BPM and 62.5 ms at 60, so the click
    // sounding ends 125 - 62.5 ms later, and the next beat comes 1000 - 62.5 ms later.
    CHECK(sink.sent == clicks({
                           click(76, 110, kNow),
                           click(77, 80, kNow + 500ms, 93750us),
                           click(77, 80, kNow + 1468750us, 125ms),
                       }));
}

TEST_CASE("dropping the metronome mid-play ends its click, and panic reaches its port")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto clicking = withMetronome(song);
    const auto quiet = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(clicking, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 10ms);
    scheduler.setPlan(quiet, kNow + 10ms);
    CHECK(sink.sent == click(76, 110, kNow, 10ms));
    CHECK(scheduler.nextWakeTime() == std::nullopt);

    sink.sent.clear();
    scheduler.panic(kNow + 20ms);
    CHECK(std::ranges::count(sink.sent, kClick, &Sent::destination) == 32);
}

TEST_CASE("stop lets up each pedal playback left down, on just its channel")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, ControlChange{64, 100}}); // a half pedal is down
                 track.events.insert({0, ControlChange{67, 127}});
                 track.events.insert({q, ControlChange{67, 0}}); // up again
             });
    addTrack(song, 2,
             [&](Track& track)
             {
                 track.port = "2";
                 track.events.insert({0, ControlChange{66, 127}});
                 track.events.insert({0, ControlChange{7, 127}}); // not a pedal
             });
    addTrack(song, 3, [&](Track& track) { track.events.insert({0, ControlChange{64, 20}}); });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow + 1s);
    sink.sent.clear();
    scheduler.stop(kNow + 1s);
    CHECK(sink.sent == std::vector<Sent>{
                           {{1}, {0xB0, 64, 0}, kNow + 1s},
                           {{2}, {0xB1, 66, 0}, kNow + 1s},
                       });

    sink.sent.clear();
    scheduler.play(2 * q, {}, kNow + 2s);
    scheduler.stop(kNow + 2s);
    CHECK(sink.sent.empty()); // let up already
}

TEST_CASE("chased pedals are let up on stop, and restarting lets pedals up first")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
             {
                 track.events.insert({0, ControlChange{64, 127}});
                 track.events.insert({4 * q, ControlChange{64, 0}});
             });
    const auto plan = compilePlan(song, byPortName);
    const auto chased = chase(plan, 2 * q);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(2 * q, chased, kNow);
    scheduler.sendDue(kNow);
    CHECK(sink.messages() == std::vector<Bytes>{{0xB0, 64, 127}});

    sink.sent.clear();
    scheduler.play(5 * q, {}, kNow + 100ms);
    CHECK(sink.messages() == std::vector<Bytes>{{0xB0, 64, 0}});

    sink.sent.clear();
    scheduler.stop(kNow + 200ms);
    CHECK(sink.sent.empty());
}

TEST_CASE("after a panic, stop has no pedals to let up")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    addTrack(song, 1, [&](Track& track) { track.events.insert({0, ControlChange{64, 127}}); });
    const auto plan = compilePlan(song, byPortName);

    RecordingSink sink;
    Scheduler scheduler{sink};
    scheduler.setPlan(plan, kNow);
    scheduler.play(0, {}, kNow);
    scheduler.sendDue(kNow);
    scheduler.panic(kNow + 1ms);
    scheduler.play(0, {}, kNow + 2ms);
    sink.sent.clear();
    scheduler.stop(kNow + 2ms); // before the pedal is sent again
    CHECK(sink.sent.empty());
}
