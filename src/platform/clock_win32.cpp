#include <modal/platform/clock.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <thread>
#include <windows.h>

namespace modal::platform
{

using namespace std::chrono_literals;

Sleeper::Sleeper()
{
    // High-resolution timers need Windows 10 1803 or later; fall back to an ordinary one.
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
    if (timer_ == nullptr)
    {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
}

Sleeper::~Sleeper()
{
    if (timer_ != nullptr)
    {
        CloseHandle(timer_);
    }
}

void Sleeper::sleepUntil(std::chrono::nanoseconds time) noexcept
{
    // The timer runs on a different clock from the steady clock, so check and sleep again
    // if it wakes a little early.
    for (auto remaining = time - now(); remaining > 0ns; remaining = time - now())
    {
        LARGE_INTEGER due;
        due.QuadPart = -((remaining.count() + 99) / 100); // relative, in 100 ns units
        if (timer_ != nullptr && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
        {
            WaitForSingleObject(timer_, INFINITE);
        }
        else
        {
            std::this_thread::sleep_for(remaining);
        }
    }
}

void spinUntil(std::chrono::nanoseconds time) noexcept
{
    while (now() < time)
    {
        YieldProcessor(); // a pause instruction: eases the spin on the core and its sibling
    }
}

} // namespace modal::platform
