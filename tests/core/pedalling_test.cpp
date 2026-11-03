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
