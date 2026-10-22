// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace modal::platform
{

// Raises the calling thread to real-time priority for as long as it exists: the MMCSS
// "Pro Audio" task on Windows, SCHED_FIFO on Linux. Restores the thread's priority when
// destroyed, which must happen on the same thread.
class RealtimePriority
{
public:
    static constexpr int kLinuxPriority = 70; // SCHED_FIFO, 1-99

    RealtimePriority();
    ~RealtimePriority();

    RealtimePriority(const RealtimePriority&) = delete;
    RealtimePriority& operator=(const RealtimePriority&) = delete;

    bool raised() const noexcept { return raised_; }

    // Why the priority couldn't be raised, if it wasn't.
    const std::string& error() const noexcept { return error_; }

private:
    bool raised_ = false;
    std::string error_;

    [[maybe_unused]] void* task_ = nullptr; // Windows only
    [[maybe_unused]] int oldPolicy_ = 0;    // Linux only
    [[maybe_unused]] int oldPriority_ = 0;  // Linux only
};

} // namespace modal::platform
