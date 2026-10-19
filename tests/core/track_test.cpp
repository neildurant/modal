#include "support/ppqn.hpp"

#include <modal/core/track.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <variant>
#include <vector>

using namespace modal::core;
using namespace std::chrono_literals;

namespace
{

std::vector<Event> eventsOf(const EventList& list)
{
    return {list.all().begin(), list.all().end()};
}

} // namespace

TEST_CASE("events are kept in tick order")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    EventList list;

    list.insert({2 * q, Note{64, 100, q}});
    list.insert({0, Note{60, 100, q}});
    list.insert({q, Note{62, 100, q}});

    REQUIRE(list.size() == 3);
    CHECK(list[0].tick == 0);
    CHECK(list[1].tick == q);
    CHECK(list[2].tick == 2 * q);
}

TEST_CASE("at the same tick, non-note events come before notes")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    EventList list;

    const Event chordLow{q, Note{48, 90, q}};
    const Event chordHigh{q, Note{55, 90, q}};
    const Event program{q, ProgramChange{5}};
    const Event volume{q, ControlChange{7, 100}};
    const Event before{0, ControlChange{10, 64}};
    const Event after{2 * q, ControlChange{10, 32}};

    list.insert(chordLow);
    list.insert(chordHigh);
    CHECK(list.insert(program) == 0);
    CHECK(list.insert(volume) == 1);
    list.insert(after);
    list.insert(before);

    CHECK(eventsOf(list) ==
          std::vector<Event>{before, program, volume, chordLow, chordHigh, after});
}

TEST_CASE("finding events by start time")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    EventList list;
    for (Tick i = 0; i < 8; ++i)
    {
        list.insert({i * q, Note{60, 100, q}});
    }

    CHECK(list.startingIn(0, 8 * q).size() == 8);
    CHECK(list.startingIn(2 * q, 4 * q).size() == 2);
    CHECK(list.startingIn(2 * q, 4 * q).front().tick == 2 * q);
    CHECK(list.startingIn(2 * q + 1, 3 * q).empty());
    CHECK(list.startingIn(8 * q, 100 * q).empty());
}

TEST_CASE("erasing events")
{
    EventList list;
    list.insert({0, Note{60, 100, 1}});
    list.insert({1, Note{61, 100, 1}});
    list.erase(0);
    REQUIRE(list.size() == 1);
    CHECK(std::get<Note>(list[0].data).pitch == 61);
}

TEST_CASE("track settings")
{
    Track track{TrackId{7}};

    CHECK(track.id() == TrackId{7});
    CHECK(track.channel == 1);
    CHECK(track.chaseNotes);
    CHECK(track.timeOffset == 0ms);
    CHECK(track.hasValidSettings());

    track.timeOffset = -40ms;
    track.transpose = -12;
    track.channel = 16;
    CHECK(track.hasValidSettings());

    track.timeOffset = 1001ms;
    CHECK_FALSE(track.hasValidSettings());
    track.timeOffset = -1000ms;
    CHECK(track.hasValidSettings());

    track.channel = 17;
    CHECK_FALSE(track.hasValidSettings());
    track.channel = 0;
    CHECK_FALSE(track.hasValidSettings());
}
