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
    void sleepUntil(nanoseconds until) noexcept override { time = std::max(time, until); }

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
        clock.sleepUntil(engine.step());
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

    CHECK(engine.step() == kStart + 1ms); // the first note goes at once; the next is 500 ms on
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

TEST_CASE("the engine wakes for its next event, or within 1 ms")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    CHECK(engine.step() == kStart + 1ms); // stopped

    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    engine.step();
    clock.time = kStart + 499'500us;
    CHECK(engine.step() == kStart + 500ms);
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

TEST_CASE("lateness is measured from each message's due time")
{
    const auto ppqn = MODAL_ANY_PPQN();
    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    engine.setPlan(fourNotes(ppqn));
    engine.play(0);
    engine.step(); // on time

    clock.time = kStart + 503ms; // note-off and note-on 3 ms late
    engine.step();
    const auto lateness = engine.status().lateness;
    CHECK(lateness.count == 3);
    CHECK(lateness.max == 3ms);
    CHECK(lateness.buckets[0] == 1);
    CHECK(lateness.buckets[5] == 2); // 2-5 ms
    CHECK(lateness.within(0.3) == 100us);
    CHECK(lateness.within(0.99) == 5ms);
}

TEST_CASE("the lateness limit that a fraction of messages were within")
{
    LatenessStats stats;
    CHECK(stats.within(0.99) == std::nullopt);

    stats.count = 100;
    stats.buckets = {90, 5, 4, 0, 0, 0, 0, 1};
    CHECK(stats.within(0.5) == 100us);
    CHECK(stats.within(0.95) == 250us);
    CHECK(stats.within(0.99) == 500us);
    CHECK(stats.within(1.0) == std::nullopt); // one message was over 10 ms late
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
