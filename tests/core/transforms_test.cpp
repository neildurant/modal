// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/meter_map.hpp>
#include <modal/core/transforms.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <variant>
#include <vector>

using namespace modal::core;

namespace
{

EventList listOf(const std::vector<Event>& events)
{
    EventList list;
    for (const auto& event : events)
    {
        list.insert(event);
    }
    return list;
}

std::vector<Event> eventsOf(const Transformed& result)
{
    REQUIRE(std::holds_alternative<EventList>(result));
    const auto& list = std::get<EventList>(result);
    return {list.all().begin(), list.all().end()};
}

Refusal refusalOf(const Transformed& result)
{
    REQUIRE(std::holds_alternative<Refusal>(result));
    return std::get<Refusal>(result);
}

Event note(Tick tick, std::uint8_t pitch, Tick length, std::uint8_t velocity = 100)
{
    return {tick, Note{pitch, velocity, length}};
}

Event pedal(Tick tick, std::uint8_t value, std::uint8_t controller = 64)
{
    return {tick, ControlChange{controller, value}};
}

const std::vector<std::size_t> kAll{0, 1, 2, 3, 4, 5, 6, 7};

std::vector<std::size_t> allOf(const EventList& list)
{
    return {kAll.begin(), kAll.begin() + static_cast<std::ptrdiff_t>(list.size())};
}

} // namespace

TEST_CASE("transposing moves only the notes taken, and refuses going past the ends")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q), pedal(q, 127), note(q, 64, q), note(2 * q, 67, q)});
    const std::vector<std::size_t> first{0, 1, 2}; // C, the pedal and E
    CHECK(eventsOf(transpose(list, first, 2)) ==
          std::vector{note(0, 62, q), pedal(q, 127), note(q, 66, q), note(2 * q, 67, q)});
    CHECK(eventsOf(transpose(list, first, -60)).front() == note(0, 0, q));

    const auto high = refusalOf(transpose(list, allOf(list), 61));
    CHECK(high.reason == Refusal::Reason::AboveHighest);
    CHECK(high.event == note(2 * q, 67, q));
    CHECK(refusalOf(transpose(list, allOf(list), -61)).reason == Refusal::Reason::BelowLowest);

    // Onto a note sounding at the pitch it goes to: refused, naming it.
    const auto held = listOf({note(0, 64, 4 * q), note(2 * q, 67, q)});
    const auto onto = refusalOf(transpose(held, std::vector<std::size_t>{1}, -3));
    CHECK(onto.reason == Refusal::Reason::Overlap);
    CHECK(onto.event == note(2 * q, 64, q));
    CHECK(onto.other == note(0, 64, 4 * q));
}

TEST_CASE("shifting moves events in time, running into a note of its pitch shortens it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick end = 8 * q;
    const auto list =
        listOf({note(0, 60, q), pedal(q / 2, 127), note(q, 64, q), note(2 * q, 60, q)});

    // The first two (the C and the pedal) a quarter later: the C runs into the next C.
    const std::vector<std::size_t> two{0, 1};
    CHECK(eventsOf(shift(list, two, q / 2, end, false)) ==
          std::vector{note(q / 2, 60, q), pedal(q, 127), note(q, 64, q), note(2 * q, 60, q)});
    const auto later = eventsOf(shift(list, two, 3 * q / 2, end, false));
    CHECK(later == std::vector{note(q, 64, q), note(3 * q / 2, 60, q / 2), pedal(2 * q, 127),
                               note(2 * q, 60, q)});

    // Only the notes, as in the piano roll.
    CHECK(eventsOf(shift(list, two, q / 2, end, true))[0] == pedal(q / 2, 127));

    // Onto the same start, before the start or past the end: refused.
    CHECK(refusalOf(shift(list, two, 2 * q, end, false)).reason == Refusal::Reason::Overlap);
    CHECK(refusalOf(shift(list, two, -1, end, false)).reason == Refusal::Reason::BeforeStart);
    CHECK(refusalOf(shift(list, allOf(list), 6 * q, end, false)).reason ==
          Refusal::Reason::PastEnd);
}

TEST_CASE("the nearest grid line, counted from each bar line")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    MeterMap meter{ppqn, {7, 8}};
    const Tick bar = 7 * q / 2;
    CHECK(nearestGridLine(meter, q / 3, q) == 0);
    CHECK(nearestGridLine(meter, q / 2, q) == q); // halfway: the later
    CHECK(nearestGridLine(meter, 3 * q + q / 5, q) == 3 * q);
    CHECK(nearestGridLine(meter, 3 * q + q / 3, q) == bar); // the bar line is nearer
    CHECK(nearestGridLine(meter, bar + q / 4, q) == bar);
    CHECK(nearestGridLine(meter, bar + 3 * q / 4, q) == bar + q);
}

TEST_CASE("quantising moves notes' starts, and pedals with them when asked")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const MeterMap meter{ppqn};
    const auto nearest = [&](Tick tick) { return nearestGridLine(meter, tick, q); };
    const Tick e = q / 8; // a little early or late

    // A chord played early, the pedal down just after it and up just before the next.
    const auto list = listOf({pedal(0, 0), note(q - e, 60, q), note(q - e, 64, q), pedal(q, 127),
                              pedal(2 * q - 2 * e, 0), note(2 * q + e, 62, q),
                              pedal(2 * q + 2 * e, 127), pedal(2 * q + 3 * e, 10, 1)});
    const auto all = allOf(list);
    CHECK(hasPedals(list, all));
    CHECK_FALSE(hasPedals(list, std::vector<std::size_t>{1, 2, 5}));

    // Without the pedals, only the notes move, their lengths kept.
    const auto plain = eventsOf(quantize(list, all, nearest, false));
    CHECK(plain == std::vector{pedal(0, 0), pedal(q, 127), note(q, 60, q), note(q, 64, q),
                               pedal(2 * q - 2 * e, 0), note(2 * q, 62, q),
                               pedal(2 * q + 2 * e, 127), pedal(2 * q + 3 * e, 10, 1)});

    // With them, each pedal by as far as the latest note at or before it moved; the one before
    // any note stays, and other controllers don't move.
    const auto pedalled = eventsOf(quantize(list, all, nearest, true));
    CHECK(pedalled == std::vector{pedal(0, 0), note(q, 60, q), note(q, 64, q), pedal(q + e, 127),
                                  pedal(2 * q - e, 0), note(2 * q, 62, q), pedal(2 * q + e, 127),
                                  pedal(2 * q + 3 * e, 10, 1)});
}

TEST_CASE("a pedal moved past the next of its pedal is refused")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const MeterMap meter{ppqn};
    const auto nearest = [&](Tick tick) { return nearestGridLine(meter, tick, q); };
    // A note played early, its pedal down just after it, and the pedal up on the beat outside
    // the range, which stays: the pedal down would move past it.
    const auto list = listOf({note(3 * q / 4, 60, q), pedal(7 * q / 8, 127), pedal(q, 0)});
    const auto refused = refusalOf(quantize(list, std::vector<std::size_t>{0, 1}, nearest, true));
    CHECK(refused.reason == Refusal::Reason::Order);
    CHECK(refused.event == pedal(9 * q / 8, 127));
    CHECK(refused.other == pedal(q, 0));
    CHECK(std::holds_alternative<EventList>(
        quantize(list, std::vector<std::size_t>{0, 1}, nearest, false)));
}

TEST_CASE("velocity changes are a value, a change or a percentage, kept within 1 to 127")
{
    CHECK(parseVelocityChange("80") == VelocityChange{VelocityChange::Kind::Set, 80});
    CHECK(parseVelocityChange(" +10 ") == VelocityChange{VelocityChange::Kind::Add, 10});
    CHECK(parseVelocityChange("-10") == VelocityChange{VelocityChange::Kind::Add, -10});
    CHECK(parseVelocityChange("80%") == VelocityChange{VelocityChange::Kind::Percent, 80});
    CHECK_FALSE(parseVelocityChange("0"));
    CHECK_FALSE(parseVelocityChange("128"));
    CHECK_FALSE(parseVelocityChange("loud"));
    CHECK_FALSE(parseVelocityChange(""));
    CHECK_FALSE(parseVelocityChange("+"));

    CHECK(changedVelocity(100, {VelocityChange::Kind::Add, 40}) == 127);
    CHECK(changedVelocity(5, {VelocityChange::Kind::Add, -10}) == 1);
    CHECK(changedVelocity(90, {VelocityChange::Kind::Percent, 50}) == 45);
    CHECK(changedVelocity(3, {VelocityChange::Kind::Percent, 10}) == 1);

    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q, 50), pedal(q, 127), note(q, 64, q, 70)});
    CHECK(eventsOf(changeVelocity(list, allOf(list), {VelocityChange::Kind::Add, 10})) ==
          std::vector{note(0, 60, q, 60), pedal(q, 127), note(q, 64, q, 80)});
}

TEST_CASE("humanising moves notes and velocities by up to the amounts, at random")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q), note(q, 60, q), pedal(q, 127), note(2 * q, 64, q)});
    // A random source that gives its highest, then its lowest, in turn.
    bool high = true;
    const auto random = [&](std::int64_t low, std::int64_t most)
    {
        high = !high;
        return high ? low : most;
    };
    const auto shifted =
        eventsOf(humanize(list, allOf(list), [&](Tick) { return q / 10; }, 8, 4 * q, random));
    // Each note: its time by the highest, its velocity by the lowest.
    CHECK(shifted == std::vector{note(q / 10, 60, q, 92), pedal(q, 127),
                                 note(q + q / 10, 60, q, 92), note(2 * q + q / 10, 64, q, 92)});
}
