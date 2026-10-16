#include <modal/core/timing_stats.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/thread_priority.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <iostream>

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
            clock.sleepUntil(until, forEvent);
            CHECK(clock.now() >= until);
        }
    }
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
            clock.sleepUntil(until, true);
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
