#pragma once

// A clock for tests of the engine, which stands still until moved.

#include <modal/core/engine.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>

namespace modal::test
{

inline constexpr std::chrono::nanoseconds kStart = std::chrono::seconds{1000};

// A clock that stands still until a test moves it, or until the engine sleeps.
class FakeClock : public core::EngineClock
{
public:
    std::chrono::nanoseconds now() noexcept override { return time; }
    void sleepUntil(std::chrono::nanoseconds until,
                    std::optional<std::chrono::nanoseconds>) noexcept override
    {
        time = std::max(time, until);
    }
    void wake() noexcept override { ++wakes; }

    std::chrono::nanoseconds time = kStart;
    std::atomic<int> wakes{0}; // from input threads too
};

// Steps the engine as run() would, until the clock reaches an end time.
inline void runUntil(core::Engine& engine, FakeClock& clock, std::chrono::nanoseconds end)
{
    while (clock.time <= end)
    {
        clock.sleepUntil(engine.step().time, std::nullopt);
    }
}

} // namespace modal::test
