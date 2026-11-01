// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/meter_map.hpp>
#include <modal/core/transforms.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
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

    constexpr VelocityLimits hard{.soft = false, .floor = 1};
    CHECK(changedVelocity(100, {VelocityChange::Kind::Add, 40}, hard) == 127);
    CHECK(changedVelocity(5, {VelocityChange::Kind::Add, -10}, hard) == 1);
    CHECK(changedVelocity(90, {VelocityChange::Kind::Percent, 50}, hard) == 45);
    CHECK(changedVelocity(3, {VelocityChange::Kind::Percent, 10}, hard) == 1);

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

TEST_CASE("cutting notes at a time splits those sounding across it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    // C4 for a half note, released at 90; E4 for a quarter; G4 from an eighth in; a pedal.
    const Event c4{0, Note{60, 100, 2 * q, 90}};
    const auto events = listOf({c4, note(0, 64, q), note(q / 2, 67, 2 * q), pedal(q / 2, 127)});
    // At q: the C4 and the G4 sound across it, the E4 ends there; only C4 and E4 are taken.
    const auto cut = cutAt(events, q, [](const Note& n) { return n.pitch != 67; });
    std::vector<std::pair<Tick, Note>> notes;
    for (const auto& event : cut.all())
    {
        if (const auto* n = std::get_if<Note>(&event.data))
        {
            notes.emplace_back(event.tick, *n);
        }
    }
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].first == 0);
    CHECK(notes[0].second.pitch == 60);
    CHECK(notes[0].second.duration == q);
    CHECK(notes[0].second.releaseVelocity == Note{}.releaseVelocity);
    CHECK(notes[1].second.pitch == 64); // ending at q, as it was
    CHECK(notes[1].second.duration == q);
    CHECK(notes[2].second.pitch == 67); // not taken: whole
    CHECK(notes[2].second.duration == 2 * q);
    CHECK(notes[3].first == q);
    CHECK(notes[3].second.pitch == 60);
    CHECK(notes[3].second.duration == q);
    CHECK(notes[3].second.releaseVelocity == 90);
    CHECK(notes[3].second.velocity == 100);
    CHECK(cut.size() == events.size() + 1); // the pedal too
}

TEST_CASE("relative velocity changes ease off near the limit they go towards, with a soft knee")
{
    using enum VelocityChange::Kind;
    // In the middle, in full.
    CHECK(changedVelocity(60, {Add, 10}) == 70);
    CHECK(changedVelocity(60, {Percent, 150}) == 90);
    // Towards the top, eased, short of 127, louder notes staying louder.
    const auto a = changedVelocity(100, {Add, 40});
    const auto b = changedVelocity(110, {Add, 40});
    CHECK(a > 111);
    CHECK(a < b);
    CHECK(b < 127);
    CHECK(changedVelocity(120, {Add, 5}) > 120);
    // Away from a limit, in full, even from it.
    CHECK(changedVelocity(127, {Add, -10}) == 117);
    CHECK(changedVelocity(5, {Add, 10}) == 15);
    // Towards the floor, eased, never below it.
    CHECK(changedVelocity(20, {Add, -30}) > 1);
    CHECK(changedVelocity(20, {Add, -30}, {.soft = true, .floor = 10}) > 10);
    CHECK(changedVelocity(60, {Set, 5}, {.soft = true, .floor = 10}) == 10);
    // Hard: clipped.
    CHECK(changedVelocity(100, {Add, 40}, {.soft = false, .floor = 1}) == 127);
}

TEST_CASE("lifting and lowering move velocities part of the way to the limits, keeping order")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list =
        listOf({note(0, 60, q, 27), pedal(q, 127), note(q, 64, q, 77), note(2 * q, 64, q, 127)});
    CHECK(eventsOf(liftVelocity(list, allOf(list), 50)) ==
          std::vector{note(0, 60, q, 77), pedal(q, 127), note(q, 64, q, 102),
                      note(2 * q, 64, q, 127)});
    CHECK(
        eventsOf(lowerVelocity(list, allOf(list), 50, {.soft = true, .floor = 7})) ==
        std::vector{note(0, 60, q, 17), pedal(q, 127), note(q, 64, q, 42), note(2 * q, 64, q, 67)});
    CHECK(eventsOf(limitVelocity(list, allOf(list), 40, 100)) ==
          std::vector{note(0, 60, q, 40), pedal(q, 127), note(q, 64, q, 77),
                      note(2 * q, 64, q, 100)});
    CHECK(parsePercent("25%") == 25);
    CHECK(parsePercent(" 25 ") == 25);
    CHECK_FALSE(parsePercent("101%"));
    CHECK_FALSE(parsePercent("lots"));
}

TEST_CASE("a velocity ramp goes from one value to the other by time, or scales the notes' own")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q, 50), note(0, 64, q, 70), note(q, 64, q, 70),
                              note(2 * q, 64, q, 70), note(4 * q, 60, q, 100)});
    CHECK(eventsOf(rampVelocity(list, allOf(list), {.percent = false, .from = 60, .to = 100})) ==
          std::vector{note(0, 60, q, 60), note(0, 64, q, 60), note(q, 64, q, 70),
                      note(2 * q, 64, q, 80), note(4 * q, 60, q, 100)});
    CHECK(eventsOf(rampVelocity(list, allOf(list), {.percent = true, .from = 100, .to = 50})) ==
          std::vector{note(0, 60, q, 50), note(0, 64, q, 70), note(q, 64, q, 61),
                      note(2 * q, 64, q, 53), note(4 * q, 60, q, 50)});
    CHECK(parseVelocityRange("60-100", true) == VelocityRamp{false, 60, 100});
    CHECK(parseVelocityRange(" 80% - 120% ", true) == VelocityRamp{true, 80, 120});
    CHECK_FALSE(parseVelocityRange("80%-120%", false));
    CHECK_FALSE(parseVelocityRange("80%-120", true));
    CHECK_FALSE(parseVelocityRange("0-100", false));
    CHECK_FALSE(parseVelocityRange("60", false));
}

TEST_CASE("compressing, expanding and shaping velocities work about each note's local average")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q, 40), pedal(q, 127), note(q, 64, q, 80)});
    const auto all = [&](Tick) { return 8 * q; };
    using Kind = VelocityDynamics::Kind;
    CHECK(eventsOf(velocityDynamics(list, allOf(list), {.kind = Kind::Compress, .window = all})) ==
          std::vector{note(0, 60, q, 50), pedal(q, 127), note(q, 64, q, 70)});
    CHECK(eventsOf(velocityDynamics(list, allOf(list), {.kind = Kind::Expand, .window = all})) ==
          std::vector{note(0, 60, q, 30), pedal(q, 127), note(q, 64, q, 90)});
    // A threshold leaves the others alone.
    CHECK(eventsOf(velocityDynamics(list, allOf(list),
                                    {.kind = Kind::Compress, .window = all, .above = 70})) ==
          std::vector{note(0, 60, q, 40), pedal(q, 127), note(q, 64, q, 70)});
    // A fixed centre.
    CHECK(eventsOf(velocityDynamics(list, allOf(list),
                                    {.kind = Kind::Compress,
                                     .percent = 100,
                                     .window = all,
                                     .centre = VelocityDynamics::Centre::Fixed,
                                     .fixed = 64})) ==
          std::vector{note(0, 60, q, 64), pedal(q, 127), note(q, 64, q, 64)});
    // A window holding each note alone: shaping scales its own distance from the average.
    const auto own = [](Tick) { return Tick{1}; };
    CHECK(eventsOf(velocityDynamics(list, allOf(list),
                                    {.kind = Kind::Shape, .percent = 150, .window = own})) ==
          std::vector{note(0, 60, q, 30), pedal(q, 127), note(q, 64, q, 90)});
}

TEST_CASE("the chord tools change each chord's velocities, leaving single notes alone")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list =
        listOf({note(0, 64, q, 70), note(0, 60, q, 60), note(1, 67, q, 80), note(q, 72, q, 90)});
    const auto together = [](Tick first, Tick tick) { return tick - first <= 2; };
    const auto chords = chordsOf(list, allOf(list), together);
    REQUIRE(chords.size() == 2);
    CHECK(chords[0].size() == 3);
    const auto with = [&](ChordVelocity how, VelocityRamp amount)
    {
        std::vector<int> velocities;
        for (const auto& event : eventsOf(chordVelocity(list, allOf(list), together, how, amount)))
        {
            velocities.push_back(std::get<Note>(event.data).velocity);
        }
        return velocities;
    };
    // In the list's order: E4 at 0, C4 at 0, G4 at 1, C5 at q.
    CHECK(with(ChordVelocity::Accent, {.from = 10}) == std::vector{60, 50, 90, 90});
    CHECK(with(ChordVelocity::Balance, {.from = 70, .to = 90}) == std::vector{80, 70, 90, 90});
    CHECK(with(ChordVelocity::Compress, {.from = 50}) == std::vector{70, 65, 75, 90});
    CHECK(with(ChordVelocity::Expand, {.from = 50}) == std::vector{70, 55, 85, 90});
}
