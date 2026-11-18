// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/meter_map.hpp>
#include <modal/core/transforms.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
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

std::vector<std::size_t> allOf(const EventList& list)
{
    std::vector<std::size_t> all(list.size());
    std::iota(all.begin(), all.end(), std::size_t{0});
    return all;
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

TEST_CASE("transposing and inverting in a key go by its degrees, notes outside it keeping their "
          "distance above the scale's")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto key = *parseKey("D dorian", builtInScales());
    const auto list = listOf({note(0, 62, q), note(q, 66, q), pedal(q, 127), note(2 * q, 72, q)});
    CHECK(eventsOf(transposeInKey(list, allOf(list), key, 2)) ==
          std::vector{note(0, 65, q), pedal(q, 127), note(q, 70, q), note(2 * q, 76, q)});
    CHECK(eventsOf(transposeInKey(list, allOf(list), key, -7)) ==
          std::vector{note(0, 50, q), pedal(q, 127), note(q, 54, q), note(2 * q, 60, q)});
    // About D4: F#4, F4 and one above, goes to B3 and one above.
    CHECK(eventsOf(invertInKey(list, allOf(list), key, 62)) ==
          std::vector{note(0, 62, q), pedal(q, 127), note(q, 60, q), note(2 * q, 52, q)});
    CHECK(eventsOf(invert(list, allOf(list), 62)) ==
          std::vector{note(0, 62, q), pedal(q, 127), note(q, 58, q), note(2 * q, 52, q)});
    CHECK(refusalOf(transposeInKey(list, allOf(list), key, 70)).reason ==
          Refusal::Reason::AboveHighest);
}

TEST_CASE("retrograde reverses the notes' order, mirroring the rhythm")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q), note(q, 64, 2 * q), pedal(q, 127)});
    CHECK(eventsOf(retrograde(list, allOf(list))) ==
          std::vector{note(0, 64, 2 * q), pedal(q, 127), note(2 * q, 60, q)});
}

TEST_CASE("quantising can go part of the way, and move ends or lengths instead of starts")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(q / 8, 60, q + q / 2), note(2 * q, 64, q - q / 8)});
    const auto nearest = [&](Tick tick) { return (tick + q / 2) / q * q; };
    const auto step = [&](Tick) { return q; };
    using Move = QuantizeHow::Move;
    CHECK(eventsOf(quantize(list, allOf(list), nearest, false, {.strength = 50})) ==
          std::vector{note(q / 16, 60, q + q / 2), note(2 * q, 64, q - q / 8)});
    // Ends: onto the grid, starts kept.
    CHECK(eventsOf(quantize(list, allOf(list), nearest, false, {.move = Move::Ends})) ==
          std::vector{note(q / 8, 60, 2 * q - q / 8), note(2 * q, 64, q)});
    // Both.
    CHECK(eventsOf(quantize(list, allOf(list), nearest, false, {.move = Move::Both})) ==
          std::vector{note(0, 60, 2 * q), note(2 * q, 64, q)});
    // Lengths, in whole steps, starts kept: 1.5 steps rounds to 2.
    CHECK(eventsOf(
              quantize(list, allOf(list), nearest, false, {.move = Move::Lengths, .step = step})) ==
          std::vector{note(q / 8, 60, 2 * q), note(2 * q, 64, q)});
}

TEST_CASE("swing moves every second grid line within its pair")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const MeterMap meter{ppqn};
    const auto eighth = q / 2;
    CHECK(nearestGridLine(meter, q / 2, eighth) == q / 2);
    CHECK(nearestGridLine(meter, q / 2, eighth, 66) == q * 66 / 100);
    CHECK(nearestGridLine(meter, q / 2 - q / 16, eighth, 66) == q * 66 / 100);
    CHECK(nearestGridLine(meter, q / 8, eighth, 66) == 0);
    CHECK(nearestGridLine(meter, q - q / 16, eighth, 66) == q);
    CHECK(nearestGridLine(meter, q / 2, eighth, 40) == q * 40 / 100);
}

TEST_CASE("stretching scales time and lengths about a tick, the range's first event")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(q, 60, q), note(2 * q, 64, q / 2), pedal(3 * q, 127)});
    CHECK(eventsOf(stretch(list, allOf(list), 2, 1, q, 100 * q, false)) ==
          std::vector{note(q, 60, 2 * q), note(3 * q, 64, q), pedal(5 * q, 127)});
    CHECK(eventsOf(stretch(list, allOf(list), 1, 2, q, 100 * q, true)) ==
          std::vector{note(q, 60, q / 2), note(q + q / 2, 64, q / 4), pedal(3 * q, 127)});
    CHECK(refusalOf(stretch(list, allOf(list), 4, 1, q, 6 * q, false)).reason ==
          Refusal::Reason::PastEnd);
}

TEST_CASE("legato holds each note or chord until the next, but for the last; lengths can be set or "
          "scaled")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf(
        {note(0, 60, q / 4), note(1, 64, q / 4), note(q, 67, q / 4), note(2 * q, 72, q / 8)});
    const auto together = [](Tick first, Tick tick) { return tick - first <= 2; };
    CHECK(eventsOf(changeLengths(list, allOf(list), LengthChange::Legato, 0, together)) ==
          std::vector{note(0, 60, q), note(1, 64, q - 1), note(q, 67, q), note(2 * q, 72, q / 8)});
    CHECK(eventsOf(changeLengths(list, allOf(list), LengthChange::Fixed, q / 2, together)) ==
          std::vector{note(0, 60, q / 2), note(1, 64, q / 2), note(q, 67, q / 2),
                      note(2 * q, 72, q / 2)});
    CHECK(eventsOf(changeLengths(list, allOf(list), LengthChange::Percent, 200, together)) ==
          std::vector{note(0, 60, q / 2), note(1, 64, q / 2), note(q, 67, q / 2),
                      note(2 * q, 72, q / 4)});
}

TEST_CASE("quantising a short note's start and end makes it a step long, not nothing")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(q + q / 8, 60, q / 8)});
    const auto nearest = [&](Tick tick) { return (tick + q / 2) / q * q; };
    const auto step = [&](Tick) { return q; };
    using Move = QuantizeHow::Move;
    CHECK(eventsOf(quantize(list, allOf(list), nearest, false,
                            {.move = Move::Both, .step = step})) == std::vector{note(q, 60, q)});
    CHECK(
        eventsOf(quantize(list, allOf(list), nearest, false, {.move = Move::Ends, .step = step})) ==
        std::vector{note(q + q / 8, 60, q - q / 8)});
}

TEST_CASE("thinning keeps each gesture's ends and events far enough apart and changed enough, "
          "never the switches'")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick tick = q / 48;
    std::vector<Event> all;
    for (int n = 0; n <= 20; ++n)
    {
        all.push_back({n * tick, ControlChange{1, static_cast<std::uint8_t>(n)}});
        all.push_back({n * tick, ControlChange{64, static_cast<std::uint8_t>(n % 2 * 127)}});
    }
    // A second gesture, long after: two events, both kept.
    all.push_back({10 * q, ControlChange{1, 50}});
    all.push_back({10 * q + tick, ControlChange{1, 50}});
    const auto list = listOf(all);
    const auto count = [](const Transformed& result, int controller)
    {
        return std::ranges::count_if(
            eventsOf(result), [&](const Event& event)
            { return std::get<ControlChange>(event.data).controller == controller; });
    };
    const auto thinned = thin(list, allOf(list), {}, 4 * tick, 2, q);
    CHECK(count(thinned, 1) == 8); // 0, 4, 8, 12, 16, 20 and the second gesture's two
    CHECK(count(thinned, 64) == 21);
    // Only another controller: none thinned.
    CHECK(count(thin(list, allOf(list), {ControlStreams::Kind::Controller, 7}, 4 * tick, 2, q),
                1) == 23);
}

TEST_CASE("thinning preserves every reset inside a dense gesture")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto unit = ppqn.quarter() / 16;
    using Curve = std::variant<ControlChange, PitchBend, ChannelPressure, PolyPressure>;
    const auto reset = GENERATE(Curve{ControlChange{74, 0}}, Curve{PitchBend{0}},
                                Curve{ChannelPressure{0}}, Curve{PolyPressure{60, 0}});
    const auto point = [&](int n, int value)
    {
        return std::visit(
            [&](auto data) -> Event
            {
                data.value = static_cast<decltype(data.value)>(value);
                return {n * unit, data};
            },
            reset);
    };
    const auto list =
        listOf({point(0, 5), point(1, 6), point(2, 0), point(3, 0), point(4, 5), point(5, 6)});
    ControlStreams what;
    if (GENERATE(false, true))
    {
        what = reset.index() == 0   ? ControlStreams{ControlStreams::Kind::Controller, 74}
               : reset.index() == 1 ? ControlStreams{ControlStreams::Kind::Bend}
                                    : ControlStreams{ControlStreams::Kind::Aftertouch};
    }
    CHECK(eventsOf(thin(list, allOf(list), what, ppqn.quarter(), 64, ppqn.quarter())) ==
          std::vector<Event>{point(0, 5), point(2, 0), point(3, 0), point(5, 6)});
}

TEST_CASE("thinning keeps the selected resets of the stream it thins")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto unit = q / 16;
    const auto list = listOf({pedal(0, 10, 1), pedal(unit, 0, 1), pedal(2 * unit, 0, 11),
                              pedal(3 * unit, 11, 1), pedal(4 * unit, 0, 1), pedal(5 * unit, 12, 1),
                              pedal(6 * unit, 127, 64), note(7 * unit, 60, q)});
    auto selected = allOf(list);
    selected.erase(selected.begin() + 1); // an unselected reset stays untouched
    CHECK(eventsOf(thin(list, selected, {ControlStreams::Kind::Controller, 1}, q, 64, q)) ==
          std::vector<Event>{list[0], list[1], list[2], list[4], list[5], list[6], list[7]});
}

TEST_CASE("a retained bend reset is the reference for later thinning decisions")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto unit = q / 8;
    SECTION("spacing is counted from the reset")
    {
        const auto list = listOf({{0, PitchBend{100}},
                                  {unit, PitchBend{90}},
                                  {2 * unit, PitchBend{0}},
                                  {3 * unit, PitchBend{-10}},
                                  {4 * unit, PitchBend{-11}},
                                  {5 * unit, PitchBend{-12}}});
        CHECK(eventsOf(thin(list, allOf(list), {}, 2 * unit, 2, q)) ==
              std::vector<Event>{list[0], list[2], list[4], list[5]});
    }
    SECTION("value change is measured from the reset")
    {
        const auto list = listOf({{0, PitchBend{5}},
                                  {unit, PitchBend{0}},
                                  {2 * unit, PitchBend{8}},
                                  {3 * unit, PitchBend{11}},
                                  {4 * unit, PitchBend{12}}});
        CHECK(eventsOf(thin(list, allOf(list), {}, 0, 10, q)) ==
              std::vector<Event>{list[0], list[1], list[3], list[4]});
    }
}

TEST_CASE("remapping changes a controller or a pitch, and removing drops the events chosen")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 36, q), pedal(0, 127, 1), note(q, 38, q)});
    CHECK(eventsOf(remapController(list, allOf(list), 1, 11)) ==
          std::vector{pedal(0, 127, 11), note(0, 36, q), note(q, 38, q)});
    CHECK(eventsOf(remapPitch(list, allOf(list), 36, 35)) ==
          std::vector{pedal(0, 127, 1), note(0, 35, q), note(q, 38, q)});
    CHECK(eventsOf(removed(list, allOf(list), [](const Event& e) { return e.isNote(); })) ==
          std::vector{pedal(0, 127, 1)});
}

TEST_CASE("stretching about a tick before the track's first event moves it too, in lowest terms")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(q, 60, q)});
    CHECK(eventsOf(stretch(list, allOf(list), 2, 1, 0, 100 * q, false)) ==
          std::vector{note(2 * q, 60, 2 * q)});
    CHECK(eventsOf(stretch(list, allOf(list), 7000, 7000, 0, 100 * q, false)) ==
          std::vector{note(q, 60, q)});
}

TEST_CASE("thinning leaves alone controllers that aren't curves")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    std::vector<Event> all;
    for (const int controller : {6, 69, 96, 0, 101, 7})
    {
        for (int n = 0; n < 4; ++n)
        {
            all.push_back({n, ControlChange{static_cast<std::uint8_t>(controller), 10}});
        }
    }
    const auto list = listOf(all);
    const auto kept = eventsOf(thin(list, allOf(list), {}, q, 2, q));
    CHECK(kept.size() == 22); // the volume's four down to its first and last, the rest kept
    CHECK_FALSE(isThinnable(6));
    CHECK_FALSE(isThinnable(38));
    CHECK_FALSE(isThinnable(69));
    CHECK(isThinnable(1));
    CHECK(isThinnable(11));
    CHECK(isThinnable(74));
}

TEST_CASE("thinning leaves the high resolution velocity prefix with its note")
{
    // CC88 gives the next note's velocity its low bits, so it's part of that note.
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    std::vector<Event> all;
    for (int n = 0; n < 3; ++n)
    {
        all.push_back({n * q, ControlChange{88, static_cast<std::uint8_t>(10 * n)}});
        all.push_back(note(n * q, 60, q / 2));
    }
    const auto list = listOf(all);
    CHECK(eventsOf(thin(list, allOf(list), {}, 4 * q, 2, q)).size() == 6);
    CHECK_FALSE(isThinnable(88));
    CHECK(isThinnable(87));
    CHECK(isThinnable(89));
}

TEST_CASE("legato holds a note until the next chord's earliest note, not its lowest")
{
    const auto list = listOf({note(0, 60, 10), note(100, 64, 10), note(120, 60, 10)});
    const auto together = [](Tick first, Tick tick) { return tick - first <= 30; };
    CHECK(eventsOf(changeLengths(list, allOf(list), LengthChange::Legato, 0, together)).front() ==
          note(0, 60, 100));
}

TEST_CASE("controller scaling preserves resets and shapes only selected continuous streams")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto list = listOf({{0, ControlChange{1, 0}},
                              {q, ControlChange{1, 20}},
                              {2 * q, ControlChange{1, 80}},
                              {3 * q, ControlChange{1, 100}},
                              {q, ControlChange{7, 60}},
                              {q, ControlChange{64, 127}},
                              {q, ControlChange{0, 4}},
                              note(0, 60, q)});
    const ControlStreams mod{ControlStreams::Kind::Controller, 1};
    const auto changed = [&](ControlScale how)
    { return eventsOf(scaleControls(list, allOf(list), mod, how)); };
    auto expected = std::vector<Event>{list.all().begin(), list.all().end()};
    const auto setting = [&](int at20, int at80, int at100)
    {
        auto result = expected;
        for (auto& event : result)
        {
            if (auto* cc = std::get_if<ControlChange>(&event.data); cc && cc->controller == 1)
            {
                cc->value = static_cast<std::uint8_t>(cc->value == 20    ? at20
                                                      : cc->value == 80  ? at80
                                                      : cc->value == 100 ? at100
                                                                         : 0);
            }
        }
        return result;
    };
    CHECK(changed({ControlScale::Kind::Percent, 50}) == setting(10, 40, 50));
    CHECK(changed({ControlScale::Kind::Offset, -40}) == setting(0, 40, 60));
    CHECK(changed({ControlScale::Kind::Offset, 70}) == setting(90, 127, 127));
    CHECK(changed({ControlScale::Kind::Fit, 30, 110}) == setting(30, 90, 110));
    CHECK(changed({ControlScale::Kind::Fit, 60, 60}) == setting(60, 60, 60));
    // Selecting only the 80 leaves the reset and other values out; a constant contour stays.
    const auto at80 =
        std::ranges::find_if(list.all(), [q](const auto& e) { return e.tick == 2 * q; });
    const std::vector<std::size_t> one{static_cast<std::size_t>(at80 - list.all().begin())};
    CHECK(eventsOf(scaleControls(list, one, mod, {ControlScale::Kind::Fit, 20, 100})) == expected);
    // A subset never collects another controller or the unselected values.
    CHECK(eventsOf(scaleControls(list, one, mod, {ControlScale::Kind::Percent, 200}))[one[0]] ==
          Event{2 * q, ControlChange{1, 127}});
}

TEST_CASE("controller centres, inversion and clipping respect pan, balance and bend")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto list = listOf({{0, ControlChange{10, 0}},
                              {q, ControlChange{10, 32}},
                              {2 * q, ControlChange{10, 64}},
                              {3 * q, ControlChange{10, 127}},
                              {0, ControlChange{8, 64}},
                              {q, ControlChange{8, 100}},
                              {0, PitchBend{-8192}},
                              {q, PitchBend{0}},
                              {2 * q, PitchBend{8191}},
                              {0, ChannelPressure{0}},
                              {q, ChannelPressure{5}},
                              {0, PolyPressure{60, 0}},
                              {q, PolyPressure{60, 11}}});
    const auto all = allOf(list);
    const auto values = [](const Transformed& changed, const auto& get)
    {
        std::vector<int> result;
        for (const auto& event : eventsOf(changed))
        {
            if (const auto value = get(event.data))
            {
                result.push_back(*value);
            }
        }
        return result;
    };
    const auto cc = [](int controller)
    {
        return [controller](const EventData& data) -> std::optional<int>
        {
            const auto* value = std::get_if<ControlChange>(&data);
            return value && value->controller == controller ? std::optional<int>{value->value}
                                                            : std::nullopt;
        };
    };
    const auto bend = [](const EventData& data) -> std::optional<int>
    {
        const auto* value = std::get_if<PitchBend>(&data);
        return value ? std::optional<int>{value->value} : std::nullopt;
    };
    CHECK(values(scaleControls(list, all, {}, {ControlScale::Kind::Percent, 50}), cc(10)) ==
          std::vector<int>{0, 48, 64, 96});
    CHECK(values(scaleControls(list, all, {}, {ControlScale::Kind::Percent, 50}), cc(8)) ==
          std::vector<int>{64, 82});
    CHECK(values(invertControls(list, all, {}), cc(10)) == std::vector<int>{127, 96, 64, 1});
    CHECK(values(invertControls(list, all, {}), cc(8)) == std::vector<int>{64, 28});
    CHECK(values(invertControls(list, all, {}), bend) == std::vector<int>{8191, 0, -8191});
    CHECK(values(scaleControls(list, all, {}, {ControlScale::Kind::Percent, 50}), bend) ==
          std::vector<int>{-4096, 0, 4096});
    CHECK(values(limitControls(list, all, {ControlStreams::Kind::Bend}, -1000, 1000), bend) ==
          std::vector<int>{-1000, 0, 1000});
    CHECK(values(limitControls(list, all, {ControlStreams::Kind::Bend}, 100, 1000), bend) ==
          std::vector<int>{100, 0, 1000});
    const auto pressure = [](const EventData& data) -> std::optional<int>
    {
        if (const auto* value = std::get_if<ChannelPressure>(&data))
        {
            return value->value;
        }
        if (const auto* value = std::get_if<PolyPressure>(&data))
        {
            return value->value;
        }
        return std::nullopt;
    };
    CHECK(values(scaleControls(list, all, {ControlStreams::Kind::Aftertouch},
                               {ControlScale::Kind::Percent, 50}),
                 pressure) == std::vector<int>{0, 0, 3, 6});
    CHECK(values(limitControls(list, all, {ControlStreams::Kind::Aftertouch}, 20, 100), pressure) ==
          std::vector<int>{0, 0, 20, 20});
    CHECK(values(invertControls(list, all, {ControlStreams::Kind::Aftertouch}), pressure) ==
          std::vector<int>{127, 127, 122, 116});
}

TEST_CASE("shaping fits each aftertouch stream separately and leaves controller actions alone")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto list = listOf({{0, ChannelPressure{10}},
                              {q, ChannelPressure{20}},
                              {0, PolyPressure{60, 40}},
                              {q, PolyPressure{60, 80}},
                              {0, PolyPressure{61, 70}},
                              {q, PolyPressure{61, 70}}});
    CHECK(eventsOf(scaleControls(list, allOf(list), {ControlStreams::Kind::Aftertouch},
                                 {ControlScale::Kind::Fit, 30, 90})) ==
          eventsOf(Transformed{listOf({{0, ChannelPressure{30}},
                                       {0, PolyPressure{60, 30}},
                                       {0, PolyPressure{61, 70}},
                                       {q, ChannelPressure{90}},
                                       {q, PolyPressure{60, 90}},
                                       {q, PolyPressure{61, 70}}})}));
    for (int cc = 0; cc <= 127; ++cc)
    {
        if (isThinnable(cc))
        {
            continue;
        }
        const auto actions = listOf({{0, ControlChange{static_cast<std::uint8_t>(cc), 23}}});
        const auto expected = std::vector<Event>{actions[0]};
        CHECK(eventsOf(invertControls(actions, allOf(actions), {})) == expected);
        CHECK(eventsOf(scaleControls(actions, allOf(actions), {},
                                     {ControlScale::Kind::Offset, 10})) == expected);
        CHECK(eventsOf(limitControls(actions, allOf(actions), {}, 30, 100)) == expected);
    }
}

TEST_CASE("controller shaping parses signed bend ranges and refuses invalid values")
{
    const ControlStreams cc{ControlStreams::Kind::Controller, 1};
    const ControlStreams bend{ControlStreams::Kind::Bend};
    REQUIRE(parseControlRange("-8192-8191", bend));
    CHECK(parseControlRange("-8192-8191", bend)->first == -8192);
    CHECK(parseControlRange("-4000--1000", bend)->second == -1000);
    CHECK(parseControlScale("+16383", bend)->first == 16383);
    CHECK(parseControlScale("1000%", cc)->first == 1000);
    CHECK(parseControlRange("0-127", cc)->second == 127);
    for (const auto text : {"-1-127", "0-128", "80-20", "20", "", "0-10x", "0-10-20"})
    {
        CHECK_FALSE(parseControlRange(text, cc));
    }
    for (const auto text : {"80", "1001%", "-10%", "+128", "80%extra", "9999999999999999", "+"})
    {
        CHECK_FALSE(parseControlScale(text, cc));
    }
    CHECK_FALSE(parseControlRange("-8193-8191", bend));
    CHECK_FALSE(parseControlRange("-8192-8192", bend));
}

TEST_CASE("controller smoothing uses original values in centred windows and keeps gesture ends")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto unit = q / 16;
    const auto list = listOf({{0, ControlChange{1, 10}},
                              {unit, ControlChange{1, 100}},
                              {2 * unit, ControlChange{1, 10}},
                              {3 * unit, ControlChange{1, 100}},
                              {4 * unit, ControlChange{1, 10}},
                              {unit, ControlChange{7, 70}},
                              {unit, ControlChange{64, 127}},
                              note(0, 60, q)});
    const auto values = [&](int strength, Tick width)
    {
        std::vector<int> result;
        const auto changed = smoothControls(
            list, allOf(list), {ControlStreams::Kind::Controller, 1},
            [width](Tick) { return width; }, strength, q);
        for (const auto& event : eventsOf(changed))
        {
            if (const auto* cc = std::get_if<ControlChange>(&event.data); cc && cc->controller == 1)
            {
                result.push_back(cc->value);
            }
        }
        return result;
    };
    CHECK(values(50, 2 * unit) == std::vector<int>{10, 70, 40, 70, 10});
    CHECK(values(100, 2 * unit) == std::vector<int>{10, 40, 70, 40, 10});
    CHECK(values(0, 2 * unit) == std::vector<int>{10, 100, 10, 100, 10});
    CHECK(values(100, unit) == std::vector<int>{10, 100, 10, 100, 10});
    const auto changed =
        eventsOf(smoothControls(list, allOf(list), {}, [unit](Tick) { return 2 * unit; }, 100, q));
    for (const auto& event : list.all())
    {
        if (const auto* cc = std::get_if<ControlChange>(&event.data); !cc || cc->controller != 1)
        {
            CHECK(std::ranges::find(changed, event) != changed.end());
        }
    }
}

TEST_CASE("controller smoothing preserves every reset, separate streams and unselected values")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto u = q / 16;
    const auto list = listOf({{0, PitchBend{-100}},
                              {u, PitchBend{100}},
                              {2 * u, PitchBend{0}},
                              {3 * u, PitchBend{0}},
                              {4 * u, PitchBend{100}},
                              {5 * u, PitchBend{-100}},
                              {2 * q, PitchBend{200}},
                              {2 * q + u, PitchBend{800}},
                              {2 * q + 2 * u, PitchBend{200}},
                              {0, ChannelPressure{10}},
                              {u, ChannelPressure{100}},
                              {2 * u, ChannelPressure{10}},
                              {0, PolyPressure{60, 90}},
                              {u, PolyPressure{60, 30}},
                              {2 * u, PolyPressure{60, 90}},
                              {0, PolyPressure{61, 50}},
                              {u, PolyPressure{61, 50}},
                              {2 * u, PolyPressure{61, 50}}});
    const auto changed =
        eventsOf(smoothControls(list, allOf(list), {}, [u](Tick) { return 2 * u; }, 100, q));
    std::vector<int> bends, pressure, poly60, poly61;
    for (const auto& event : changed)
    {
        if (const auto* b = std::get_if<PitchBend>(&event.data))
        {
            bends.push_back(b->value);
        }
        if (const auto* p = std::get_if<ChannelPressure>(&event.data))
        {
            pressure.push_back(p->value);
        }
        if (const auto* p = std::get_if<PolyPressure>(&event.data))
        {
            (p->pitch == 60 ? poly60 : poly61).push_back(p->value);
        }
    }
    CHECK(bends == std::vector<int>{-100, 0, 0, 0, 0, -100, 200, 400, 200});
    CHECK(pressure == std::vector<int>{10, 40, 10});
    CHECK(poly60 == std::vector<int>{90, 70, 90});
    CHECK(poly61 == std::vector<int>{50, 50, 50});
    const auto onlyPressure = eventsOf(smoothControls(
        list, allOf(list), {ControlStreams::Kind::Aftertouch}, [u](Tick) { return 2 * u; }, 50, q));
    for (const auto& event : list.all())
    {
        if (std::holds_alternative<PitchBend>(event.data))
        {
            CHECK(std::ranges::find(onlyPressure, event) != onlyPressure.end());
        }
    }
    // The selected points define the smoothing contour and its endpoints.
    const auto list2 = listOf({{0, ControlChange{1, 100}},
                               {u, ControlChange{1, 10}},
                               {2 * u, ControlChange{1, 100}},
                               {3 * u, ControlChange{1, 10}},
                               {4 * u, ControlChange{1, 100}}});
    const std::vector<std::size_t> selected{1, 2, 3};
    CHECK(eventsOf(smoothControls(
              list2, selected, {}, [u](Tick) { return 2 * u; }, 100, q)) ==
          eventsOf(Transformed{listOf({{0, ControlChange{1, 100}},
                                       {u, ControlChange{1, 10}},
                                       {2 * u, ControlChange{1, 40}},
                                       {3 * u, ControlChange{1, 10}},
                                       {4 * u, ControlChange{1, 100}}})}));
}

TEST_CASE("controller repeat removal uses held context, preserves unknown first values and "
          "isolates streams")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto list = listOf({{0, ControlChange{1, 0}},
                              {q, ControlChange{1, 0}},
                              {2 * q, ControlChange{1, 20}},
                              {3 * q, ControlChange{1, 20}},
                              {4 * q, ControlChange{1, 0}},
                              {5 * q, ControlChange{1, 0}},
                              {0, PitchBend{0}},
                              {q, PitchBend{0}},
                              {0, ChannelPressure{20}},
                              {q, ChannelPressure{20}},
                              {0, PolyPressure{60, 20}},
                              {q, PolyPressure{60, 20}},
                              {q, PolyPressure{61, 20}},
                              {0, ControlChange{64, 127}},
                              {q, ControlChange{64, 127}},
                              {0, ControlChange{0, 1}},
                              {q, ControlChange{0, 1}},
                              note(0, 60, q)});
    const auto expected = listOf({{0, ControlChange{1, 0}},
                                  {0, PitchBend{0}},
                                  {0, ChannelPressure{20}},
                                  {0, PolyPressure{60, 20}},
                                  {0, ControlChange{64, 127}},
                                  {0, ControlChange{0, 1}},
                                  note(0, 60, q),
                                  {q, PolyPressure{61, 20}},
                                  {q, ControlChange{64, 127}},
                                  {q, ControlChange{0, 1}},
                                  {2 * q, ControlChange{1, 20}},
                                  {4 * q, ControlChange{1, 0}}});
    CHECK(eventsOf(dedupeControls(list, allOf(list), {})) == eventsOf(Transformed{expected}));
    // An unselected earlier value still tells us what is held; later unselected events stay.
    const auto mod = listOf({{0, ControlChange{1, 40}},
                             {q, ControlChange{1, 40}},
                             {2 * q, ControlChange{1, 80}},
                             {3 * q, ControlChange{1, 40}},
                             {4 * q, ControlChange{1, 40}}});
    const std::vector<std::size_t> selected{1, 3};
    CHECK(eventsOf(dedupeControls(mod, selected, {})) ==
          eventsOf(Transformed{listOf({mod[0], mod[2], mod[3], mod[4]})}));
    const auto bendOnly = eventsOf(dedupeControls(list, allOf(list), {ControlStreams::Kind::Bend}));
    CHECK(bendOnly.size() == list.size() - 1);
    CHECK(std::ranges::count(bendOnly, Event{q, ControlChange{1, 0}}) == 1);
}

TEST_CASE("controller repeat removal forgets held values after reset, program and SysEx messages")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    const auto barrier = GENERATE(EventData{ControlChange{121, 0}}, EventData{ProgramChange{5}},
                                  EventData{SysEx{std::make_shared<const std::vector<std::uint8_t>>(
                                      std::initializer_list<std::uint8_t>{0xf0, 0x7e, 0xf7})}});
    const auto list = listOf({{0, ControlChange{1, 40}},
                              {0, PitchBend{10}},
                              {q, barrier},
                              {2 * q, ControlChange{1, 40}},
                              {2 * q, PitchBend{10}},
                              {3 * q, ControlChange{1, 40}},
                              {3 * q, PitchBend{10}}});
    const auto expected = listOf({list[0], list[1], list[2], list[3], list[4]});
    CHECK(eventsOf(dedupeControls(list, allOf(list), {})) == eventsOf(Transformed{expected}));
    const std::vector<std::size_t> selected{3, 4, 5, 6};
    CHECK(eventsOf(dedupeControls(list, selected, {})) == eventsOf(Transformed{expected}));
}

TEST_CASE("controller smoothing protects interior repeated resets for every continuous kind")
{
    const auto u = MODAL_ANY_PPQN().quarter() / 16;
    const auto reset = GENERATE(EventData{ControlChange{1, 0}}, EventData{PitchBend{0}},
                                EventData{ChannelPressure{0}}, EventData{PolyPressure{60, 0}});
    EventList events;
    Tick at = 0;
    for (const int value : {20, 80, 0, 0, 100, 20})
    {
        auto data = reset;
        std::visit(
            [value](auto& each)
            {
                if constexpr (requires { each.value; })
                {
                    each.value = static_cast<decltype(each.value)>(value);
                }
            },
            data);
        events.insert({at, data});
        at += u;
    }
    const auto result = eventsOf(
        smoothControls(events, allOf(events), {}, [u](Tick) { return 2 * u; }, 100, 16 * u));
    CHECK(result[0] == events[0]);
    CHECK(result[1] != events[1]);
    CHECK(result[2].data == reset);
    CHECK(result[3].data == reset);
    CHECK(result[5] == events[5]);
}

TEST_CASE("controller cleanup never shapes or deduplicates switches and controller actions")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    for (const int controller : {0, 32, 64, 65, 68, 88, 96, 98, 100, 120, 121, 127})
    {
        INFO(controller);
        const auto cc = static_cast<std::uint8_t>(controller);
        const auto events = listOf({{0, ControlChange{cc, 10}},
                                    {q / 8, ControlChange{cc, 100}},
                                    {q / 4, ControlChange{cc, 10}}});
        CHECK(eventsOf(smoothControls(
                  events, allOf(events), {}, [q](Tick) { return q; }, 100, q)) ==
              eventsOf(Transformed{events}));
        const auto repeats = listOf({{0, ControlChange{cc, 10}},
                                     {q / 8, ControlChange{cc, 10}},
                                     {q / 4, ControlChange{cc, 10}}});
        CHECK(eventsOf(dedupeControls(repeats, allOf(repeats), {})) ==
              eventsOf(Transformed{repeats}));
    }
}
