#include <modal/platform/thread_priority.hpp>

#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>

namespace modal::platform
{

RealtimePriority::RealtimePriority()
{
    sched_param old{};
    if (pthread_getschedparam(pthread_self(), &oldPolicy_, &old) == 0)
    {
        oldPriority_ = old.sched_priority;
    }

    sched_param param{};
    param.sched_priority = kLinuxPriority;
    const int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (result == EPERM)
    {
        error_ = "real-time priority isn't permitted: add the user to the audio group, or set "
                 "an rtprio limit in /etc/security/limits.conf";
        return;
    }
    if (result != 0)
    {
        error_ = std::string{"can't set real-time priority: "} + std::strerror(result);
        return;
    }
    raised_ = true;
}

RealtimePriority::~RealtimePriority()
{
    if (raised_)
    {
        sched_param old{};
        old.sched_priority = oldPriority_;
        pthread_setschedparam(pthread_self(), oldPolicy_, &old);
    }
}

} // namespace modal::platform
