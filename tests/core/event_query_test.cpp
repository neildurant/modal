// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/event_query.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

using namespace modal::core;

namespace
{

bool found(std::string_view text, const EventData& data, Ppqn ppqn = kPpqn)
{
    const auto parsed = parseQuery(text);
    INFO(parsed.error);
    REQUIRE(parsed.query);
    return matches(*parsed.query, data, ppqn);
}

std::string error(std::string_view text)
{
    const auto parsed = parseQuery(text);
    CHECK_FALSE(parsed.query);
    return parsed.error;
}

} // namespace

TEST_CASE("a query finds notes by pitch, or pitch class in any octave")
{
    const Note c3{48, 100, 120};
    CHECK(found("C3", c3));
    CHECK(found("c3", c3));
    CHECK_FALSE(found("C4", c3));
    CHECK(found("C", c3));
    CHECK(found("B#", c3));
    CHECK_FALSE(found("C#", c3));
    CHECK_FALSE(found("C3", ControlChange{48, 100}));
}

TEST_CASE("a query finds notes by velocity and length, all its terms matching")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    const auto q = ppqn.quarter();
    const Note quiet{36, 50, q / 8}; // C2, a thirty-second
    const Note loud{36, 120, q};
    CHECK(found("vel<60", quiet, ppqn));
    CHECK_FALSE(found("vel<60", loud, ppqn));
    CHECK(found("VEL>110", loud, ppqn));
    CHECK(found("vel=50", quiet, ppqn));
    CHECK(found("vel>=50", quiet, ppqn));
    CHECK_FALSE(found("vel>50", quiet, ppqn));
    CHECK(found("vel<=50", quiet, ppqn));
    CHECK(found("len<1/16", quiet, ppqn));
    CHECK(found("len=1/32", quiet, ppqn));
    CHECK_FALSE(found("len<1/32", quiet, ppqn));
    CHECK(found("len>1/8", loud, ppqn));
    CHECK(found("C2 vel<60", quiet, ppqn));
    CHECK_FALSE(found("C2 vel<60", loud, ppqn));
    CHECK_FALSE(found("C3 vel<60", quiet, ppqn));
    CHECK(found("  C2   len<1/16 ", quiet, ppqn));
}

TEST_CASE("a query finds a controller, with leading zeros or without, and by its value")
{
    const ControlChange modulation{1, 127};
    CHECK(found("cc1", modulation));
    CHECK(found("cc01", modulation));
    CHECK(found("CC001", modulation));
    CHECK(found("cc1=127", modulation));
    CHECK_FALSE(found("cc1=0", modulation));
    CHECK_FALSE(found("cc64", modulation));
    CHECK(found("cc64>63", ControlChange{64, 127}));
    CHECK_FALSE(found("cc64>63", ControlChange{64, 0}));
    CHECK_FALSE(found("cc1", Note{1, 100, 10}));
}

TEST_CASE("a query finds pitch bends, by value if given")
{
    CHECK(found("bend", PitchBend{-100}));
    CHECK(found("bend<0", PitchBend{-100}));
    CHECK_FALSE(found("bend>0", PitchBend{-100}));
    CHECK(found("bend=-8192", PitchBend{-8192}));
    CHECK_FALSE(found("bend", ControlChange{1, 0}));
}

TEST_CASE("a query that can't be read says why")
{
    CHECK(error("") == "Nothing to search for");
    CHECK(error("H2") == "Can't search for \"H2\": try a pitch (C3), vel>100, len<1/16, cc64 or "
                         "bend");
    CHECK(error("vel") == "vel takes a comparison and a velocity, such as vel<60 or vel=60-90");
    CHECK(error("vel<200") == "vel takes a comparison and a velocity, such as vel<60 or vel=60-90");
    CHECK(error("len<x") == "len takes a comparison and a length, such as len<1/16");
    CHECK(
        error("cc") ==
        "cc takes a controller number, and a value if wanted, such as cc64, cc1=127 or cc7=20-40");
    CHECK(
        error("cc128") ==
        "cc takes a controller number, and a value if wanted, such as cc64, cc1=127 or cc7=20-40");
    CHECK(
        error("cc1=") ==
        "cc takes a controller number, and a value if wanted, such as cc64, cc1=127 or cc7=20-40");
    CHECK(error("bend>9000") == "bend takes a value if wanted, such as bend>0");
    CHECK(error("C3 cc64") == "\"cc64\" is for another kind of event than the terms before it");
    CHECK(error("C3 D3") == "Only one of each kind of term: \"D3\" is a second");
    CHECK(error("cc1 cc2") == "Only one of each kind of term: \"cc2\" is a second");
}

TEST_CASE("a query finds pitches above, below or between others, and numbers in a range")
{
    const Note a3{57, 80, 120};
    CHECK(found(">G3", a3));
    CHECK_FALSE(found(">A3", a3));
    CHECK(found(">=A3", a3));
    CHECK(found("<C4", a3));
    CHECK(found("<=a3", a3));
    CHECK(found("A3-A5", a3));
    CHECK(found("a5-a3", a3)); // either way round
    CHECK_FALSE(found("A#3-A5", a3));
    CHECK(found("C-1-G9", a3)); // C-1 the lowest C, the - before G the range
    CHECK(found("C-1", Note{0, 80, 120}));
    CHECK(found("A3-A5 vel=70-90", a3));
    CHECK_FALSE(found("vel=90-127", a3));
    CHECK(found("cc7=20-40", ControlChange{7, 40}));
    CHECK_FALSE(found("cc7=20-40", ControlChange{7, 41}));
    CHECK(found("bend=-8192--100", PitchBend{-200}));
    CHECK_FALSE(found("bend=-8192--100", PitchBend{-50}));
    CHECK(error(">A") == "Give an octave to compare with, such as >A3");
    CHECK(error("A3-B") == "A range of pitches has an octave at each end, such as A3-A5");
    CHECK(error("vel=60-") == "vel takes a comparison and a velocity, such as vel<60 or vel=60-90");
    CHECK(error("C3 >A3") == "Only one of each kind of term: \">A3\" is a second");
}
