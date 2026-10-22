// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/engine.hpp>

#include <atomic>
#include <chrono>
#include <optional>

namespace modal::platform
{

// The engine's clock: the steady clock, as the time since its epoch. MIDI input is
// timestamped on it too.
inline std::chrono::nanoseconds now() noexcept
{
    return std::chrono::steady_clock::now().time_since_epoch();
}

// Sleeps the calling thread until a time on the engine's clock, as precisely as the platform
// allows (a high-resolution waitable timer on Windows, an absolute timerfd on Linux), or until
// another thread wakes it. Never wakes early unless woken. Each thread that sleeps needs its
// own.
class Sleeper
{
public:
    Sleeper();
    ~Sleeper();

    Sleeper(const Sleeper&) = delete;
    Sleeper& operator=(const Sleeper&) = delete;

    // Returns true at the time, or false if woken first.
    bool sleepUntil(std::chrono::nanoseconds time) noexcept;

    // Busy-waits until the time, as spinUntil does, but can be woken like a sleep. Returns
    // true at the time, or false if woken first.
    bool spinUntil(std::chrono::nanoseconds time) noexcept;

    // Ends the current sleep or spin early, or if there is none, the next one. Safe to call
    // from any thread.
    void wake() noexcept;

private:
    // Clears a pending wake, saying whether there was one.
    bool takeWake() noexcept
    {
        return woken_.load(std::memory_order_relaxed) &&
               woken_.exchange(false, std::memory_order_acquire);
    }

    std::atomic<bool> woken_{false};
    [[maybe_unused]] void* timer_ = nullptr; // Windows only
    [[maybe_unused]] void* event_ = nullptr; // Windows only
    [[maybe_unused]] int timerFd_ = -1;      // Linux only
    [[maybe_unused]] int eventFd_ = -1;      // Linux only
};

// Busy-waits until a time on the engine's clock, using the CPU throughout but waking within
// microseconds of it.
void spinUntil(std::chrono::nanoseconds time) noexcept;

// The engine's clock: sleeping is for the engine thread only, waking for any thread.
// Sleeping is accurate to around a millisecond, so for events it sleeps until a margin before
// the time and spins the rest, even when that runs past a routine wake-up.
class SystemClock final : public core::EngineClock
{
public:
    static constexpr std::chrono::nanoseconds kDefaultSpinMargin = std::chrono::microseconds{1500};

    explicit SystemClock(std::chrono::nanoseconds spinMargin = kDefaultSpinMargin)
        : spinMargin_(spinMargin)
    {
    }

    std::chrono::nanoseconds now() noexcept override { return platform::now(); }

    void sleepUntil(std::chrono::nanoseconds time,
                    std::optional<std::chrono::nanoseconds> nextEvent) noexcept override
    {
        if (nextEvent && spinMargin_ > std::chrono::nanoseconds{0} &&
            *nextEvent - spinMargin_ <= time)
        {
            if (sleeper_.sleepUntil(*nextEvent - spinMargin_))
            {
                sleeper_.spinUntil(*nextEvent);
            }
        }
        else
        {
            sleeper_.sleepUntil(time);
        }
    }

    void wake() noexcept override { sleeper_.wake(); }

private:
    Sleeper sleeper_;
    std::chrono::nanoseconds spinMargin_;
};

} // namespace modal::platform
