// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/tick.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modal::core
{

// What a search looks for (decided November 2026): terms separated by spaces, all of which an
// event must match. A pitch (C1, F#3), a pitch class, any octave (C), pitches above or below
// one (>A3, <=C4) or in a range (A3-A5, ends included, a - before a letter marking it, so
// C-1-G2 is C-1 to G2); vel<60, vel>110, vel=100, vel=60-90; len<1/32, len>1/4, in note
// fractions; a controller, cc1 or cc01, with a value, a comparison or a range (cc1=127,
// cc64>63, cc7=20-40); bend, with one too (bend>0). Comparisons are <, <=, =, >= and >.
// Keywords are read in either case.
struct EventQuery
{
    enum class Compare : std::uint8_t
    {
        Less,
        LessOrEqual,
        Equal,
        GreaterOrEqual,
        Greater,
        Between, // from value to most, both included
    };
    struct Condition
    {
        Compare compare;
        int value;
        int most = 0;
        bool holds(int actual) const;
        friend bool operator==(const Condition&, const Condition&) = default;
    };
    struct LengthCondition
    {
        Compare compare;
        NoteFraction length;
        friend bool operator==(const LengthCondition&, const LengthCondition&) = default;
    };
    enum class Kind : std::uint8_t
    {
        Note,
        Controller,
        Bend,
    };

    Kind kind = Kind::Note;
    std::optional<std::uint8_t> pitch;
    std::optional<int> pitchClass;    // 0 for C to 11 for B
    std::optional<Condition> pitches; // above, below or between pitches
    std::optional<Condition> velocity;
    std::optional<LengthCondition> length;
    std::optional<std::uint8_t> controller;
    std::optional<Condition> value; // a controller's or a bend's

    friend bool operator==(const EventQuery&, const EventQuery&) = default;
};

// A query read from what's typed, or why it can't be, as a message to show.
struct ParsedQuery
{
    std::optional<EventQuery> query;
    std::string error;
};
ParsedQuery parseQuery(std::string_view text);

// Whether an event matches, lengths in ticks at this resolution.
bool matches(const EventQuery& query, const EventData& data, Ppqn ppqn);

} // namespace modal::core
