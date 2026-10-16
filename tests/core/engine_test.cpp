#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/engine.hpp>

#include <catch2/catch_test_macros.hpp>

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
    void sleepUntil(nanoseconds until, bool) noexcept override { time = std::max(time, until); }

    nanoseconds time = kStart;
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
        clock.sleepUntil(engine.step().time, false);
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
    CHECK(engine.step() == EngineWake{kStart + 1ms, false});
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
    CHECK(engine.step() == EngineWake{kStart + 1ms, false}); // stopped

    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    engine.step();
    clock.time = kStart + 499'500us;
    CHECK(engine.step() == EngineWake{kStart + 500ms, true});
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
