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

// The engine's clock, for the engine thread only.
class SystemClock final : public core::EngineClock
{
public:
    std::chrono::nanoseconds now() noexcept override { return platform::now(); }
    void sleepUntil(std::chrono::nanoseconds time) noexcept override { sleeper_.sleepUntil(time); }

private:
    Sleeper sleeper_;
};

} // namespace modal::platform
