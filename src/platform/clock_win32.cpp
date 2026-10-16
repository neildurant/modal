#include <modal/platform/clock.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <algorithm>
#include <cstdint>
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
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
}

Sleeper::~Sleeper()
{
    for (void* handle : {timer_, event_})
    {
        if (handle != nullptr)
        {
            CloseHandle(handle);
        }
    }
}

bool Sleeper::sleepUntil(std::chrono::nanoseconds time) noexcept
{
    // The timer runs on a different clock from the steady clock, so check and sleep again
    // if it wakes a little early. The event only says to look at woken_: a wake already
    // taken may leave it set, which just goes round again.
    for (;;)
    {
        if (takeWake())
        {
            return false;
        }
        const auto remaining = time - now();
        if (remaining <= 0ns)
        {
            return true;
        }

        LARGE_INTEGER due;
        due.QuadPart = -((remaining.count() + 99) / 100); // relative, in 100 ns units
        if (timer_ != nullptr && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
        {
            if (event_ != nullptr)
            {
                const HANDLE handles[] = {event_, timer_};
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            }
            else
            {
                WaitForSingleObject(timer_, INFINITE);
            }
        }
        else if (event_ != nullptr)
        {
            const auto ms = std::chrono::ceil<std::chrono::milliseconds>(remaining);
            const auto longest = static_cast<std::int64_t>(INFINITE - 1);
            WaitForSingleObject(event_,
                                static_cast<DWORD>(std::min<std::int64_t>(ms.count(), longest)));
        }
        else
        {
            std::this_thread::sleep_for(remaining);
        }
    }
}

bool Sleeper::spinUntil(std::chrono::nanoseconds time) noexcept
{
    while (now() < time)
    {
        if (takeWake())
        {
            return false;
        }
        YieldProcessor();
    }
    return true;
}

void Sleeper::wake() noexcept
{
    woken_.store(true, std::memory_order_release);
    if (event_ != nullptr)
    {
        SetEvent(event_);
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
