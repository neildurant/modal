#pragma once

#include <modal/core/engine.hpp>

#include <chrono>

namespace modal::platform
{

// The engine's clock: the steady clock, as the time since its epoch. MIDI input is
// timestamped on it too.
inline std::chrono::nanoseconds now() noexcept
{
    return std::chrono::steady_clock::now().time_since_epoch();
}

// Sleeps the calling thread until a time on the engine's clock, as precisely as the platform
// allows: a high-resolution waitable timer on Windows, clock_nanosleep on Linux. Never wakes
// early. Each thread that sleeps needs its own.
class Sleeper
{
public:
    Sleeper();
    ~Sleeper();

    Sleeper(const Sleeper&) = delete;
    Sleeper& operator=(const Sleeper&) = delete;

    void sleepUntil(std::chrono::nanoseconds time) noexcept;

private:
    [[maybe_unused]] void* timer_ = nullptr; // Windows only
};

// Busy-waits until a time on the engine's clock, using the CPU throughout but waking within
// microseconds of it.
void spinUntil(std::chrono::nanoseconds time) noexcept;

// The engine's clock, for the engine thread only. Sleeping is accurate to around a
// millisecond, so for events it sleeps until a margin before the time and spins the rest.
class SystemClock final : public core::EngineClock
{
public:
    static constexpr std::chrono::nanoseconds kDefaultSpinMargin = std::chrono::microseconds{1500};

    explicit SystemClock(std::chrono::nanoseconds spinMargin = kDefaultSpinMargin)
        : spinMargin_(spinMargin)
    {
    }

    std::chrono::nanoseconds now() noexcept override { return platform::now(); }

    void sleepUntil(std::chrono::nanoseconds time, bool forEvent) noexcept override
    {
        if (forEvent && spinMargin_ > std::chrono::nanoseconds{0})
        {
            sleeper_.sleepUntil(time - spinMargin_);
            spinUntil(time);
        }
        else
        {
            sleeper_.sleepUntil(time);
        }
    }

private:
    Sleeper sleeper_;
    std::chrono::nanoseconds spinMargin_;
};

} // namespace modal::platform
