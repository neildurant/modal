#include <modal/platform/thread_priority.hpp>

#include <catch2/catch_test_macros.hpp>

#include <thread>

using modal::platform::RealtimePriority;

// Whether the priority can be raised depends on the machine: CI runners and most Linux
// accounts aren't permitted real-time scheduling. Either way, it says which.
TEST_CASE("raising a thread's priority reports whether it worked")
{
    bool consistent = true;
    std::thread thread(
        [&]
        {
            for (int i = 0; i < 2; ++i) // raised and restored twice on the same thread
            {
                const RealtimePriority priority;
                consistent = consistent && priority.raised() == priority.error().empty();
            }
        });
    thread.join();
    CHECK(consistent);
}
