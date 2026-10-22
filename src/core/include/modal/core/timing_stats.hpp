// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace modal::core
{

// A histogram of durations in 50 µs steps up to 20 ms, for timing gathered on the engine
// thread, where nothing can allocate.
struct TimingStats
{
    static constexpr std::chrono::nanoseconds kStep = std::chrono::microseconds{50};
    static constexpr std::size_t kSteps = 400; // up to 20 ms; a last bucket holds anything later

    std::size_t count = 0;
    std::chrono::nanoseconds max{0};
    std::array<std::size_t, kSteps + 1> buckets{}; // buckets[i]: from i steps up to i + 1

    // The bucket a duration falls in; negative durations count as zero.
    static std::size_t bucketOf(std::chrono::nanoseconds duration) noexcept;

    void record(std::chrono::nanoseconds duration) noexcept;

    // A time that at least a fraction (e.g. 0.99) of the durations were within, rounded up to
    // the next step, or none if that's beyond 20 ms or nothing has been recorded.
    std::optional<std::chrono::nanoseconds> within(double fraction) const;
};

// Timing stats recorded by one thread and read by any.
class SharedTimingStats
{
public:
    void record(std::chrono::nanoseconds duration) noexcept; // the recording thread only
    TimingStats read() const noexcept;

private:
    std::atomic<std::size_t> count_{0};
    std::atomic<std::int64_t> max_{0};
    std::array<std::atomic<std::size_t>, TimingStats::kSteps + 1> buckets_{};
};

} // namespace modal::core
