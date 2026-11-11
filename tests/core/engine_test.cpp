// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/edit_track.hpp"
#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/engine.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;
using std::chrono::nanoseconds;

namespace
{

// One note per quarter, starting on the beat, a quarter note long: pitches 60, 62, 64, 65.
std::shared_ptr<const PlaybackPlan> fourNotes(Ppqn ppqn)
{
    Song song = songAt(ppqn);
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
    return std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName));
}

} // namespace

TEST_CASE("the engine plays a plan from a start tick, and reports its progress")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};

    CHECK(engine.status().position == std::nullopt);
    REQUIRE(engine.setPlan(fourNotes(ppqn)));
    REQUIRE(engine.play(ppqn.quarter()));
    CHECK_FALSE(engine.status().playing); // until the engine thread gets to it

    // The first note goes at once; the next event is 500 ms on.
    CHECK(engine.step() == EngineWake{kStart + 1ms, kStart + 500ms});
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x90, 62, 100}});
    auto status = engine.status();
    CHECK(status.playing);
    CHECK_FALSE(status.finished);
    CHECK(status.position == ppqn.quarter());

    runUntil(engine, clock, kStart + 2s);
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{
                                 {0x90, 62, 100},
                                 {0x80, 62, 64},
                                 {0x90, 64, 100},
                                 {0x80, 64, 64},
                                 {0x90, 65, 100},
                                 {0x80, 65, 64},
                             });
    status = engine.status();
    CHECK(status.playing);
    CHECK(status.finished);
    CHECK(status.lateness.count == 6);
}

TEST_CASE("the engine wakes for its next event, or within 1 ms to look for commands")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.step() == EngineWake{kStart + 1ms, std::nullopt}); // stopped

    CHECK(engine.setPlan(fourNotes(ppqn)));
    CHECK(engine.play(0));
    engine.step();
    clock.time = kStart + 499'500us;
    CHECK(engine.step() == EngineWake{kStart + 500ms, kStart + 500ms});
}

TEST_CASE("commands are carried out in the order given")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));

    SECTION("play then stop sends nothing")
    {
        CHECK(engine.play(0));
        CHECK(engine.stop());
        engine.step();
        CHECK(sink.sent.empty());
        CHECK_FALSE(engine.status().playing);
    }

    SECTION("stop then play plays")
    {
        CHECK(engine.stop());
        CHECK(engine.play(0));
        engine.step();
        CHECK(sink.sent.size() == 1);
        CHECK(engine.status().playing);
    }

    SECTION("of two plays, the later one wins")
    {
        CHECK(engine.play(0));
        CHECK(engine.play(2 * ppqn.quarter()));
        engine.step();
        CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x90, 64, 100}});
    }
}

TEST_CASE("a new plan during playback applies from the current position")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));
    CHECK(engine.play(0));
    runUntil(engine, clock, kStart + 750ms);

    Song song = songAt(ppqn);
    addTrack(song, 2,
             [&](EditTrack& track) { track.events.insert({3 * ppqn.quarter(), Note{72, 90, 1}}); });
    CHECK(engine.setPlan(std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName))));
    sink.sent.clear();
    runUntil(engine, clock, kStart + 2s);

    // The second note, from the first plan, ends as planned; the new plan's note plays.
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{
                                 {0x80, 62, 64},
                                 {0x91, 72, 90},
                                 {0x81, 72, 64},
                             });
}

TEST_CASE("panic and quit end the notes sounding")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));
    CHECK(engine.play(0));
    engine.step();
    sink.sent.clear();

    SECTION("panic")
    {
        CHECK(engine.panic());
        engine.step();
        CHECK(sink.sent.size() == 1 + 16 * 2);
        CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x80, 60, 64});
        CHECK_FALSE(engine.status().playing);
    }

    SECTION("quit")
    {
        CHECK(engine.quit());
        CHECK(engine.stop()); // after quit: taken, but never carried out
        engine.step();
        CHECK(engine.quitRequested());
        CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x80, 60, 64}});
    }
}

// A sink whose every send takes 200 us.
class SlowSink : public MessageSink
{
public:
    explicit SlowSink(FakeClock& clock)
        : clock_(clock)
    {
    }

    void send(DestinationId, std::span<const std::uint8_t>, nanoseconds) noexcept override
    {
        clock_.time += 200us;
    }

private:
    FakeClock& clock_;
};

TEST_CASE("lateness is measured from each message's due time, in two parts")
{
    const auto ppqn = MODAL_ANY_PPQN();
    FakeClock clock;
    SlowSink sink{clock};
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));
    CHECK(engine.play(0));
    engine.step(); // woken on time; sent by 0.2 ms

    // Woken 3 ms late for a note-off and a note-on; the note-on waits for the note-off.
    clock.time = kStart + 503ms;
    engine.step();
    const auto status = engine.status();
    CHECK(status.lateness.count == 3);
    CHECK(status.lateness.max == 3400us);
    CHECK(status.lateness.within(0.3) == 250us);
    CHECK(status.lateness.within(0.99) == 3400us); // the step's edge, but no more than the max
    CHECK(status.wakeLateness.max == 3200us);
    CHECK(status.wakeLateness.within(0.3) == 50us);
    CHECK(status.sendTime.max == 200us);
    CHECK(status.sendTime.within(1.0) == 200us);
}

// The engine on a thread of its own, with a clock that never really sleeps.
TEST_CASE("the engine runs on its own thread until told to quit")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::thread thread{[&] { engine.run(); }};

    REQUIRE(engine.setPlan(fourNotes(ppqn)));
    REQUIRE(engine.play(0));
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!engine.status().finished && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    CHECK(engine.status().finished);
    REQUIRE(engine.quit());
    thread.join();
    CHECK(sink.sent.size() == 8);
}

TEST_CASE("input wakes the engine, which takes it in on its next pass")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const std::vector<std::uint8_t> note{0x90, 60, 100};
    const std::vector<std::uint8_t> sysEx(3 * InputChunk::kMaxBytes, 0x01);
    std::vector<std::uint8_t> wholeSysEx = sysEx;
    wholeSysEx.front() = 0xF0;
    wholeSysEx.back() = 0xF7;

    CHECK(engine.receive(InputId{0}, note, kStart));
    CHECK(engine.receive(InputId{3}, wholeSysEx, kStart));
    CHECK(engine.receive(InputId{0}, std::vector<std::uint8_t>{0xF8}, kStart)); // ignored
    CHECK(clock.wakes == 2);
    CHECK(engine.status().inputReceived == 0);

    engine.step();
    const auto status = engine.status();
    CHECK(status.inputReceived == 2); // the SysEx counts once
    CHECK(status.inputDropped == 0);
}

TEST_CASE("input the engine hasn't kept up with is dropped, for that input only")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const std::vector<std::uint8_t> note{0x90, 60, 100};
    for (std::size_t i = 0; i < InputQueue::kCapacity; ++i)
    {
        REQUIRE(engine.receive(InputId{0}, note, kStart));
    }
    CHECK_FALSE(engine.receive(InputId{0}, note, kStart));
    CHECK(engine.receive(InputId{Engine::kMaxInputs - 1}, note, kStart));
    CHECK(engine.status().inputDropped == 1);

    for (std::size_t pass = 0;
         pass <= InputQueue::kCapacity && engine.status().inputReceived < InputQueue::kCapacity + 1;
         ++pass)
        engine.step();
    CHECK(engine.status().inputReceived == InputQueue::kCapacity + 1);
    CHECK(engine.receive(InputId{0}, note, kStart)); // room again
}

// Input from a thread of its own, while the engine runs on another.
TEST_CASE("the engine takes in input from another thread while it runs")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::thread thread{[&] { engine.run(); }};

    constexpr std::size_t kMessages = 20'000;
    std::thread input{[&]
                      {
                          const std::vector<std::uint8_t> note{0x90, 60, 100};
                          for (std::size_t i = 0; i < kMessages; ++i)
                          {
                              while (!engine.receive(InputId{1}, note, kStart))
                              {
                                  std::this_thread::yield();
                              }
                          }
                      }};
    input.join();
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (engine.status().inputReceived < kMessages && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    REQUIRE(engine.quit());
    thread.join();
    CHECK(engine.status().inputReceived == kMessages);
}

namespace
{

// A plan with one armed track, on channel 2 of destination 1, and nothing to play.
std::shared_ptr<const PlaybackPlan> armedPlan(Ppqn ppqn)
{
    Song song = songAt(ppqn);
    addTrack(song, 2, [](EditTrack& track) { track.arm = true; });
    return std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName));
}

} // namespace

TEST_CASE("the engine sends input thru to the armed tracks, playing or not")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(armedPlan(ppqn)));
    engine.step();

    REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart - 2ms));
    std::vector<std::uint8_t> sysEx(100, 0x01);
    sysEx.front() = 0xF0;
    sysEx.back() = 0xF7;
    REQUIRE(engine.receive(InputId{0}, sysEx, kStart - 1ms)); // not sent thru
    engine.step();
    CHECK(sink.sent == std::vector<Sent>{{DestinationId{1}, {0x91, 60, 100}, kStart - 2ms}});

    const auto status = engine.status();
    CHECK(status.inputReceived == 2);
    CHECK(status.thruDelay.count == 1);
    CHECK(status.thruDelay.max == 2ms);
    CHECK(status.lateness.count == 0); // playback lateness is kept apart
}

TEST_CASE("with thru off, the engine takes input in but doesn't send it on")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(armedPlan(ppqn)));
    engine.setThru(false);
    engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart);
    engine.step();
    CHECK(sink.sent.empty());
    CHECK(engine.status().inputReceived == 1);
}

TEST_CASE("stop leaves held thru notes alone; panic ends them")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(armedPlan(ppqn)));
    CHECK(engine.play(0));
    engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart);
    engine.step();
    CHECK(engine.stop());
    engine.step();
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x91, 60, 100}});

    CHECK(engine.panic());
    engine.step();
    const auto messages = sink.messages();
    REQUIRE(messages.size() > 2);
    CHECK(messages[1] == std::vector<std::uint8_t>{0x81, 60, 64}); // then All Notes Off etc.
}

namespace
{

std::vector<StampedInput> allReceived(Engine& engine)
{
    std::vector<StampedInput> result;
    while (auto stamped = engine.nextReceived())
    {
        result.push_back(*stamped);
    }
    return result;
}

} // namespace

TEST_CASE("the engine passes input back, with the play, tick and song time it arrived at")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));
    const std::vector<std::uint8_t> note{0x90, 60, 100};

    engine.receive(InputId{2}, note, kStart - 1ms);
    engine.step();
    auto received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].input == InputId{2});
    CHECK(received[0].chunk.time == kStart - 1ms);
    CHECK(received[0].first);
    CHECK(received[0].play == 0); // stopped

    CHECK(engine.play(ppqn.quarter()));
    engine.step(); // starts playing at kStart
    CHECK(engine.lastPlay() != 0);
    engine.receive(InputId{2}, note, kStart + 250ms);
    clock.time = kStart + 300ms;
    engine.step();
    received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].play == engine.lastPlay());
    CHECK(received[0].tick == ppqn.quarter() + ppqn.quarter() / 2);
    CHECK(received[0].songTime == 750ms); // the song is at 120 BPM

    // Input that arrived before a stop took effect belongs to the play, even if the stop was
    // given first; input after it doesn't.
    CHECK(engine.stop());
    engine.receive(InputId{2}, note, kStart + 400ms);
    clock.time = kStart + 500ms;
    engine.step(); // stops at 500 ms
    engine.receive(InputId{2}, note, clock.time);
    engine.step();
    received = allReceived(engine);
    REQUIRE(received.size() == 2);
    CHECK(received[0].play == engine.lastPlay());
    CHECK(received[0].tick == ppqn.quarter() + ppqn.quarter() * 4 / 5);
    CHECK(engine.stoppedAt(engine.lastPlay()) == ppqn.quarter() * 2);
    CHECK(received[1].play == 0);
}

TEST_CASE("a SysEx comes back in chunks, the first marked")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    std::vector<std::uint8_t> sysEx(InputChunk::kMaxBytes * 2 + 1, 0x01);
    sysEx.front() = 0xF0;
    sysEx.back() = 0xF7;
    engine.receive(InputId{0}, sysEx, kStart);
    engine.step();
    const auto received = allReceived(engine);
    REQUIRE(received.size() == 3);
    CHECK(received[0].first);
    CHECK_FALSE(received[1].first);
    CHECK_FALSE(received[2].first);
    CHECK_FALSE(received[2].chunk.more);
}

TEST_CASE(
    "input not collected in time is counted, and a SysEx cut short is followed by a new message")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const std::vector<std::uint8_t> note{0x90, 60, 100};
    std::vector<std::uint8_t> sysEx(InputChunk::kMaxBytes * 3, 0x01);
    sysEx.front() = 0xF0;
    sysEx.back() = 0xF7;

    // Fill the way back to two short of full, from several passes of the engine.
    std::size_t queued = 0;
    while (queued < Engine::kMaxStamped - 2)
    {
        for (std::size_t i = 0; i < Engine::kInputBatch && queued < Engine::kMaxStamped - 2;
             ++i, ++queued)
        {
            REQUIRE(engine.receive(InputId{0}, note, kStart));
        }
        engine.step();
    }
    engine.receive(InputId{0}, sysEx, kStart); // only two of its three chunks fit
    engine.receive(InputId{0}, note, kStart);  // doesn't fit
    engine.step();
    CHECK(engine.status().inputUnreported == 2);

    auto received = allReceived(engine);
    REQUIRE(received.size() == Engine::kMaxStamped);
    CHECK(received[Engine::kMaxStamped - 2].first);
    CHECK(received[Engine::kMaxStamped - 1].chunk.more); // cut short

    engine.receive(InputId{0}, note, kStart);
    engine.step();
    received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].first);
}

TEST_CASE("the engine plays a count-in, and input during it is stamped before the record point")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const auto plan = fourNotes(ppqn);
    CHECK(engine.setPlan(plan));
    Song song = songAt(ppqn);
    song.countIn.beats = 2;
    auto start = planRecordStart(song, ppqn.quarter(), DestinationId{9});

    CHECK(engine.play(start.playFrom, std::move(start.countIn)));
    engine.step(); // the first click, 1000 ms before the record point
    engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart + 750ms);
    runUntil(engine, clock, kStart + 1100ms);
    CHECK(sink.sent == std::vector<Sent>{
                           {{9}, {0x99, 77, 80}, kStart},
                           {{9}, {0x89, 77, 64}, kStart + 62500us},
                           {{9}, {0x99, 76, 110}, kStart + 500ms}, // beat 1, before beat 2
                           {{9}, {0x89, 76, 64}, kStart + 562500us},
                           {{1}, {0x90, 62, 100}, kStart + 1000ms}, // the record point
                       });
    const auto received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].tick < ppqn.quarter());
    CHECK(received[0].songTime == 250ms); // the record point is at 500 ms into the song
}

TEST_CASE("the engine reports where each play stopped")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.setPlan(fourNotes(ppqn)));

    CHECK(engine.play(q));
    engine.step();
    const auto first = engine.lastPlay();
    CHECK(engine.stoppedAt(first) == std::nullopt); // still playing
    clock.time = kStart + 750ms;
    CHECK(engine.stop());
    engine.step();
    CHECK(engine.stoppedAt(first) == q + q + q / 2);

    // Stopped in a count-in: before the record point.
    Song song = songAt(ppqn);
    auto start = planRecordStart(song, q, DestinationId{9});
    CHECK(engine.play(start.playFrom, std::move(start.countIn)));
    engine.step();
    const auto second = engine.lastPlay();
    clock.time += 500ms;
    CHECK(engine.stop());
    engine.step();
    REQUIRE(engine.stoppedAt(second));
    CHECK(*engine.stoppedAt(second) < q);
    CHECK(engine.stoppedAt(first) == std::nullopt); // only the last play's is kept
}

TEST_CASE("a plan, play, stop or panic wakes the engine at once")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const int before = clock.wakes;
    CHECK(engine.setPlan(fourNotes(ppqn)));
    CHECK(engine.play(0));
    CHECK(engine.stop());
    CHECK(engine.panic());
    CHECK(clock.wakes == before + 4);
}

TEST_CASE("resetting inputs releases notes and pedals before input IDs can be reused")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    REQUIRE(engine.setPlan(armedPlan(ppqn)));
    engine.step();
    REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, clock.time));
    REQUIRE(engine.receive(InputId{1}, std::vector<std::uint8_t>{0x90, 64, 100}, clock.time));
    for (const auto pedal : std::array<std::uint8_t, 3>{64, 66, 67})
    {
        REQUIRE(
            engine.receive(InputId{0}, std::vector<std::uint8_t>{0xB0, pedal, 100}, clock.time));
    }
    SECTION("input already taken")
    {
        engine.step();
    }
    SECTION("old messages still queued") {}
    // Producers are now closed; no new device may use these IDs until acknowledged.
    const auto request = engine.resetInputs();
    REQUIRE(request);
    CHECK_FALSE(engine.inputsReset(*request));
    CHECK_FALSE(engine.inputsReset(0));
    engine.step();
    REQUIRE(engine.inputsReset(*request));
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x91, 60, 100},
                                                                    {0xB1, 64, 100},
                                                                    {0xB1, 66, 100},
                                                                    {0xB1, 67, 100},
                                                                    {0x91, 64, 100},
                                                                    {0x81, 60, 64},
                                                                    {0x81, 64, 64},
                                                                    {0xB1, 64, 0},
                                                                    {0xB1, 66, 0},
                                                                    {0xB1, 67, 0}});
    // Old input is still available to the recorder/control side, including queued messages.
    std::size_t received = 0;
    while (engine.nextReceived())
    {
        ++received;
    }
    CHECK(received == 5);
    sink.sent.clear();
    REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0x80, 60, 64}, clock.time));
    REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 67, 100}, clock.time));
    engine.step();
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x91, 67, 100}});
    // A later reset needs its own acknowledgement and releases the replacement's note.
    const auto again = engine.resetInputs();
    REQUIRE(again);
    CHECK(*again > *request);
    CHECK_FALSE(engine.inputsReset(*again));
    engine.step();
    CHECK(engine.inputsReset(*again));
    CHECK(sink.messages() ==
          std::vector<std::vector<std::uint8_t>>{{0x91, 67, 100}, {0x81, 67, 64}});
}

TEST_CASE("an input reset retries a full command queue without acknowledging it early")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    while (engine.stop())
    {
    }
    CHECK_FALSE(engine.resetInputs());
    CHECK_FALSE(engine.inputsReset(1));
    engine.step();
    const auto request = engine.resetInputs();
    REQUIRE(request);
    CHECK(*request == 1); // the refused request didn't consume a ticket
    CHECK_FALSE(engine.inputsReset(*request));
    engine.step();
    CHECK(engine.inputsReset(*request));
}

TEST_CASE("resetting inputs keeps playback running without a global controller reset")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track) { track.events.insert({0, Note{72, 100, ppqn.quarter()}}); });
    addTrack(song, 2, [](EditTrack& track) { track.arm = true; });
    REQUIRE(engine.setPlan(std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName))));
    REQUIRE(engine.play(0));
    engine.step();
    REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, clock.time));
    engine.step();
    const auto play = engine.lastPlay();
    const auto request = engine.resetInputs();
    REQUIRE(request);
    engine.step();
    CHECK(engine.inputsReset(*request));
    CHECK(engine.status().playing);
    CHECK_FALSE(engine.stoppedAt(play));
    CHECK(sink.messages() ==
          std::vector<std::vector<std::uint8_t>>{{0x90, 72, 100}, {0x91, 60, 100}, {0x81, 60, 64}});
    runUntil(engine, clock, kStart + 501ms);
    CHECK(sink.messages().back() == std::vector<std::uint8_t>{0x80, 72, 64});
}

TEST_CASE("an input reset drains a final callback after the step's normal input pass")
{
    const auto ppqn = MODAL_ANY_PPQN();
    struct FinalCallbackSink : RecordingSink
    {
        std::function<void()> onStop;
        void send(DestinationId destination, std::span<const std::uint8_t> bytes,
                  nanoseconds due) noexcept override
        {
            RecordingSink::send(destination, bytes, due);
            if (bytes[0] == 0x80 && onStop)
            {
                auto callback = std::move(onStop);
                callback();
            }
        }
    } sink;
    FakeClock clock;
    Engine engine{sink, clock};
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](EditTrack& track) { track.events.insert({0, Note{72, 100, ppqn.quarter()}}); });
    addTrack(song, 2, [](EditTrack& track) { track.arm = true; });
    REQUIRE(engine.setPlan(std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName))));
    REQUIRE(engine.play(0));
    engine.step();
    std::optional<std::uint64_t> request;
    bool received = false;
    // While Stop is sending its note-off, emulate the final input callback, then the
    // control side closing its producer and queuing the reset. The normal input pass has
    // already finished, so the reset itself must drain this message before acknowledging.
    sink.onStop = [&]
    {
        received = engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, clock.time);
        request = engine.resetInputs();
    };
    REQUIRE(engine.stop());
    engine.step();
    REQUIRE(received);
    REQUIRE(request);
    CHECK(engine.inputsReset(*request));
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{
                                 {0x90, 72, 100}, {0x80, 72, 64}, {0x91, 60, 100}, {0x81, 60, 64}});
    CHECK(engine.nextReceived().has_value());
}

TEST_CASE("input work is bounded and a busy low input does not starve a higher one")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const auto high = static_cast<std::uint32_t>(Engine::kMaxInputs - 1);
    for (std::size_t i = 0; i < Engine::kInputBatch * 2; ++i)
    {
        const std::vector<std::uint8_t> cc{0xB0, 1, static_cast<std::uint8_t>(i)};
        REQUIRE(engine.receive(InputId{0}, cc, kStart));
        REQUIRE(engine.receive(InputId{high}, cc, kStart));
    }
    CHECK(engine.inputPending());
    engine.step();
    CHECK(engine.inputPending());
    CHECK(engine.status().inputReceived == Engine::kInputBatch);
    auto first = allReceived(engine);
    REQUIRE(first.size() == Engine::kInputBatch);
    CHECK(first[0].input == InputId{0});
    CHECK(first[Engine::kInputQuantum].input == InputId{high});
    std::array<std::size_t, Engine::kMaxInputs> seen{};
    const auto checkOrder = [&](const std::vector<StampedInput>& inputs)
    {
        for (const auto& input : inputs)
        {
            CHECK(input.chunk.bytes[2] == seen[input.input.value]);
            ++seen[input.input.value];
        }
    };
    checkOrder(first);
    for (int i = 0; i < 3; ++i)
    {
        engine.step();
        checkOrder(allReceived(engine));
    }
    CHECK(seen[0] == Engine::kInputBatch * 2);
    CHECK(seen[high] == Engine::kInputBatch * 2);
    CHECK_FALSE(engine.inputPending());
}

TEST_CASE("input service rotates across every busy port over successive passes")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const std::vector<std::uint8_t> cc{0xB0, 1, 64};
    for (std::uint32_t id = 0; id < Engine::kMaxInputs; ++id)
        for (std::size_t n = 0; n < Engine::kInputQuantum; ++n)
            REQUIRE(engine.receive(InputId{id}, cc, kStart));
    const auto total = Engine::kMaxInputs * Engine::kInputQuantum;
    for (std::size_t taken = 0; taken < total; taken += Engine::kInputBatch)
        engine.step();
    const auto inputs = allReceived(engine);
    REQUIRE(inputs.size() == total);
    for (std::size_t i = 0; i < inputs.size(); ++i)
        CHECK(inputs[i].input.value == i / Engine::kInputQuantum);
}

TEST_CASE("playback sends due events while a large input backlog remains")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    REQUIRE(engine.setPlan(fourNotes(ppqn)));
    REQUIRE(engine.play(0));
    engine.step();
    for (std::size_t n = 0; n < Engine::kInputBatch * 2; ++n)
        REQUIRE(engine.receive(InputId{0}, std::vector<std::uint8_t>{0xB0, 1, 64}, kStart));
    clock.time += 500ms;
    engine.step();
    CHECK(engine.status().inputReceived == Engine::kInputBatch);
    CHECK(sink.messages().back() == std::vector<std::uint8_t>{0x90, 62, 100});
    CHECK(sink.sent.back().due == kStart + 500ms);
}

TEST_CASE("a producer refilling input cannot keep a transport command in the input loop")
{
    struct RefillingSink : RecordingSink
    {
        std::function<void()> refill;
        void send(DestinationId destination, std::span<const std::uint8_t> bytes,
                  nanoseconds due) noexcept override
        {
            RecordingSink::send(destination, bytes, due);
            if (bytes.size() == 3 && (bytes[0] & 0xF0) == 0xB0 && bytes[1] == 1 && refill)
                refill();
        }
    } sink;
    FakeClock clock;
    Engine engine{sink, clock};
    REQUIRE(engine.setPlan(armedPlan(kPpqn)));
    REQUIRE(engine.play(0));
    engine.step();
    const std::vector<std::uint8_t> cc{0xB0, 1, 64};
    const auto captured = Engine::kInputBatch * 2;
    for (std::size_t n = 0; n < captured; ++n)
        REQUIRE(engine.receive(InputId{0}, cc, kStart));
    std::size_t refilled = 0;
    bool kept = true;
    sink.refill = [&]
    {
        // A finite cap also makes this regression safe against the old unbounded loop.
        if (refilled < InputQueue::kCapacity * 2)
        {
            kept = engine.receive(InputId{0}, cc, kStart) && kept;
            ++refilled;
        }
    };
    REQUIRE(engine.stop());
    engine.step();
    CHECK_FALSE(engine.status().playing);
    CHECK(kept);
    CHECK(refilled == captured);
    CHECK(engine.status().inputReceived == captured);
    const auto beforeStop = allReceived(engine);
    REQUIRE(beforeStop.size() == captured);
    for (const auto& input : beforeStop)
        CHECK(input.play == engine.lastPlay());
    sink.refill = {};
    engine.step();
    const auto afterStop = allReceived(engine);
    REQUIRE(afterStop.size() == Engine::kInputBatch);
    for (const auto& input : afterStop)
        CHECK(input.play == 0);
}

TEST_CASE("transport drains the captured backlog before changing its recording stamp")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    REQUIRE(engine.setPlan(fourNotes(ppqn)));
    REQUIRE(engine.play(0));
    engine.step();
    const auto total = Engine::kInputBatch + 5;
    for (std::size_t n = 0; n < total; ++n)
        REQUIRE(
            engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart + 100ms));
    clock.time += 200ms;
    REQUIRE(engine.stop());
    engine.step();
    CHECK_FALSE(engine.status().playing);
    const auto inputs = allReceived(engine);
    REQUIRE(inputs.size() == total);
    for (const auto& input : inputs)
    {
        CHECK(input.play == engine.lastPlay());
        CHECK(input.tick == ppqn.quarter() / 5);
        CHECK(input.songTime == 100ms);
    }
}

TEST_CASE("input batching keeps a complete SysEx together at the message budget boundary")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    const std::vector<std::uint8_t> note{0x90, 60, 100};
    for (std::size_t n = 0; n < Engine::kInputBatch - 1; ++n)
        REQUIRE(engine.receive(InputId{0}, note, kStart));
    const auto chunks = Engine::kInputBatch * 2;
    std::vector<std::uint8_t> sysex(chunks * InputChunk::kMaxBytes, 1);
    sysex.front() = 0xF0;
    sysex.back() = 0xF7;
    REQUIRE(engine.receive(InputId{0}, sysex, kStart));
    REQUIRE(engine.receive(InputId{0}, note, kStart));
    engine.step();
    CHECK(engine.status().inputReceived == Engine::kInputBatch);
    const auto inputs = allReceived(engine);
    REQUIRE(inputs.size() == Engine::kInputBatch - 1 + chunks);
    for (std::size_t i = 0; i < chunks; ++i)
    {
        const auto& input = inputs[Engine::kInputBatch - 1 + i];
        CHECK(input.first == (i == 0));
        CHECK(input.chunk.more == (i + 1 != chunks));
    }
    engine.step();
    const auto last = allReceived(engine);
    REQUIRE(last.size() == 1);
    CHECK(last[0].first);
    CHECK(last[0].chunk.bytes[0] == 0x90);
}

TEST_CASE("an auditioned note sounds now and ends after its length, or as the next starts")
{
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    REQUIRE(engine.audition(DestinationId{3}, 9, 76, 110, 50ms));
    engine.step();
    REQUIRE(sink.sent.size() == 1);
    CHECK(sink.sent[0].destination == DestinationId{3});
    CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x99, 76, 110});
    // Woken for its end, which goes when due.
    const auto wake = engine.step();
    CHECK(wake.nextEvent == std::optional{sink.sent[0].due + 50ms});
    runUntil(engine, clock, clock.time + 60ms);
    REQUIRE(sink.sent.size() == 2);
    CHECK(sink.sent[1].bytes == std::vector<std::uint8_t>{0x89, 76, 0});

    // Another before the first ends ends it first; panic ends one still sounding.
    sink.sent.clear();
    REQUIRE(engine.audition(DestinationId{3}, 9, 76, 110, 1s));
    REQUIRE(engine.audition(DestinationId{3}, 9, 77, 80, 1s));
    engine.step();
    REQUIRE(sink.sent.size() == 3);
    CHECK(sink.sent[1].bytes == std::vector<std::uint8_t>{0x89, 76, 0});
    CHECK(sink.sent[2].bytes == std::vector<std::uint8_t>{0x99, 77, 80});
    REQUIRE(engine.panic());
    engine.step();
    CHECK(std::ranges::any_of(sink.sent, [](const Sent& sent)
                              { return sent.bytes == std::vector<std::uint8_t>{0x89, 77, 0}; }));
}
