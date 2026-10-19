#include "support/edit_track.hpp"
#include "support/ppqn.hpp"

#include <modal/core/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;
using std::chrono::nanoseconds;
using Bytes = std::vector<std::uint8_t>;

namespace
{

constexpr std::uint64_t kPlay = 7;
constexpr InputId kInput{0};

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
                  track.arm = true;
                  if (edit)
                  {
                      edit(track);
                  }
              });
    return id;
}

StampedInput stamped(nanoseconds songTime, const Bytes& bytes, InputId input = kInput,
                     std::uint64_t play = kPlay)
{
    StampedInput result;
    result.input = input;
    result.play = play;
    result.songTime = songTime;
    result.chunk.time = 10s + songTime;
    result.chunk.size = static_cast<std::uint8_t>(bytes.size());
    std::ranges::copy(bytes, result.chunk.bytes.begin());
    return result;
}

std::vector<Event> eventsOf(const Song& song, TrackId id)
{
    const auto events = song.events(id).all();
    return {events.begin(), events.end()};
}

Event note(Tick tick, std::uint8_t pitch, Tick duration, std::uint8_t velocity = 100,
           std::uint8_t releaseVelocity = 64)
{
    return {tick, Note{pitch, velocity, duration, releaseVelocity}};
}

Event cc(Tick tick, std::uint8_t controller, std::uint8_t value)
{
    return {tick, ControlChange{controller, value}};
}

} // namespace

TEST_CASE("a take is recorded onto the armed track, at the ticks it was played")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 3);
    const auto unarmed = addTrack(song, 4, [](EditTrack& track) { track.arm = false; });

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0x95, 60, 100})); // input channel 6: rechannelised
    recorder.receive(stamped(250ms, {0x85, 60, 30}));
    recorder.receive(stamped(500ms, {0x90, 62, 90}));
    recorder.receive(stamped(500ms, {0xB0, 7, 100}));
    recorder.receive(stamped(625ms, {0xE0, 0, 0x40}));
    recorder.receive(stamped(750ms, {0x90, 62, 0})); // a note-on at velocity 0 is a note-off
    recorder.receive(stamped(1000ms, {0xC0, 5}));
    recorder.receive(stamped(1000ms, {0xD0, 40}));
    recorder.receive(stamped(1000ms, {0xA0, 62, 50}));
    const Song result = recorder.finish(4 * q);

    CHECK(eventsOf(result, id) == std::vector<Event>{
                                      note(0, 60, q / 2, 100, 30),
                                      cc(q, 7, 100),
                                      note(q, 62, q / 2, 90, 64),
                                      {q + q / 4, PitchBend{0}},
                                      {2 * q, ProgramChange{5}},
                                      {2 * q, ChannelPressure{40}},
                                      {2 * q, PolyPressure{62, 50}},
                                  });
    CHECK(result.track(id)->channel == 3);
    CHECK(result.events(unarmed).empty());
    CHECK(song.events(id).empty()); // the song recorded onto is unchanged
}

TEST_CASE("input from another play, or while stopped, isn't recorded")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0x90, 60, 100}, kInput, 0));
    recorder.receive(stamped(0ms, {0x90, 61, 100}, kInput, kPlay + 1));
    CHECK(eventsOf(recorder.finish(ppqn.whole()), id).empty());
}

TEST_CASE("a track's time offset is taken off, so the take plays back as it was heard")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto late = addTrack(song, 1, [](EditTrack& track) { track.timeOffset = 125ms; });
    const auto early = addTrack(song, 2, [](EditTrack& track) { track.timeOffset = -125ms; });

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(625ms, {0x90, 60, 100}));
    recorder.receive(stamped(875ms, {0x80, 60, 64}));
    const Song result = recorder.finish(4 * q);
    CHECK(eventsOf(result, late) == std::vector<Event>{note(q, 60, q / 2)});
    CHECK(eventsOf(result, early) == std::vector<Event>{note(q + q / 2, 60, q / 2)});
}

TEST_CASE("from a count-in, only what's held at the record point is kept, starting there")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    // Recording from bar 2, with a one-bar count-in (or pre-roll).
    Recorder recorder{song, kPlay, 4 * q};
    recorder.receive(stamped(500ms, {0x90, 60, 100})); // played and let go
    recorder.receive(stamped(750ms, {0x80, 60, 64}));
    recorder.receive(stamped(1000ms, {0xB0, 1, 50})); // other controllers are dropped
    recorder.receive(stamped(1500ms, {0xB0, 64, 127}));
    recorder.receive(stamped(1750ms, {0x90, 64, 80})); // still held at the record point
    recorder.receive(stamped(1875ms, {0xB0, 66, 127}));
    recorder.receive(stamped(1900ms, {0xB0, 66, 0})); // let go again
    recorder.receive(stamped(1950ms, {0x90, 65, 70}));
    recorder.receive(stamped(2000ms, {0x80, 65, 64})); // released exactly at the record point
    recorder.receive(stamped(2500ms, {0x80, 64, 64}));
    recorder.receive(stamped(2500ms, {0xB0, 64, 0}));

    CHECK(eventsOf(recorder.finish(8 * q), id) == std::vector<Event>{
                                                      cc(4 * q, 64, 127),
                                                      note(4 * q, 64, q, 80),
                                                      cc(5 * q, 64, 0),
                                                  });
}

TEST_CASE("input before the song's start is a count-in to bar 1")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(-500ms, {0x90, 60, 100}));
    recorder.receive(stamped(-250ms, {0x80, 60, 64})); // dropped
    recorder.receive(stamped(-125ms, {0x90, 62, 100}));
    recorder.receive(stamped(250ms, {0x80, 62, 64}));
    CHECK(eventsOf(recorder.finish(4 * q), id) == std::vector<Event>{note(0, 62, q / 2)});
}

TEST_CASE("notes and pedals still held when recording stops end there")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0xB0, 64, 100}));
    recorder.receive(stamped(0ms, {0x90, 60, 100}));
    recorder.receive(stamped(250ms, {0x90, 62, 100}));
    recorder.receive(stamped(750ms, {0x80, 62, 64}));  // after the stop
    recorder.receive(stamped(500ms, {0x90, 64, 100})); // at the stop: not recorded
    recorder.receive(stamped(500ms, {0xB0, 7, 100}));

    CHECK(eventsOf(recorder.finish(q), id) == std::vector<Event>{
                                                  cc(0, 64, 100),
                                                  note(0, 60, q),
                                                  note(q / 2, 62, q / 2),
                                                  cc(q, 64, 0),
                                              });
}

TEST_CASE("replacing removes what started in the recorded range, cutting notes running in")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.events.insert(note(0, 50, 6 * q)); // runs into the range
                                 track.events.insert(note(q, 51, q));     // ends before it
                                 track.events.insert(cc(4 * q, 7, 10));
                                 track.events.insert(note(5 * q, 52, 4 * q)); // starts in it
                                 track.events.insert(note(8 * q, 53, q));     // at the stop
                             });

    Recorder recorder{song, kPlay, 4 * q};
    recorder.receive(stamped(2500ms, {0x90, 60, 100}));
    recorder.receive(stamped(3000ms, {0x80, 60, 64}));
    CHECK(eventsOf(recorder.finish(8 * q), id) == std::vector<Event>{
                                                      note(0, 50, 4 * q),
                                                      note(q, 51, q),
                                                      note(5 * q, 60, q),
                                                      note(8 * q, 53, q),
                                                  });
}

TEST_CASE("replacing patches the old pedals at both edges of the recorded range")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.events.insert(cc(0, 64, 127)); // down at the record point
                                 track.events.insert(cc(5 * q, 64, 0));
                                 track.events.insert(cc(7 * q, 66, 90)); // down at the stop
                                 track.events.insert(cc(9 * q, 66, 0));
                                 track.events.insert(cc(q, 67, 127)); // up again before the range
                                 track.events.insert(cc(2 * q, 67, 0));
                             });

    Recorder recorder{song, kPlay, 4 * q};
    recorder.receive(stamped(2500ms, {0x90, 60, 100}));
    recorder.receive(stamped(3000ms, {0x80, 60, 64}));
    CHECK(eventsOf(recorder.finish(8 * q), id) == std::vector<Event>{
                                                      cc(0, 64, 127),
                                                      cc(q, 67, 127),
                                                      cc(2 * q, 67, 0),
                                                      cc(4 * q, 64, 0),
                                                      note(5 * q, 60, q),
                                                      cc(8 * q, 66, 90),
                                                      cc(9 * q, 66, 0),
                                                  });
}

TEST_CASE("a pedal held into the record point follows the old pedal's patch")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id =
        addTrack(song, 1, [&](EditTrack& track) { track.events.insert(cc(0, 64, 127)); });

    Recorder recorder{song, kPlay, 4 * q};
    recorder.receive(stamped(1500ms, {0xB0, 64, 100}));
    recorder.receive(stamped(2500ms, {0xB0, 64, 0}));
    CHECK(eventsOf(recorder.finish(8 * q), id) == std::vector<Event>{
                                                      cc(0, 64, 127),
                                                      cc(4 * q, 64, 0),
                                                      cc(4 * q, 64, 100),
                                                      cc(5 * q, 64, 0),
                                                      cc(8 * q, 64, 127),
                                                  });
}

TEST_CASE("overdubbing keeps what was there, ending overlapping notes of one pitch")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1,
                             [&](EditTrack& track)
                             {
                                 track.events.insert(note(0, 60, 2 * q)); // the take's note cuts it
                                 track.events.insert(note(3 * q, 62, q)); // cuts the take's note
                                 track.events.insert(note(4 * q, 64, q)); // the take's note wins
                                 track.events.insert(note(4 * q, 65, q, 50)); // another pitch
                                 track.events.insert(cc(0, 64, 127));
                             });

    Recorder recorder{song, kPlay, 0, RecordMode::Overdub};
    recorder.receive(stamped(500ms, {0x90, 60, 100}));
    recorder.receive(stamped(1000ms, {0x90, 62, 100}));
    recorder.receive(stamped(2000ms, {0x90, 64, 90}));
    recorder.receive(stamped(2000ms, {0xB0, 64, 127}));
    recorder.receive(stamped(2250ms, {0x80, 60, 64}));
    recorder.receive(stamped(2250ms, {0x80, 62, 64}));
    recorder.receive(stamped(2250ms, {0x80, 64, 64}));

    // The take's pedal is let go at the stop, so the old one goes down again.
    CHECK(eventsOf(recorder.finish(6 * q), id) == std::vector<Event>{
                                                      cc(0, 64, 127),
                                                      note(0, 60, q),
                                                      note(q, 60, 3 * q + q / 2),
                                                      note(2 * q, 62, q),
                                                      note(3 * q, 62, q),
                                                      cc(4 * q, 64, 127),
                                                      note(4 * q, 65, q, 50),
                                                      note(4 * q, 64, q / 2, 90),
                                                      cc(6 * q, 64, 0),
                                                      cc(6 * q, 64, 127),
                                                  });
}

TEST_CASE("a repeated note ends the one already sounding on that pitch")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0x90, 60, 100}, InputId{0}));
    recorder.receive(stamped(500ms, {0x91, 60, 90}, InputId{1})); // the same key elsewhere
    recorder.receive(stamped(750ms, {0x80, 60, 64}, InputId{0})); // ended already: ignored
    recorder.receive(stamped(1000ms, {0x81, 60, 64}, InputId{1}));
    recorder.receive(stamped(1500ms, {0x90, 62, 100}));
    recorder.receive(stamped(1500ms, {0x90, 62, 80})); // at the same tick: replaces it
    recorder.receive(stamped(2000ms, {0x80, 62, 64}));
    CHECK(eventsOf(recorder.finish(8 * q), id) == std::vector<Event>{
                                                      note(0, 60, q),
                                                      note(q, 60, q, 90),
                                                      note(3 * q, 62, q, 80),
                                                  });
}

TEST_CASE("a note whose note-off comes on the same tick lasts one tick")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0x90, 60, 100}));
    recorder.receive(stamped(0ms, {0x80, 60, 64}));
    CHECK(eventsOf(recorder.finish(ppqn.whole()), id) == std::vector<Event>{note(0, 60, 1)});
}

TEST_CASE("every armed track gets the take, on its own channel")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto a = addTrack(song, 1);
    const auto b = addTrack(song, 10);

    Recorder recorder{song, kPlay, 0};
    recorder.receive(stamped(0ms, {0x90, 60, 100}));
    recorder.receive(stamped(500ms, {0x80, 60, 64}));
    const Song result = recorder.finish(4 * q);
    CHECK(eventsOf(result, a) == std::vector<Event>{note(0, 60, q)});
    CHECK(eventsOf(result, b) == std::vector<Event>{note(0, 60, q)});
}

TEST_CASE("a SysEx is recorded whole from its chunks, and one cut short is dropped")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id = addTrack(song, 1);

    Bytes sysEx(InputChunk::kMaxBytes + 4, 0x01);
    sysEx.front() = 0xF0;
    sysEx.back() = 0xF7;
    const auto chunks = [&](nanoseconds songTime, InputId input)
    {
        auto first =
            stamped(songTime, Bytes(sysEx.begin(), sysEx.begin() + InputChunk::kMaxBytes), input);
        first.chunk.more = true;
        auto rest = stamped(songTime + 1ms,
                            Bytes(sysEx.begin() + InputChunk::kMaxBytes, sysEx.end()), input);
        rest.first = false;
        return std::pair{first, rest};
    };

    Recorder recorder{song, kPlay, 0};
    const auto [a, aRest] = chunks(500ms, InputId{0});
    const auto [b, bRest] = chunks(1000ms, InputId{1});
    recorder.receive(a);
    recorder.receive(b); // from another input, in between
    recorder.receive(aRest);
    recorder.receive(stamped(1250ms, {0x90, 60, 100}, InputId{1})); // b was cut short
    recorder.receive(bRest);                                        // so this is ignored
    recorder.receive(stamped(1500ms, {0x80, 60, 64}, InputId{1}));

    const auto events = eventsOf(recorder.finish(4 * q), id);
    REQUIRE(events.size() == 2);
    CHECK(events[0] == Event{q, makeSysEx(sysEx)});
    CHECK(events[1] == note(2 * q + q / 2, 60, q / 2));
}

TEST_CASE("stopping at or before the record point leaves the song unchanged")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song = songAt(ppqn);
    const auto id =
        addTrack(song, 1, [&](EditTrack& track) { track.events.insert(note(4 * q, 50, q)); });

    Recorder recorder{song, kPlay, 4 * q};
    recorder.receive(stamped(1500ms, {0x90, 60, 100}));
    CHECK(eventsOf(recorder.finish(4 * q), id) == std::vector<Event>{note(4 * q, 50, q)});
}
