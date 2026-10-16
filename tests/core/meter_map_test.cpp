#include "support/ppqn.hpp"

#include <modal/core/meter_map.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using modal::core::BarBeatTick;
using modal::core::MeterChange;
using modal::core::MeterMap;
using modal::core::Tick;
using modal::core::TimeSignature;

namespace
{

constexpr TimeSignature k4_4{4, 4};
constexpr TimeSignature k3_4{3, 4};
constexpr TimeSignature k2_4{2, 4};
constexpr TimeSignature k1_4{1, 4};
constexpr TimeSignature k7_8{7, 8};
constexpr TimeSignature k6_8{6, 8};

std::vector<MeterChange> changesOf(const MeterMap& map)
{
    return {map.changes().begin(), map.changes().end()};
}

} // namespace

TEST_CASE("time signatures")
{
    CHECK(k4_4.isValid());
    CHECK(TimeSignature{5, 16}.isValid());
    CHECK(TimeSignature{255, 64}.isValid());
    CHECK_FALSE(TimeSignature{0, 4}.isValid());
    CHECK_FALSE(TimeSignature{4, 3}.isValid());
    CHECK_FALSE(TimeSignature{4, 128}.isValid());
    CHECK_FALSE(TimeSignature{256, 4}.isValid());

    CHECK(k6_8.beatLength() == modal::core::NoteFraction{1, 8});
    CHECK(k6_8.barLength() == modal::core::NoteFraction{3, 4});
}

TEST_CASE("bar:beat:tick in 4/4")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const MeterMap map{ppqn};
    const Tick q = ppqn.quarter();

    CHECK(map.changes().size() == 1);
    CHECK(map.toBarBeatTick(0) == BarBeatTick{1, 1, 0});
    CHECK(map.toBarBeatTick(q) == BarBeatTick{1, 2, 0});
    CHECK(map.toBarBeatTick(4 * q - 1) == BarBeatTick{1, 4, q - 1});
    CHECK(map.toBarBeatTick(4 * q) == BarBeatTick{2, 1, 0});
    CHECK(map.toBarBeatTick(9 * 4 * q + q + 5) == BarBeatTick{10, 2, 5});

    CHECK(map.barStart(1) == 0);
    CHECK(map.barStart(10) == 9 * 4 * q);
    CHECK(map.toTick({10, 2, 5}) == 9 * 4 * q + q + 5);
}

TEST_CASE("the beat is the denominator note")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const MeterMap map{ppqn, k6_8};
    const Tick eighth = ppqn.quarter() / 2;

    CHECK(map.toBarBeatTick(5 * eighth) == BarBeatTick{1, 6, 0});
    CHECK(map.toBarBeatTick(6 * eighth) == BarBeatTick{2, 1, 0});
}

TEST_CASE("tracks in different meters share time but not bars")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const MeterMap common{ppqn, k4_4};
    const MeterMap waltz{ppqn, k3_4};
    const Tick q = ppqn.quarter();

    CHECK(common.barStart(10) == 36 * q);
    CHECK(waltz.barStart(10) == 27 * q);
    CHECK(waltz.barStart(10) < common.barStart(10));

    // The same moment is a different bar on each track.
    CHECK(common.toBarBeatTick(12 * q) == BarBeatTick{4, 1, 0});
    CHECK(waltz.toBarBeatTick(12 * q) == BarBeatTick{5, 1, 0});
}

TEST_CASE("meter changes on bar lines")
{
    const auto ppqn = MODAL_ANY_PPQN();
    MeterMap map{ppqn};
    const Tick q = ppqn.quarter();
    const Tick eighth = q / 2;

    CHECK(map.set(8 * q, k7_8) == 8 * q);
    CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {8 * q, k7_8}});
    CHECK(map.signatureAt(8 * q - 1) == k4_4);
    CHECK(map.signatureAt(8 * q) == k7_8);
    CHECK(map.barStart(3) == 8 * q);
    CHECK(map.barStart(4) == 8 * q + 7 * eighth);
    CHECK(map.toBarBeatTick(8 * q + 7 * eighth + eighth) == BarBeatTick{4, 2, 0});
}

TEST_CASE("a mid-bar change ends the bar with the exact leftover")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();

    SECTION("at a beat")
    {
        MeterMap map{ppqn};
        // Bar 2, beat 3.
        CHECK(map.set(6 * q, k3_4) == 6 * q);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {4 * q, k2_4}, {6 * q, k3_4}});
        CHECK(map.toBarBeatTick(6 * q) == BarBeatTick{3, 1, 0});
        CHECK(map.barStart(4) == 9 * q);
    }

    SECTION("ten beats of 3/4, as in the design")
    {
        MeterMap map{ppqn, k3_4};
        map.set(10 * q, k4_4);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k3_4}, {9 * q, k1_4}, {10 * q, k4_4}});
        CHECK(map.barStart(4) == 9 * q);
        CHECK(map.barStart(5) == 10 * q);
    }

    SECTION("in the first bar after a change")
    {
        MeterMap map{ppqn};
        map.set(2 * q, k3_4);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k2_4}, {2 * q, k3_4}});
    }

    SECTION("between beats, using a finer denominator")
    {
        MeterMap map{ppqn};
        CHECK(map.set(4 * q + q / 2, k3_4) == 4 * q + q / 2);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4},
                                                         {4 * q, TimeSignature{1, 8}},
                                                         {4 * q + q / 2, k3_4}});
    }

    SECTION("at an odd tick, moving on to the next beat")
    {
        MeterMap map{ppqn};
        CHECK(map.set(4 * q + q + 1, k3_4) == 4 * q + 2 * q);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {4 * q, k2_4}, {6 * q, k3_4}});
    }
}

TEST_CASE("replacing and merging meter changes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    MeterMap map{ppqn};

    map.set(8 * q, k3_4);
    map.set(8 * q, k7_8);
    CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {8 * q, k7_8}});

    // Setting the meter already in effect adds nothing.
    map.set(4 * q, k4_4);
    CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {8 * q, k7_8}});

    // Replacing the first meter.
    map.set(0, k2_4);
    CHECK(changesOf(map) == std::vector<MeterChange>{{0, k2_4}, {8 * q, k7_8}});
    CHECK(map.barStart(5) == 8 * q);
}

TEST_CASE("removing meter changes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();

    SECTION("later changes stay on bar lines")
    {
        MeterMap map{ppqn};
        map.set(4 * q, k3_4);
        map.set(10 * q, k4_4);
        CHECK_FALSE(map.remove(5 * q));
        CHECK_FALSE(map.remove(0));
        CHECK(map.remove(4 * q));
        // 4/4 from 0 carries on; the change at 10 quarters is now mid-bar 3, so bar 3
        // becomes a 2/4 remainder and 4/4 resumes at 10 quarters.
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {8 * q, k2_4}, {10 * q, k4_4}});
        CHECK(map.barStart(4) == 10 * q);
    }

    SECTION("time is shared, so bars after the removed change keep their positions")
    {
        MeterMap map{ppqn};
        map.set(4 * q, k3_4);
        map.set(7 * q, k4_4);
        map.remove(4 * q);
        CHECK(changesOf(map) == std::vector<MeterChange>{{0, k4_4}, {4 * q, k3_4}, {7 * q, k4_4}});
    }
}

TEST_CASE("every tick round-trips through bar:beat:tick")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    MeterMap map{ppqn, k7_8};
    map.set(7 * q, k3_4);
    map.set(9 * q + q / 2, TimeSignature{5, 16});
    map.set(20 * q, k4_4);

    BarBeatTick previous{0, 0, 0};
    for (Tick tick = 0; tick < 30 * q; ++tick)
    {
        const auto position = map.toBarBeatTick(tick);
        INFO("ppqn " << ppqn.value << ", tick " << tick);
        REQUIRE(map.toTick(position) == tick);
        REQUIRE(position.bar >= previous.bar);
        if (position.tick == 0 && position.beat == 1)
        {
            REQUIRE(map.barStart(position.bar) == tick);
        }
        previous = position;
    }
}

TEST_CASE("positions outside their bar are rejected")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const MeterMap map{ppqn, k3_4};

    CHECK_FALSE(map.toTick({0, 1, 0}));
    CHECK_FALSE(map.toTick({1, 0, 0}));
    CHECK_FALSE(map.toTick({1, 4, 0}));
    CHECK_FALSE(map.toTick({1, 1, -1}));
    CHECK_FALSE(map.toTick({1, 1, ppqn.quarter()}));
    CHECK(map.toTick({1, 3, ppqn.quarter() - 1}));
}
