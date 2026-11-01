// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/event_query.hpp>
#include <modal/core/pitch.hpp>

#include <charconv>
#include <system_error>
#include <variant>

namespace modal::core
{

namespace
{

using Compare = EventQuery::Compare;

// A comparison at the start of text, taken off it.
std::optional<Compare> takeCompare(std::string_view& text)
{
    for (const auto& [sign, compare] : {std::pair{std::string_view{"<="}, Compare::LessOrEqual},
                                        std::pair{std::string_view{">="}, Compare::GreaterOrEqual},
                                        std::pair{std::string_view{"<"}, Compare::Less},
                                        std::pair{std::string_view{">"}, Compare::Greater},
                                        std::pair{std::string_view{"="}, Compare::Equal}})
    {
        if (text.starts_with(sign))
        {
            text.remove_prefix(sign.size());
            return compare;
        }
    }
    return std::nullopt;
}

// A whole number making up all of text, from min to max.
std::optional<int> number(std::string_view text, int min, int max)
{
    int value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end || value < min || value > max)
    {
        return std::nullopt;
    }
    return value;
}

// A comparison and a number, or = and a range of them (=60-90), all of text.
std::optional<EventQuery::Condition> condition(std::string_view text, int min, int max)
{
    const auto compare = takeCompare(text);
    if (const auto dash = text.find('-', 1); compare == Compare::Equal && dash != text.npos)
    {
        const auto from = number(text.substr(0, dash), min, max);
        const auto to = number(text.substr(dash + 1), min, max);
        if (!from || !to)
        {
            return std::nullopt;
        }
        return EventQuery::Condition{Compare::Between, std::min(*from, *to), std::max(*from, *to)};
    }
    const auto value = compare ? number(text, min, max) : std::nullopt;
    if (!value)
    {
        return std::nullopt;
    }
    return EventQuery::Condition{*compare, *value};
}

// Where a range of pitches is split: a - before a letter, so C-1 stays a pitch.
std::size_t rangeDash(std::string_view text)
{
    for (std::size_t i = 1; i + 1 < text.size(); ++i)
    {
        const char next = static_cast<char>(text[i + 1] | 0x20);
        if (text[i] == '-' && next >= 'a' && next <= 'g')
        {
            return i;
        }
    }
    return std::string_view::npos;
}

template <typename T>
bool compared(Compare compare, T actual, T wanted)
{
    switch (compare)
    {
    case Compare::Between: // as Condition::holds takes it
    case Compare::Less:
        return actual < wanted;
    case Compare::LessOrEqual:
        return actual <= wanted;
    case Compare::Equal:
        return actual == wanted;
    case Compare::GreaterOrEqual:
        return actual >= wanted;
    case Compare::Greater:
        return actual > wanted;
    }
    return false;
}

std::string lowered(std::string_view text)
{
    std::string lower{text};
    for (auto& c : lower)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lower;
}

} // namespace

bool EventQuery::Condition::holds(int actual) const
{
    return compare == Compare::Between ? actual >= value && actual <= most
                                       : compared(compare, actual, value);
}

ParsedQuery parseQuery(std::string_view text)
{
    EventQuery query;
    std::optional<EventQuery::Kind> kind;
    const auto fail = [](std::string message)
    { return ParsedQuery{std::nullopt, std::move(message)}; };
    while (true)
    {
        while (text.starts_with(' '))
        {
            text.remove_prefix(1);
        }
        if (text.empty())
        {
            break;
        }
        const auto space = text.find(' ');
        const auto term = text.substr(0, space);
        text.remove_prefix(space == std::string_view::npos ? text.size() : space);
        const auto lower = lowered(term);
        const std::string_view word{lower};
        const auto quoted = "\"" + std::string{term} + "\"";

        EventQuery::Kind termKind = EventQuery::Kind::Note;
        bool twice = false;
        if (word.starts_with("vel"))
        {
            const auto velocity = condition(word.substr(3), 0, 127);
            if (!velocity)
            {
                return fail("vel takes a comparison and a velocity, such as vel<60 or vel=60-90");
            }
            twice = query.velocity.has_value();
            query.velocity = velocity;
        }
        else if (word.starts_with("len"))
        {
            auto rest = word.substr(3);
            const auto compare = takeCompare(rest);
            const auto length = compare ? parseNoteFraction(rest) : std::nullopt;
            if (!length)
            {
                return fail("len takes a comparison and a length, such as len<1/16");
            }
            twice = query.length.has_value();
            query.length = EventQuery::LengthCondition{*compare, *length};
        }
        else if (word.starts_with("cc"))
        {
            termKind = EventQuery::Kind::Controller;
            auto rest = word.substr(2);
            const auto digits = std::min(rest.find_first_not_of("0123456789"), rest.size());
            const auto controller = number(rest.substr(0, digits), 0, 127);
            rest.remove_prefix(digits);
            const auto value = rest.empty() ? std::nullopt : condition(rest, 0, 127);
            if (!controller || (!rest.empty() && !value))
            {
                return fail("cc takes a controller number, and a value if wanted, such as cc64, "
                            "cc1=127 or cc7=20-40");
            }
            twice = query.controller.has_value();
            query.controller = static_cast<std::uint8_t>(*controller);
            query.value = value;
        }
        else if (word.starts_with("bend"))
        {
            termKind = EventQuery::Kind::Bend;
            const auto rest = word.substr(4);
            const auto value =
                rest.empty() ? std::nullopt : condition(rest, PitchBend::kMin, PitchBend::kMax);
            if (!rest.empty() && !value)
            {
                return fail("bend takes a value if wanted, such as bend>0");
            }
            twice = kind == EventQuery::Kind::Bend;
            query.value = value;
        }
        else if (const auto dash = rangeDash(term); dash != std::string_view::npos)
        {
            const auto from = parsePitch(term.substr(0, dash));
            const auto to = parsePitch(term.substr(dash + 1));
            if (!from || !to)
            {
                return fail("A range of pitches has an octave at each end, such as A3-A5");
            }
            twice = query.pitch || query.pitchClass || query.pitches;
            query.pitches =
                EventQuery::Condition{Compare::Between, std::min(*from, *to), std::max(*from, *to)};
        }
        else if (term.starts_with('<') || term.starts_with('>') || term.starts_with('='))
        {
            auto rest = term;
            const auto compare = takeCompare(rest);
            const auto pitch = parsePitch(rest);
            if (!pitch)
            {
                return fail("Give an octave to compare with, such as >A3");
            }
            twice = query.pitch || query.pitchClass || query.pitches;
            query.pitches = EventQuery::Condition{*compare, *pitch};
        }
        else if (const auto pitch = parsePitch(term))
        {
            twice = query.pitch || query.pitchClass || query.pitches;
            query.pitch = *pitch;
        }
        else if (const auto pitchClass = parsePitchClass(term))
        {
            twice = query.pitch || query.pitchClass || query.pitches;
            query.pitchClass = *pitchClass;
        }
        else
        {
            return fail("Can't search for " + quoted +
                        ": try a pitch (C3), vel>100, len<1/16, cc64 or bend");
        }
        if (twice)
        {
            return fail("Only one of each kind of term: " + quoted + " is a second");
        }
        if (kind && *kind != termKind)
        {
            return fail(quoted + " is for another kind of event than the terms before it");
        }
        kind = termKind;
    }
    if (!kind)
    {
        return fail("Nothing to search for");
    }
    query.kind = *kind;
    return {query, {}};
}

bool matches(const EventQuery& query, const EventData& data, Ppqn ppqn)
{
    switch (query.kind)
    {
    case EventQuery::Kind::Note:
    {
        const auto* note = std::get_if<Note>(&data);
        return note != nullptr && (!query.pitch || note->pitch == *query.pitch) &&
               (!query.pitchClass || note->pitch % 12 == *query.pitchClass) &&
               (!query.pitches || query.pitches->holds(note->pitch)) &&
               (!query.velocity || query.velocity->holds(note->velocity)) &&
               (!query.length || compared(query.length->compare, note->duration,
                                          query.length->length.toTicks(ppqn)));
    }
    case EventQuery::Kind::Controller:
    {
        const auto* change = std::get_if<ControlChange>(&data);
        return change != nullptr && change->controller == *query.controller &&
               (!query.value || query.value->holds(change->value));
    }
    case EventQuery::Kind::Bend:
    {
        const auto* bend = std::get_if<PitchBend>(&data);
        return bend != nullptr && (!query.value || query.value->holds(bend->value));
    }
    }
    return false;
}

} // namespace modal::core
