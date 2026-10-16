#include <modal/platform/clock.hpp>

#include <cerrno>
#include <ctime>

namespace modal::platform
{

// On Linux the steady clock is CLOCK_MONOTONIC, so the time can be passed on as it is.

Sleeper::Sleeper() = default;
Sleeper::~Sleeper() = default;

void Sleeper::sleepUntil(std::chrono::nanoseconds time) noexcept
{
    timespec until{};
    until.tv_sec = static_cast<time_t>(time.count() / 1'000'000'000);
    until.tv_nsec = static_cast<long>(time.count() % 1'000'000'000);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr) == EINTR)
    {
    }
}

void spinUntil(std::chrono::nanoseconds time) noexcept
{
    while (now() < time)
    {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause(); // eases the spin on the core and its sibling
#elif defined(__aarch64__)
        asm volatile("yield");
#endif
    }
}

} // namespace modal::platform
