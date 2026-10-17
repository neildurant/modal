#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/engine.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;
using std::chrono::nanoseconds;

namespace
{

constexpr nanoseconds kStart = 1000s;

// A clock that stands still until a test moves it, or until the engine sleeps.
class FakeClock : public EngineClock
{
public:
    nanoseconds now() noexcept override { return time; }
    void sleepUntil(nanoseconds until, std::optional<nanoseconds>) noexcept override
    {
        time = std::max(time, until);
    }
    void wake() noexcept override { ++wakes; }

    nanoseconds time = kStart;
    std::atomic<int> wakes{0}; // from input threads too
};

// One note per quarter, starting on the beat, a quarter note long: pitches 60, 62, 64, 65.
std::shared_ptr<const PlaybackPlan> fourNotes(Ppqn ppqn)
{
    Song song = songAt(ppqn);
    addTrack(song, 1,
             [&](Track& track)
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

// Steps the engine as run() would, until the clock reaches an end time.
void runUntil(Engine& engine, FakeClock& clock, nanoseconds end)
{
    while (clock.time <= end)
    {
        clock.sleepUntil(engine.step().time, std::nullopt);
    }
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

    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
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
    engine.setPlan(fourNotes(ppqn));

    SECTION("play then stop sends nothing")
    {
        engine.play(0);
        engine.stop();
        engine.step();
        CHECK(sink.sent.empty());
        CHECK_FALSE(engine.status().playing);
    }

    SECTION("stop then play plays")
    {
        engine.stop();
        engine.play(0);
        engine.step();
        CHECK(sink.sent.size() == 1);
        CHECK(engine.status().playing);
    }

    SECTION("of two plays, the later one wins")
    {
        engine.play(0);
        engine.play(2 * ppqn.quarter());
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
    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    runUntil(engine, clock, kStart + 750ms);

    Song song = songAt(ppqn);
    addTrack(song, 2,
             [&](Track& track) { track.events.insert({3 * ppqn.quarter(), Note{72, 90, 1}}); });
    engine.setPlan(std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName)));
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
    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    engine.step();
    sink.sent.clear();

    SECTION("panic")
    {
        engine.panic();
        engine.step();
        CHECK(sink.sent.size() == 1 + 16 * 2);
        CHECK(sink.sent[0].bytes == std::vector<std::uint8_t>{0x80, 60, 64});
        CHECK_FALSE(engine.status().playing);
    }

    SECTION("quit")
    {
        engine.quit();
        engine.stop(); // after quit: never carried out
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
    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    engine.step(); // woken on time; sent by 0.2 ms

    // Woken 3 ms late for a note-off and a note-on; the note-on waits for the note-off.
    clock.time = kStart + 503ms;
    engine.step();
    const auto status = engine.status();
    CHECK(status.lateness.count == 3);
    CHECK(status.lateness.max == 3400us);
    CHECK(status.lateness.within(0.3) == 250us);
    CHECK(status.lateness.within(0.99) == 3450us);
    CHECK(status.wakeLateness.max == 3200us);
    CHECK(status.wakeLateness.within(0.3) == 50us);
    CHECK(status.sendTime.max == 200us);
    CHECK(status.sendTime.within(1.0) == 250us);
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
    addTrack(song, 2, [](Track& track) { track.arm = true; });
    return std::make_shared<const PlaybackPlan>(compilePlan(song, byPortName));
}

} // namespace

TEST_CASE("the engine sends input thru to the armed tracks, playing or not")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    engine.setPlan(armedPlan(ppqn));
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
    engine.setPlan(armedPlan(ppqn));
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
    engine.setPlan(armedPlan(ppqn));
    engine.play(0);
    engine.receive(InputId{0}, std::vector<std::uint8_t>{0x90, 60, 100}, kStart);
    engine.step();
    engine.stop();
    engine.step();
    CHECK(sink.messages() == std::vector<std::vector<std::uint8_t>>{{0x91, 60, 100}});

    engine.panic();
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
    engine.setPlan(fourNotes(ppqn));
    const std::vector<std::uint8_t> note{0x90, 60, 100};

    engine.receive(InputId{2}, note, kStart - 1ms);
    engine.step();
    auto received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].input == InputId{2});
    CHECK(received[0].chunk.time == kStart - 1ms);
    CHECK(received[0].first);
    CHECK(received[0].play == 0); // stopped

    engine.play(ppqn.quarter());
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

    engine.stop();
    engine.receive(InputId{2}, note, clock.time);
    engine.step();
    received = allReceived(engine);
    REQUIRE(received.size() == 1);
    CHECK(received[0].play == 0);
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
        for (std::size_t i = 0; i < 512 && queued < Engine::kMaxStamped - 2; ++i, ++queued)
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
    engine.setPlan(plan);
    Song song = songAt(ppqn);
    song.countIn.beats = 2;
    auto start = planRecordStart(song, ppqn.quarter(), DestinationId{9});

    engine.play(start.playFrom, std::move(start.countIn));
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
    engine.setPlan(fourNotes(ppqn));

    engine.play(q);
    engine.step();
    const auto first = engine.lastPlay();
    CHECK(engine.stoppedAt(first) == std::nullopt); // still playing
    clock.time = kStart + 750ms;
    engine.stop();
    engine.step();
    CHECK(engine.stoppedAt(first) == q + q + q / 2);

    // Stopped in a count-in: before the record point.
    Song song = songAt(ppqn);
    auto start = planRecordStart(song, q, DestinationId{9});
    engine.play(start.playFrom, std::move(start.countIn));
    engine.step();
    const auto second = engine.lastPlay();
    clock.time += 500ms;
    engine.stop();
    engine.step();
    REQUIRE(engine.stoppedAt(second));
    CHECK(*engine.stoppedAt(second) < q);
    CHECK(engine.stoppedAt(first) == std::nullopt); // only the last play's is kept
}
