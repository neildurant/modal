// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/pedalling.hpp>

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

Event note(Tick tick, std::uint8_t pitch, Tick length)
{
    return {tick, Note{pitch, 100, length}};
}

Event sustain(Tick tick, bool down)
{
    return {tick, ControlChange{64, static_cast<std::uint8_t>(down ? 127 : 0)}};
}

Event cc(Tick tick, int controller, int value)
{
    return {tick,
            ControlChange{static_cast<std::uint8_t>(controller), static_cast<std::uint8_t>(value)}};
}

EventList regionEdit(PedalRegionResult result)
{
    REQUIRE(std::holds_alternative<EventList>(result));
    return std::get<EventList>(std::move(result));
}

// The sustain's changes, as their ticks and whether down.
std::vector<std::pair<Tick, bool>> sustainOf(const Transformed& result)
{
    REQUIRE(std::holds_alternative<EventList>(result));
    std::vector<std::pair<Tick, bool>> found;
    for (const auto& event : std::get<EventList>(result).all())
    {
        if (const auto* change = std::get_if<ControlChange>(&event.data);
            change != nullptr && change->controller == 64)
        {
            found.emplace_back(event.tick, change->value >= 64);
        }
    }
    return found;
}

using Changes = std::vector<std::pair<Tick, bool>>;

} // namespace

TEST_CASE("pedal-region queries retain half-pedalling and isolate one controller")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto controller = GENERATE(64, 65, 66, 67, 68, 69);
    const auto cc = [&](Tick tick, int value)
    {
        return Event{tick, ControlChange{static_cast<std::uint8_t>(controller),
                                         static_cast<std::uint8_t>(value)}};
    };
    const auto events = listOf({{0, ControlChange{1, 10}},
                                cc(0, 20),
                                note(0, 60, q),
                                cc(q, 64),
                                {q + q / 4, ControlChange{1, 90}},
                                cc(q + q / 2, 90),
                                cc(q + 3 * q / 4, 127),
                                cc(2 * q, 63),
                                cc(2 * q, 30),
                                cc(3 * q, 127),
                                cc(3 * q, 100),
                                note(3 * q, 62, q)});
    const auto before = events;
    const auto regions = pedalRegions(events, controller, 4 * q);
    REQUIRE(regions.size() == 2);
    CHECK(regions[0] == PedalRegion{q, 2 * q, 3, 7});
    CHECK(regions[1] == PedalRegion{3 * q, 4 * q, 9, std::nullopt});
    CHECK(pedalRegionEvents(events, controller, regions[0]) ==
          std::vector<std::size_t>{3, 5, 6, 7});
    CHECK(pedalRegionEvents(events, controller, regions[1]) == std::vector<std::size_t>{9, 10});
    CHECK(events == before);
    CHECK(pedalRegions(events, 7, 4 * q).empty());
}

TEST_CASE("pedal-region discovery preserves same-tick controller ordering")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events =
        listOf({sustain(0, true), sustain(q, false), sustain(q, true), sustain(2 * q, false),
                sustain(2 * q, true), sustain(2 * q, false)});
    CHECK(pedalRegions(events, kSustain, 4 * q) ==
          std::vector<PedalRegion>{{0, q, 0, 1}, {q, 2 * q, 2, 3}, {2 * q, 2 * q, 4, 5}});
}

TEST_CASE("an unlifted pedal region has an effective end without inventing a lift")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events = listOf({sustain(q, true)});
    CHECK(pedalRegions(events, kSustain, 4 * q) ==
          std::vector<PedalRegion>{{q, 4 * q, 0, std::nullopt}});
    CHECK(pedalRegions(events, kSustain, q) ==
          std::vector<PedalRegion>{{q, q + 1, 0, std::nullopt}});
    CHECK(pedalRegions(EventList{}, kSustain, 4 * q).empty());
}

TEST_CASE("inserting a pedal region preserves existing events and stops before the next press")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto controller = GENERATE(64, 65, 66, 67, 68, 69);
    const auto events = listOf(
        {cc(0, 1, 10), note(0, 60, q), cc(2 * q, controller, 100), cc(3 * q, controller, 20)});
    const auto before = events;
    CHECK(
        regionEdit(insertPedalRegion(events, controller, q, 4 * q, 8 * q)) ==
        listOf({cc(0, 1, 10), note(0, 60, q), cc(q, controller, 127), cc(2 * q - 1, controller, 0),
                cc(2 * q, controller, 100), cc(3 * q, controller, 20)}));
    CHECK(insertPedalRegion(events, controller, 2 * q + q / 2, 4 * q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::AlreadyDown});
    CHECK(insertPedalRegion(events, controller, 2 * q - 1, 4 * q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::NoRoom});
    CHECK(insertPedalRegion(events, controller, q, q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::NoRoom});
    CHECK(insertPedalRegion(events, controller, -q, q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::BeforeStart});
    // Insertion can grow the song, as it did in the view.
    CHECK(regionEdit(insertPedalRegion(EventList{}, controller, 3 * q, 5 * q, 4 * q)) ==
          listOf({cc(3 * q, controller, 127), cc(5 * q, controller, 0)}));
    CHECK(events == before);
}

TEST_CASE("moving pedal regions retains their raw values and unrelated events")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto controller = GENERATE(64, 66, 69);
    const auto events = listOf({cc(q, controller, 64), cc(q, 1, 10), cc(q + q / 4, controller, 90),
                                note(q + q / 4, 60, q), cc(2 * q, controller, 20), cc(2 * q, 7, 5),
                                cc(4 * q, controller, 100), cc(5 * q, controller, 30)});
    const auto before = events;
    const auto regions = pedalRegions(events, controller, 8 * q);
    REQUIRE(regions.size() == 2);
    CHECK(regionEdit(movePedalRegions(events, controller, regions, q, 8 * q)) ==
          listOf({cc(q, 1, 10), note(q + q / 4, 60, q), cc(2 * q, 7, 5), cc(2 * q, controller, 64),
                  cc(2 * q + q / 4, controller, 90), cc(3 * q, controller, 20),
                  cc(5 * q, controller, 100), cc(6 * q, controller, 30)}));
    CHECK(movePedalRegions(events, controller, regions, -2 * q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::BeforeStart});
    // The first could move, but the second would reach the exclusive song end.
    CHECK(movePedalRegions(events, controller, regions, 3 * q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::PastEnd});
    CHECK(movePedalRegions(events, controller, std::span{regions}.first(1), 2 * q, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::Collision});
    CHECK(regionEdit(movePedalRegions(events, controller, regions, 0, 8 * q)) == before);
    CHECK(events == before);
}

TEST_CASE("moving touching imported pedal regions preserves their shared boundary order")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events =
        listOf({cc(q, 64, 80), cc(2 * q, 64, 20), cc(2 * q, 64, 100), cc(3 * q, 64, 30)});
    const auto regions = pedalRegions(events, 64, 8 * q);
    CHECK(regionEdit(movePedalRegions(events, 64, regions, q, 8 * q)) ==
          listOf({cc(2 * q, 64, 80), cc(3 * q, 64, 20), cc(3 * q, 64, 100), cc(4 * q, 64, 30)}));
}

TEST_CASE("resizing a pedal region retains its lift value and removes only cut-off values")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto controller = GENERATE(64, 66, 69);
    const auto events = listOf({cc(q, controller, 64), cc(q + q / 4, controller, 80),
                                cc(q + q / 2, controller, 110), note(q + q / 2, 60, q),
                                cc(q + 3 * q / 4, 1, 90), cc(2 * q, controller, 31)});
    const auto before = events;
    const auto regions = pedalRegions(events, controller, 8 * q);
    CHECK(
        regionEdit(resizePedalRegions(
            events, controller, regions, [&](Tick) { return q + q / 2; }, 8 * q)) ==
        listOf({cc(q, controller, 64), cc(q + q / 4, controller, 80), cc(q + q / 2, controller, 31),
                note(q + q / 2, 60, q), cc(q + 3 * q / 4, 1, 90)}));
    CHECK(regionEdit(resizePedalRegions(
              events, controller, regions, [&](Tick end) { return end + q; }, 8 * q)) ==
          listOf({cc(q, controller, 64), cc(q + q / 4, controller, 80),
                  cc(q + q / 2, controller, 110), note(q + q / 2, 60, q), cc(q + 3 * q / 4, 1, 90),
                  cc(3 * q, controller, 31)}));
    CHECK(events == before);
}

TEST_CASE("pedal-region resizing refuses the whole batch on a collision or invalid end")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events = listOf(
        {sustain(q, true), sustain(2 * q, false), sustain(3 * q, true), sustain(4 * q, false)});
    const auto before = events;
    const auto regions = pedalRegions(events, kSustain, 8 * q);
    for (const auto by : {q, q + q / 4})
    {
        CHECK(resizePedalRegions(
                  events, kSustain, regions, [&](Tick end) { return end + by; }, 8 * q) ==
              PedalRegionResult{PedalRegionRefusal::Collision});
    }
    CHECK(resizePedalRegions(
              events, kSustain, regions, [&](Tick) { return q; }, 8 * q) ==
          PedalRegionResult{PedalRegionRefusal::EmptyRegion});
    CHECK(resizePedalRegions(
              events, kSustain, regions,
              [&](Tick end) { return end == 2 * q ? end + q / 4 : 8 * q; },
              8 * q) == PedalRegionResult{PedalRegionRefusal::PastEnd});
    CHECK(events == before);
}

TEST_CASE("unlifted pedal regions move and delete their values without inventing a lift")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events =
        listOf({cc(q, 64, 80), cc(q + q / 2, 64, 90), cc(2 * q, 1, 20), note(2 * q, 60, q)});
    const auto before = events;
    const auto regions = pedalRegions(events, kSustain, 8 * q);
    REQUIRE(regions.size() == 1);
    CHECK_FALSE(regions[0].lift);
    bool destinationAsked = false;
    CHECK(resizePedalRegions(
              events, kSustain, regions,
              [&](Tick end)
              {
                  destinationAsked = true;
                  return end + q;
              },
              8 * q) == PedalRegionResult{PedalRegionRefusal::NoLift});
    CHECK_FALSE(destinationAsked);
    CHECK(regionEdit(movePedalRegions(events, kSustain, regions, q, 8 * q)) ==
          listOf({cc(2 * q, 1, 20), cc(2 * q, 64, 80), note(2 * q, 60, q),
                  cc(2 * q + q / 2, 64, 90)}));
    CHECK(erasePedalRegions(events, kSustain, regions) ==
          listOf({cc(2 * q, 1, 20), note(2 * q, 60, q)}));
    CHECK(events == before);
}

TEST_CASE("deleting a pedal region removes its press, values and lift only")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto events =
        listOf({cc(0, 64, 20), cc(q, 64, 80), cc(q + q / 2, 64, 90), cc(q + q / 2, 66, 127),
                note(q + q / 2, 60, q), cc(2 * q, 64, 30), cc(3 * q, 64, 100), cc(4 * q, 64, 10)});
    const auto regions = pedalRegions(events, kSustain, 8 * q);
    CHECK(erasePedalRegions(events, kSustain, std::span{regions}.first(1)) ==
          listOf({cc(0, 64, 20), cc(q + q / 2, 66, 127), note(q + q / 2, 60, q), cc(3 * q, 64, 100),
                  cc(4 * q, 64, 10)}));
}

TEST_CASE("re-pedalling replaces the sustain, lifting and pressing around each change")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const PedalTiming timing{[&](Tick t) { return t - d / 4; }, [&](Tick t) { return t + d / 2; }};
    const auto list = listOf({note(0, 60, q), note(q, 62, q), note(2 * q, 64, q),
                              sustain(q / 2, true), sustain(3 * q, false)});
    const std::vector<Tick> changes{0, q, 2 * q};
    CHECK(sustainOf(repedal(list, 0, 4 * q, changes, 3 * q, timing)) ==
          Changes{{d / 2, true},
                  {q - d / 4, false},
                  {q + d / 2, true},
                  {2 * q - d / 4, false},
                  {2 * q + d / 2, true},
                  {3 * q, false}});
    // Down as it starts: lifted there first, and pressed again where it ends, as it was.
    const auto held = listOf({sustain(0, true), note(q, 60, q)});
    CHECK(sustainOf(repedal(held, q, 4 * q, std::vector<Tick>{q}, 2 * q, timing)) ==
          Changes{{0, true}, {q, false}, {q + d / 2, true}, {2 * q, false}, {4 * q, true}});
}

TEST_CASE("re-pedalling changes nothing at or past the end of what it takes")
{
    // Found in a second review, November 2026: the pedal's state outside the stretch takes over
    // at its end, rather than a lift there or after it.
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick bar = 4 * q;
    const Tick d = q / 8;
    const PedalTiming timing{[&](Tick t) { return t - d / 4; }, [&](Tick t) { return t + d / 2; }};
    // Held down through two bars; a note ending just at the first bar's end.
    const auto held = listOf({sustain(0, true), note(0, 60, bar), sustain(2 * bar, false)});
    CHECK(sustainOf(repedal(held, 0, bar, std::vector<Tick>{0}, bar, timing)) ==
          Changes{{d / 2, true}, {bar, true}, {2 * bar, false}});
    // A note running past the end: no lift after it, the pedal staying down into what's held.
    CHECK(sustainOf(repedal(held, 0, bar, std::vector<Tick>{0}, bar + q, timing)) ==
          Changes{{d / 2, true}, {bar, true}, {2 * bar, false}});
    // Lifted outside: the lift comes at the end at the latest.
    const auto lifted = listOf({note(0, 60, bar + q)});
    CHECK(sustainOf(repedal(lifted, 0, bar, std::vector<Tick>{0}, bar + q, timing)) ==
          Changes{{d / 2, true}, {bar, false}});
}

TEST_CASE("re-timing moves the lifts and presses near a change to it, leaving the rest")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const PedalTiming timing{[&](Tick t) { return t - d / 4; }, [&](Tick t) { return t + d / 2; }};
    const auto list = listOf({sustain(d, true), sustain(q - d, false), sustain(q + d, true),
                              sustain(2 * q + 4 * d, false)});
    const std::vector<Tick> changes{0, q, 2 * q};
    CHECK(sustainOf(retimePedal(list, 0, 4 * q, changes, timing, [&](Tick) { return d; })) ==
          Changes{{d / 2, true}, {q - d / 4, false}, {q + d / 2, true}, {2 * q + 4 * d, false}});
}

TEST_CASE("cleaning the pedals removes repeats and flicker, keeping the first and last")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const auto list = listOf({sustain(0, true), sustain(d, true), sustain(q, false),
                              sustain(q + 1, true), sustain(2 * q, false), sustain(3 * q, false)});
    CHECK(sustainOf(cleanPedals(list, 0, 4 * q, [&](Tick) { return d; })) ==
          Changes{{0, true}, {2 * q, false}, {3 * q, false}});
}

TEST_CASE("clearing the pedals keeps the pedal's state at the stretch's edges")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({sustain(0, true), sustain(2 * q, false)});
    const std::vector<int> sustainOnly{64};
    CHECK(sustainOf(clearPedals(list, q, 3 * q, sustainOnly)) == Changes{{0, true}, {q, false}});
    CHECK(sustainOf(clearPedals(list, 0, q, sustainOnly)) == Changes{{q, true}, {2 * q, false}});
}

TEST_CASE("offsetting moves the pedal changes, refused past the song's end")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({sustain(0, true), note(q, 60, q), sustain(2 * q, false)});
    CHECK(sustainOf(offsetPedals(
              list, 0, 4 * q, [&](Tick) { return q / 8; }, 8 * q)) ==
          Changes{{q / 8, true}, {2 * q + q / 8, false}});
    CHECK(std::holds_alternative<Refusal>(
        offsetPedals(list, 0, 4 * q, [&](Tick) { return 6 * q; }, 8 * q)));
}

TEST_CASE("baking holds the notes under the sustain until it lifts, then clears it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto list = listOf({note(0, 60, q / 2), note(0, 64, q / 2), sustain(q / 4, true),
                              note(q, 60, q / 2), sustain(2 * q, false)});
    const auto result = bakePedal(list, 0, 4 * q);
    CHECK(sustainOf(result).empty());
    std::vector<Tick> lengths;
    for (const auto& event : std::get<EventList>(result).all())
    {
        if (const auto* n = std::get_if<Note>(&event.data))
        {
            lengths.push_back(n->duration);
        }
    }
    // The first C4 up to the next, the E4 and the second C4 to the lift.
    CHECK(lengths == std::vector<Tick>{q, 2 * q, q});
}

TEST_CASE("re-pedalling leaves out a press that wouldn't come before its lift")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const PedalTiming timing{[&](Tick t) { return t - d / 4; }, [&](Tick t) { return t + d / 2; }};
    const auto list = listOf({note(0, 60, q), note(q, 62, d / 4)});
    // A short last note: no press after its change, as it would come after the lift at its end.
    CHECK(sustainOf(repedal(list, 0, 4 * q, std::vector<Tick>{0, q}, q + d / 4, timing)) ==
          Changes{{d / 2, true}, {q - d / 4, false}});
    // Changes closer than the lift and press: the first's press would come after the second's
    // lift.
    CHECK(sustainOf(repedal(list, 0, 4 * q, std::vector<Tick>{0, d / 8}, 2 * q, timing)) ==
          Changes{{d / 8 + d / 2, true}, {2 * q, false}});
}

TEST_CASE("cleaning takes a lift and press for flicker only where the pedal was down before")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const auto list =
        listOf({sustain(0, false), sustain(q, false), sustain(q + 1, true), sustain(2 * q, false)});
    CHECK(sustainOf(cleanPedals(list, 0, 4 * q, [&](Tick) { return d; })) ==
          Changes{{0, false}, {q + 1, true}, {2 * q, false}});
}

TEST_CASE("re-timing is refused where a lift and press would change places")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick d = q / 8;
    const PedalTiming timing{[&](Tick t) { return t - d / 4; }, [&](Tick t) { return t + d / 2; }};
    const auto list = listOf({sustain(q - d, true), sustain(q, false)});
    CHECK(std::holds_alternative<Refusal>(
        retimePedal(list, 0, 4 * q, std::vector<Tick>{q}, timing, [&](Tick) { return 2 * d; })));
}

TEST_CASE("clearing a half pedal sets it again to its value, and baking holds notes from before")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const auto half = listOf({{0, ControlChange{64, 80}}, note(0, 60, q)});
    const std::vector<int> sustainOnly{64};
    const auto cleared = std::get<EventList>(clearPedals(half, 0, q, sustainOnly));
    CHECK(cleared.all().back() == Event{q, ControlChange{64, 80}});

    // A note before the stretch, held by the pedal into it, held to the lift as it was.
    const auto held = listOf({note(0, 60, q / 4), sustain(q / 8, true), sustain(3 * q, false)});
    const auto baked = std::get<EventList>(bakePedal(held, q, 2 * q));
    CHECK(std::get<Note>(baked.all().front().data).duration == 3 * q);
}
