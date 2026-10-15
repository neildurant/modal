#include "support/ppqn.hpp"

#include <modal/core/tempo_map.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdint>

using modal::core::kDefaultTempo;
using modal::core::Tempo;
using modal::core::TempoChange;
using modal::core::TempoMap;
using modal::core::Tick;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

namespace
{

constexpr Tempo k60Bpm{1'000'000};
constexpr Tempo k90Bpm{666'667}; // not a whole number of nanoseconds per tick at any test PPQN
constexpr Tempo k120Bpm{500'000};

} // namespace

TEST_CASE("tempo converts to and from BPM")
{
    using Catch::Matchers::WithinRel;

    CHECK(Tempo::fromBpm(120.0) == k120Bpm);
    CHECK(Tempo::fromBpm(60.0) == k60Bpm);
    CHECK(Tempo::fromBpm(96.5) == Tempo{621'762}); // 621761.66 rounds to nearest
    CHECK_THAT(k120Bpm.bpm(), WithinRel(120.0));
    CHECK_THAT(Tempo{621'762}.bpm(), WithinRel(96.5, 1e-5));

    CHECK(Tempo::fromBpm(0.5) == Tempo{Tempo::kMaxMicrosPerQuarter});
    CHECK(Tempo::fromBpm(5000.0) == Tempo{Tempo::kMinMicrosPerQuarter});
    CHECK(Tempo::fromBpm(1.0).isValid());
    CHECK(Tempo::fromBpm(1000.0).isValid());
    CHECK_FALSE(Tempo{Tempo::kMinMicrosPerQuarter - 1}.isValid());
}

TEST_CASE("a new tempo map has one tempo at tick 0")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const TempoMap defaultMap{ppqn};
    REQUIRE(defaultMap.changes().size() == 1);
    CHECK(defaultMap.changes()[0] == TempoChange{0, kDefaultTempo});
    CHECK(defaultMap.ppqn() == ppqn);

    const TempoMap slow{ppqn, k60Bpm};
    CHECK(slow.tempoAt(0) == k60Bpm);
    CHECK(slow.tempoAt(1000 * ppqn.quarter()) == k60Bpm);
}

TEST_CASE("ticks convert to time at a single tempo")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const TempoMap map{ppqn, k120Bpm};

    CHECK(map.toTime(0) == 0ns);
    CHECK(map.toTime(ppqn.quarter()) == 500ms);
    CHECK(map.toTime(ppqn.whole()) == 2s);
    CHECK(map.toTime(ppqn.quarter() / 2) == 250ms);

    CHECK(map.toTick(0ns) == 0);
    CHECK(map.toTick(500ms) == ppqn.quarter());
    CHECK(map.toTick(2s) == ppqn.whole());
    CHECK(map.toTick(499ms) < ppqn.quarter());
}

TEST_CASE("tempo changes are steps")
{
    const auto ppqn = MODAL_ANY_PPQN();
    TempoMap map{ppqn, k120Bpm};
    const Tick bar2 = ppqn.whole();
    map.set(bar2, k60Bpm);

    CHECK(map.tempoAt(bar2 - 1) == k120Bpm);
    CHECK(map.tempoAt(bar2) == k60Bpm);
    CHECK(map.tempoAt(bar2 + 1) == k60Bpm);

    CHECK(map.toTime(bar2) == 2s);
    CHECK(map.toTime(bar2 + ppqn.quarter()) == 3s);
    CHECK(map.toTime(bar2 + ppqn.whole()) == 6s);

    CHECK(map.toTick(2s) == bar2);
    CHECK(map.toTick(3s) == bar2 + ppqn.quarter());
    CHECK(map.toTick(6s) == bar2 + ppqn.whole());
}

TEST_CASE("setting and removing tempo changes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    TempoMap map{ppqn, k120Bpm};
    const Tick q = ppqn.quarter();

    // Out of order, then replaced in place.
    map.set(8 * q, k90Bpm);
    map.set(4 * q, k60Bpm);
    map.set(8 * q, k120Bpm);
    REQUIRE(map.changes().size() == 3);
    CHECK(map.changes()[1] == TempoChange{4 * q, k60Bpm});
    CHECK(map.changes()[2] == TempoChange{8 * q, k120Bpm});

    // 4 quarters at 120, then 4 at 60.
    CHECK(map.toTime(8 * q) == 2s + 4s);

    // Removing the middle change makes everything after it earlier.
    CHECK(map.remove(4 * q));
    CHECK_FALSE(map.remove(4 * q));
    CHECK(map.toTime(8 * q) == 4s);

    // The first tempo can be replaced but not removed.
    CHECK_FALSE(map.remove(0));
    map.set(0, k60Bpm);
    CHECK(map.changes().size() == 2);
    CHECK(map.toTime(8 * q) == 8s);
}

TEST_CASE("every tick round-trips through time")
{
    const auto ppqn = MODAL_ANY_PPQN();
    TempoMap map{ppqn, k90Bpm};
    map.set(ppqn.whole() + 7, k120Bpm);
    map.set(2 * ppqn.whole() + 3, Tempo{Tempo::kMinMicrosPerQuarter});
    map.set(3 * ppqn.whole(), Tempo{Tempo::kMaxMicrosPerQuarter});

    nanoseconds previous{-1};
    for (Tick tick = 0; tick <= 4 * ppqn.whole(); ++tick)
    {
        const auto time = map.toTime(tick);
        INFO("ppqn " << ppqn.value << ", tick " << tick);
        REQUIRE(time > previous);
        REQUIRE(map.toTick(time) == tick);
        // The nanosecond before belongs to the previous tick.
        if (tick > 0)
        {
            REQUIRE(map.toTick(time - 1ns) == tick - 1);
        }
        previous = time;
    }
}

TEST_CASE("many tempo changes accumulate exactly")
{
    const auto ppqn = MODAL_ANY_PPQN();
    TempoMap map{ppqn, k120Bpm};
    const Tick q = ppqn.quarter();

    // Alternate 120 and 60 BPM every quarter note for 1000 quarters.
    nanoseconds expected{0};
    for (Tick i = 0; i < 1000; ++i)
    {
        map.set(i * q, i % 2 == 0 ? k120Bpm : k60Bpm);
        expected += i % 2 == 0 ? 500ms : 1s;
    }
    CHECK(map.toTime(1000 * q) == expected);
    CHECK(map.toTick(expected) == 1000 * q);
}

TEST_CASE("long songs don't overflow")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto day = std::chrono::duration_cast<nanoseconds>(std::chrono::hours{24});

    // A day at 120 BPM: two quarter notes a second.
    const TempoMap fast{ppqn, k120Bpm};
    const Tick dayOfQuarters = 24 * 60 * 60 * 2 * ppqn.quarter();
    CHECK(fast.toTime(dayOfQuarters) == day);
    CHECK(fast.toTick(day) == dayOfQuarters);

    // A year at the slowest and fastest tempos.
    const auto year = 365 * day;
    for (const Tempo tempo :
         {Tempo{Tempo::kMinMicrosPerQuarter}, Tempo{Tempo::kMaxMicrosPerQuarter}})
    {
        const TempoMap map{ppqn, tempo};
        const Tick tick = map.toTick(year);
        CHECK(map.toTime(tick) <= year);
        CHECK(map.toTime(tick + 1) > year);
    }
}
