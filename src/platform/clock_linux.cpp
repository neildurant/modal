#include <modal/platform/clock.hpp>

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace modal::platform
{

// On Linux the steady clock is CLOCK_MONOTONIC, so the time can be passed on as it is.

namespace
{

timespec toTimespec(std::chrono::nanoseconds time)
{
    timespec result{};
    result.tv_sec = static_cast<time_t>(time.count() / 1'000'000'000);
    result.tv_nsec = static_cast<long>(time.count() % 1'000'000'000);
    return result;
}

// Reads a timerfd's or eventfd's count, to clear it.
void drain(int fd)
{
    std::uint64_t count = 0;
    [[maybe_unused]] const auto ignored = read(fd, &count, sizeof count);
}

void relax()
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause(); // eases the spin on the core and its sibling
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

} // namespace

Sleeper::Sleeper()
{
    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    eventFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

Sleeper::~Sleeper()
{
    for (const int fd : {timerFd_, eventFd_})
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }
}

bool Sleeper::sleepUntil(std::chrono::nanoseconds time) noexcept
{
    // The eventfd only says to look at woken_: a wake already taken may leave it set, which
    // just goes round again.
    const itimerspec due{{}, toTimespec(time)};
    const bool waitable = timerFd_ >= 0 && eventFd_ >= 0 &&
                          timerfd_settime(timerFd_, TFD_TIMER_ABSTIME, &due, nullptr) == 0;
    for (;;)
    {
        if (takeWake())
        {
            return false;
        }
        if (now() >= time)
        {
            return true;
        }

        if (waitable)
        {
            pollfd fds[] = {{timerFd_, POLLIN, 0}, {eventFd_, POLLIN, 0}};
            if (poll(fds, 2, -1) > 0)
            {
                if ((fds[0].revents & POLLIN) != 0)
                {
                    drain(timerFd_);
                }
                if ((fds[1].revents & POLLIN) != 0)
                {
                    drain(eventFd_);
                }
            }
        }
        else // can't be woken early, but still sleeps precisely
        {
            const timespec until = toTimespec(time);
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr) == EINTR)
            {
            }
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
        relax();
    }
    return true;
}

void Sleeper::wake() noexcept
{
    woken_.store(true, std::memory_order_release);
    if (eventFd_ >= 0)
    {
        const std::uint64_t one = 1;
        [[maybe_unused]] const auto ignored = write(eventFd_, &one, sizeof one);
    }
}

void spinUntil(std::chrono::nanoseconds time) noexcept
{
    while (now() < time)
    {
        relax();
    }
}

} // namespace modal::platform
