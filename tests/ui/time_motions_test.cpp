#include "support/ppqn.hpp"

#include <modal/ui/time_motions.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace modal;
using namespace modal::ui;
using namespace std::chrono_literals;

namespace
{

// 4/4 for two bars, then 7/8 from tick 8 quarters.
core::MeterMap mixed(core::Ppqn ppqn)
{
    core::MeterMap meter{ppqn};
    meter.set(8 * ppqn.quarter(), {7, 8});
    return meter;
}

} // namespace

TEST_CASE("beats on and back, counting the beat a tick is in when going back")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto e = q / 2;
    const auto meter = mixed(ppqn);

    CHECK(beatAfter(meter, 0, 1) == q);
    CHECK(beatAfter(meter, q / 3, 1) == q);
    CHECK(beatAfter(meter, 0, 3) == 3 * q);
    CHECK(beatAfter(meter, 7 * q, 2) == 8 * q + e); // into the 7/8, whose beats are eighths

    CHECK(beatBefore(meter, q, 1) == 0);
    CHECK(beatBefore(meter, q + 1, 1) == q);
    CHECK(beatBefore(meter, 8 * q + e, 2) == 7 * q);
    CHECK(beatBefore(meter, 0, 1) == 0);
}

TEST_CASE("bar lines on and back, counting the bar a tick is in when going back")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto bar78 = 7 * q / 2;
    const auto meter = mixed(ppqn);

    CHECK(barLineAfter(meter, 0, 1) == 4 * q);
    CHECK(barLineAfter(meter, 5 * q, 2) == 8 * q + bar78);
    CHECK(barLineBefore(meter, 5 * q, 1) == 4 * q);
    CHECK(barLineBefore(meter, 4 * q, 1) == 0);
    CHECK(barLineBefore(meter, 8 * q + bar78 + 1, 2) == 8 * q);
    CHECK(barLineBefore(meter, 0, 3) == 0);
}

TEST_CASE("the same position a bar on or back, kept to the end of a shorter bar")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto bar78 = 7 * q / 2;
    const auto meter = mixed(ppqn);

    CHECK(offsetInBar(meter, 5 * q + 10) == q + 10);
    CHECK(sameInBar(meter, 0, 1, q + 10) == 5 * q + 10);
    // Bar 2 is 4/4, bar 3 7/8: three and three quarters quarters in doesn't fit, so the last
    // tick of bar 3; the offset kept, a bar further on is again three and three quarters in.
    const auto offset = 3 * q + 3 * q / 4;
    CHECK(sameInBar(meter, 4 * q, 1, offset) == 8 * q + bar78 - 1);
    CHECK(sameInBar(meter, 8 * q, 1, offset) == 8 * q + 2 * bar78 - 1);
    CHECK(sameInBar(meter, 8 * q, -1, offset) == 4 * q + offset);
    CHECK(sameInBar(meter, 4 * q, -5, 0) == 0); // not before bar 1
}

TEST_CASE("the next or previous of some ticks, such as section starts")
{
    const std::vector<core::Tick> ticks{0, 100, 200};
    CHECK(nextOf(ticks, 0, 1) == 100);
    CHECK(nextOf(ticks, 50, 2) == 200);
    CHECK(nextOf(ticks, 200, 1) == std::nullopt);
    CHECK(previousOf(ticks, 150, 1) == 100);
    CHECK(previousOf(ticks, 100, 1) == 0);
    CHECK(previousOf(ticks, 150, 2) == 0);
    CHECK(previousOf(ticks, 0, 1) == std::nullopt);
}

TEST_CASE("notes sound together within a window in time, whatever the tempo")
{
    const auto ppqn = MODAL_ANY_PPQN();
    core::Song song{ppqn}; // 120 BPM: a quarter is 500 ms
    const auto q = ppqn.quarter();
    CHECK(together(song, 0, q * 30 / 500, 30ms));
    CHECK_FALSE(together(song, 0, q * 31 / 500 + 1, 30ms));

    song.updateTempo([&](core::TempoMap& tempo) { tempo.set(0, core::Tempo{1'000'000}); }); // 60
    CHECK(together(song, 0, q * 30 / 1000, 30ms));
    CHECK_FALSE(together(song, 0, q * 30 / 500, 30ms));
}
