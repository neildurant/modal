#include <modal/platform/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>

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
