#include <modal/core/engine.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/timing_stats.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/thread_priority.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <thread>

using namespace modal::platform;
using namespace std::chrono_literals;

// How precisely the sleep wakes depends on the machine and its load, so the tests only check
// that it never wakes early. Timing is measured with real MIDI by the loopback tool.
TEST_CASE("sleeping until a time never wakes before it")
{
    const auto duration = GENERATE(std::chrono::nanoseconds{1}, std::chrono::nanoseconds{50us},
                                   std::chrono::nanoseconds{1ms}, std::chrono::nanoseconds{3ms});
    Sleeper sleeper;
    for (int i = 0; i < 5; ++i)
    {
        const auto until = now() + duration;
        sleeper.sleepUntil(until);
        CHECK(now() >= until);
    }
}

TEST_CASE("sleeping until a time already past returns at once")
{
    Sleeper sleeper;
    const auto start = now();
    sleeper.sleepUntil(start - 1s);
    CHECK(now() - start < 500ms);
}

TEST_CASE("spinning until a time never stops before it")
{
    for (int i = 0; i < 20; ++i)
    {
        const auto until = now() + 200us;
        spinUntil(until);
        CHECK(now() >= until);
    }
}

TEST_CASE("the system clock never wakes early, for events or not")
{
    const auto margin = GENERATE(std::chrono::nanoseconds{0}, std::chrono::nanoseconds{1500us});
    SystemClock clock{margin};
    for (const bool forEvent : {false, true})
    {
        for (int i = 0; i < 5; ++i)
        {
            const auto until = clock.now() + 2ms;
            const auto event = forEvent ? std::optional{until + 500us} : std::nullopt;
            clock.sleepUntil(until, event);
            CHECK(clock.now() >= until);
            if (event && margin > 0ns) // spinning carries on to the event
            {
                CHECK(clock.now() >= *event);
            }
        }
    }
}

// How soon a woken sleep returns depends on the machine, so the limit is generous: it only
// shows the wake ended the sleep rather than the sleep running its course.
TEST_CASE("a wake from another thread ends a sleep or spin early")
{
    const bool spin = GENERATE(false, true);
    Sleeper sleeper;
    const auto start = now();
    std::thread waker{[&]
                      {
                          std::this_thread::sleep_for(20ms);
                          sleeper.wake();
                      }};
    const bool reached = spin ? sleeper.spinUntil(start + 5s) : sleeper.sleepUntil(start + 5s);
    waker.join();
    CHECK_FALSE(reached);
    CHECK(now() - start < 2s);
}

TEST_CASE("a wake before a sleep ends that sleep at once, and only that one")
{
    Sleeper sleeper;
    sleeper.wake();
    const auto start = now();
    CHECK_FALSE(sleeper.sleepUntil(start + 5s));
    CHECK(now() - start < 2s);

    const auto until = now() + 2ms;
    CHECK(sleeper.sleepUntil(until));
    CHECK(now() >= until);
}

TEST_CASE("the system clock passes wakes on to its sleep")
{
    SystemClock clock;
    const auto start = clock.now();
    std::thread waker{[&]
                      {
                          std::this_thread::sleep_for(20ms);
                          clock.wake();
                      }};
    clock.sleepUntil(start + 5s, start + 5s);
    waker.join();
    CHECK(clock.now() - start < 2s);
}

// Not run by default, since it measures the machine rather than checking the code. Run it
// with: modal_tests "[.timing]"
TEST_CASE("how late the system clock wakes", "[.timing]")
{
    const modal::platform::RealtimePriority priority;
    for (const auto margin : {std::chrono::nanoseconds{0}, SystemClock::kDefaultSpinMargin})
    {
        SystemClock clock{margin};
        modal::core::TimingStats lateness;
        for (int i = 0; i < 1000; ++i)
        {
            const auto until = clock.now() + 3700us; // not a whole number of timer ticks
            clock.sleepUntil(until, until);
            lateness.record(clock.now() - until);
        }
        const auto ms = [](auto time)
        { return std::chrono::duration<double, std::milli>(time).count(); };
        std::cout << "spin margin " << ms(margin) << " ms: 50% within "
                  << ms(lateness.within(0.5).value_or(20ms)) << " ms, 99% within "
                  << ms(lateness.within(0.99).value_or(20ms)) << " ms, max " << ms(lateness.max)
                  << " ms (priority " << (priority.raised() ? "raised" : "normal") << ")\n";
    }
}

namespace
{

class NullSink : public modal::core::MessageSink
{
public:
    void send(modal::core::DestinationId, std::span<const std::uint8_t>,
              std::chrono::nanoseconds) noexcept override
    {
    }
};

} // namespace

// The engine's own timing with the system clock, as modal-loopback's default stream (a note
// every 10 ms, lasting 5 ms) but without MIDI. Not run by default; run it with
// modal_tests "[.timing]"
TEST_CASE("how late the engine sends a steady stream", "[.timing]")
{
    using namespace modal::core;
    for (const auto margin : {std::chrono::nanoseconds{0}, SystemClock::kDefaultSpinMargin})
    {
        PlaybackPlan plan{TempoMap{kPpqn}};
        plan.tracks.push_back({TrackId{1}, DestinationId{0}, 0, false, 0ns});
        for (int i = 0; i < 500; ++i)
        {
            PlannedEvent event{};
            event.time = 50ms + 10ms * i;
            event.tick = i;
            event.track = TrackId{1};
            event.message = {{0x90, static_cast<std::uint8_t>(i % 128), 100}, 3};
            event.endTick = i;
            event.noteOffTime = event.time + 5ms;
            plan.events.push_back(event);
        }

        NullSink sink;
        SystemClock clock{margin};
        Engine engine{sink, clock};
        std::thread thread{[&]
                           {
                               const modal::platform::RealtimePriority priority;
                               engine.run();
                           }};
        engine.setPlan(std::make_shared<const PlaybackPlan>(std::move(plan)));
        engine.play(0);
        while (!engine.status().finished)
        {
            std::this_thread::sleep_for(10ms);
        }
        engine.quit();
        thread.join();

        const auto waking = engine.status().wakeLateness;
        const auto ms = [](auto time)
        { return std::chrono::duration<double, std::milli>(time).count(); };
        std::cout << "engine, spin margin " << ms(margin) << " ms: waking 50% within "
                  << ms(waking.within(0.5).value_or(20ms)) << " ms, 99% within "
                  << ms(waking.within(0.99).value_or(20ms)) << " ms, max " << ms(waking.max)
                  << " ms\n";
    }
}
