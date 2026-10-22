// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/timing_stats.hpp>

#include <algorithm>
#include <cmath>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

// One writer, so a plain load and store does instead of a read-modify-write.
template <typename T>
void add(std::atomic<T>& counter, T amount) noexcept
{
    counter.store(counter.load(std::memory_order_relaxed) + amount, std::memory_order_relaxed);
}

} // namespace

std::size_t TimingStats::bucketOf(nanoseconds duration) noexcept
{
    if (duration < nanoseconds{0})
    {
        return 0;
    }
    return std::min(static_cast<std::size_t>(duration / kStep), kSteps);
}

void TimingStats::record(nanoseconds duration) noexcept
{
    ++count;
    max = std::max(max, duration);
    ++buckets[bucketOf(duration)];
}

std::optional<nanoseconds> TimingStats::within(double fraction) const
{
    if (count == 0)
    {
        return std::nullopt;
    }
    const auto needed = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(count)));
    std::size_t sofar = 0;
    for (std::size_t i = 0; i < kSteps; ++i)
    {
        sofar += buckets[i];
        if (sofar >= needed)
        {
            // The bucket's upper edge, but never past the latest seen.
            return std::min(kStep * static_cast<std::int64_t>(i + 1), max);
        }
    }
    return std::nullopt;
}

void SharedTimingStats::record(nanoseconds duration) noexcept
{
    add(buckets_[TimingStats::bucketOf(duration)], std::size_t{1});
    add(count_, std::size_t{1});
    if (duration.count() > max_.load(std::memory_order_relaxed))
    {
        max_.store(duration.count(), std::memory_order_relaxed);
    }
}

TimingStats SharedTimingStats::read() const noexcept
{
    TimingStats stats;
    stats.count = count_.load(std::memory_order_relaxed);
    stats.max = nanoseconds{max_.load(std::memory_order_relaxed)};
    for (std::size_t i = 0; i < buckets_.size(); ++i)
    {
        stats.buckets[i] = buckets_[i].load(std::memory_order_relaxed);
    }
    return stats;
}

} // namespace modal::core
