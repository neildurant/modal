// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/timing_stats.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using modal::core::SharedTimingStats;
using modal::core::TimingStats;
using namespace std::chrono_literals;

TEST_CASE("durations fall in 50 us steps up to 20 ms")
{
    CHECK(TimingStats::bucketOf(-5us) == 0);
    CHECK(TimingStats::bucketOf(0us) == 0);
    CHECK(TimingStats::bucketOf(49'999ns) == 0);
    CHECK(TimingStats::bucketOf(50us) == 1);
    CHECK(TimingStats::bucketOf(2070us) == 41);
    CHECK(TimingStats::bucketOf(19'999us) == 399);
    CHECK(TimingStats::bucketOf(20ms) == 400);
    CHECK(TimingStats::bucketOf(1s) == 400);
}

TEST_CASE("the time that a fraction of durations were within")
{
    TimingStats stats;
    CHECK(stats.within(0.99) == std::nullopt);

    for (int i = 0; i < 90; ++i)
    {
        stats.record(10us);
    }
    for (int i = 0; i < 9; ++i)
    {
        stats.record(1900us);
    }
    stats.record(25ms);
    CHECK(stats.count == 100);
    CHECK(stats.max == 25ms);
    CHECK(stats.within(0.5) == 50us);
    CHECK(stats.within(0.9) == 50us);
    CHECK(stats.within(0.91) == 1950us);
    CHECK(stats.within(0.99) == 1950us);
    CHECK(stats.within(1.0) == std::nullopt); // one was over 20 ms
}

TEST_CASE("shared timing stats read back what was recorded")
{
    SharedTimingStats shared;
    TimingStats plain;
    for (const std::chrono::nanoseconds duration : {0us, 120us, 120us, 3000us, 30000us})
    {
        shared.record(duration);
        plain.record(duration);
    }
    const auto read = shared.read();
    CHECK(read.count == plain.count);
    CHECK(read.max == plain.max);
    CHECK(read.buckets == plain.buckets);
}

TEST_CASE("the time a fraction were within is never more than the longest")
{
    TimingStats stats;
    stats.record(30us);
    stats.record(10us);
    CHECK(stats.within(0.99) == 30us); // not the 50 us step's edge
}
