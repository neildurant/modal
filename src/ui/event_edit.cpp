// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/note_fraction.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <system_error>
#include <type_traits>
#include <vector>

namespace modal::ui
{

namespace
{

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::int64_t> number(std::string_view text)
{
    std::int64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

// A change typed with + or -, and the rest of the text; or none.
std::optional<std::pair<int, std::string_view>> change(std::string_view text)
{
    if (text.size() > 1 && (text.front() == '+' || text.front() == '-'))
    {
        return std::pair{text.front() == '+' ? 1 : -1, text.substr(1)};
    }
    return std::nullopt;
}

// A whole number, or a change to one, within a range.
std::optional<std::int64_t> numberIn(std::string_view text, std::int64_t current, std::int64_t min,
                                     std::int64_t max)
{
    std::optional<std::int64_t> value;
    if (const auto sign = change(text))
    {
        if (const auto by = number(sign->second))
        {
            value = current + sign->first * *by;
        }
    }
    else
    {
        value = number(text);
    }
    if (!value || *value < min || *value > max)
    {
        return std::nullopt;
    }
    return value;
}

// A length as bars:beats:ticks in a meter, or as a note value; none if it's neither.
std::optional<core::Tick> duration(std::string_view text, core::TimeSignature signature,
                                   core::Ppqn ppqn)
{
    if (text.find(':') == std::string_view::npos)
    {
        const auto fraction = core::parseNoteFraction(text);
        return fraction ? std::optional{fraction->toTicks(ppqn)} : std::nullopt;
    }
    const auto first = text.find(':');
    const auto second = text.find(':', first + 1);
    if (second == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto bars = number(text.substr(0, first));
    const auto beats = number(text.substr(first + 1, second - first - 1));
    const auto ticks = number(text.substr(second + 1));
    if (!bars || !beats || !ticks || *bars < 0 || *beats < 0 || *ticks < 0)
    {
        return std::nullopt;
    }
    return *bars * signature.barLength().toTicks(ppqn) +
           *beats * signature.beatLength().toTicks(ppqn) + *ticks;
}

// An absolute position, 3:2:120, 3:2 or 3, in a meter's bars; none if it isn't one there.
std::optional<core::Tick> position(std::string_view text, const core::MeterMap& meter)
{
    std::int64_t parts[3] = {0, 1, 0};
    int count = 0;
    while (true)
    {
        const auto colon = text.find(':');
        if (count == 3)
        {
            return std::nullopt;
        }
        const auto part = number(text.substr(0, colon));
        if (!part)
        {
            return std::nullopt;
        }
        parts[count++] = *part;
        if (colon == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(colon + 1);
    }
    return meter.toTick({parts[0], parts[1], parts[2]});
}

std::optional<std::uint8_t> pitchOf(std::string_view text, std::uint8_t current)
{
    if (const auto value = numberIn(text, current, 0, 127))
    {
        return static_cast<std::uint8_t>(*value);
    }
    return core::parsePitch(text);
}

std::optional<std::vector<std::uint8_t>> hexBytes(std::string_view text)
{
    std::vector<std::uint8_t> bytes;
    while (!(text = trimmed(text)).empty())
    {
        const auto space = text.find(' ');
        const auto word = text.substr(0, space);
        unsigned value = 0;
        const auto [ptr, error] =
            std::from_chars(word.data(), word.data() + word.size(), value, 16);
        if (word.size() != 2 || error != std::errc{} || ptr != word.data() + word.size())
        {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
        text.remove_prefix(word.size());
    }
    return bytes;
}

const std::string kPositionUsage =
    "A position is bar:beat:tick, such as 3:1:000, or a change such as +1/16 or -0:0:010";
const std::string kLengthUsage = "A length is bars:beats:ticks, such as 0:1:000, a note value "
                                 "such as 1/8., or a change such as +1/16";
const std::string kValueUsage = "A value must be from 0 to 127, or a change such as +10";
const std::string kVelocityUsage = "A velocity must be from 1 to 127, or a change such as +10";

} // namespace

std::optional<core::TimeSignature> signatureOf(std::string_view text, core::Ppqn ppqn)
{
    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    int numerator = 0;
    int denominator = 0;
    const auto top = text.substr(0, slash);
    const auto bottom = text.substr(slash + 1);
    const auto a = std::from_chars(top.data(), top.data() + top.size(), numerator);
    const auto b = std::from_chars(bottom.data(), bottom.data() + bottom.size(), denominator);
    const core::TimeSignature signature{numerator, denominator};
    if (a.ec != std::errc{} || a.ptr != top.data() + top.size() || b.ec != std::errc{} ||
        b.ptr != bottom.data() + bottom.size() || !signature.isValid() ||
        !signature.beatLength().isExact(ppqn))
    {
        return std::nullopt;
    }
    return signature;
}

EditResult setField(const core::Event& event, Column field, std::string_view text,
                    const core::MeterMap& meter)
{
    text = trimmed(text);
    const auto ppqn = meter.ppqn();
    const auto signature = meter.signatureAt(event.tick);
    core::Event result = event;

    if (field == Column::Position)
    {
        std::optional<core::Tick> tick;
        if (const auto sign = change(text))
        {
            if (const auto by = duration(sign->second, signature, ppqn))
            {
                tick = event.tick + sign->first * *by;
            }
        }
        else
        {
            tick = position(text, meter);
        }
        if (!tick || *tick < 0)
        {
            return kPositionUsage;
        }
        result.tick = *tick;
        return result;
    }

    const auto byte = [&](std::uint8_t& value, std::int64_t min,
                          const std::string& usage) -> std::optional<std::string>
    {
        const auto set = numberIn(text, value, min, 127);
        if (!set)
        {
            return usage;
        }
        value = static_cast<std::uint8_t>(*set);
        return std::nullopt;
    };
    const auto pitch = [&](std::uint8_t& value) -> std::optional<std::string>
    {
        const auto set = pitchOf(text, value);
        if (!set)
        {
            return "\"" + std::string{text} +
                   "\" isn't a pitch from C-1 to G9, or a change such "
                   "as +12";
        }
        value = *set;
        return std::nullopt;
    };

    std::optional<std::string> error;
    bool fits = true; // whether the event has the field
    std::visit(
        [&](auto& data)
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, core::Note>)
            {
                if (field == Column::Data)
                {
                    error = pitch(data.pitch);
                }
                else if (field == Column::Value)
                {
                    error = byte(data.velocity, 1, kVelocityUsage);
                }
                else if (field == Column::Release)
                {
                    error = byte(data.releaseVelocity, 0, kValueUsage);
                }
                else if (field == Column::Length)
                {
                    std::optional<core::Tick> length;
                    if (const auto sign = change(text))
                    {
                        if (const auto by = duration(sign->second, signature, ppqn))
                        {
                            length = data.duration + sign->first * *by;
                        }
                    }
                    else
                    {
                        length = duration(text, signature, ppqn);
                    }
                    if (!length || *length < 0)
                    {
                        error = kLengthUsage;
                    }
                    else
                    {
                        data.duration = *length;
                    }
                }
                else
                {
                    fits = false;
                }
            }
            else if constexpr (std::is_same_v<T, core::ControlChange>)
            {
                fits = field == Column::Data || field == Column::Value;
                if (fits)
                {
                    error =
                        byte(field == Column::Data ? data.controller : data.value, 0, kValueUsage);
                }
            }
            else if constexpr (std::is_same_v<T, core::ProgramChange>)
            {
                fits = field == Column::Data;
                if (fits)
                {
                    error = byte(data.program, 0, kValueUsage);
                }
            }
            else if constexpr (std::is_same_v<T, core::ChannelPressure>)
            {
                fits = field == Column::Value;
                if (fits)
                {
                    error = byte(data.value, 0, kValueUsage);
                }
            }
            else if constexpr (std::is_same_v<T, core::PolyPressure>)
            {
                fits = field == Column::Data || field == Column::Value;
                if (fits)
                {
                    error = field == Column::Data ? pitch(data.pitch)
                                                  : byte(data.value, 0, kValueUsage);
                }
            }
            else if constexpr (std::is_same_v<T, core::PitchBend>)
            {
                fits = field == Column::Value;
                const auto set =
                    numberIn(text, data.value, core::PitchBend::kMin, core::PitchBend::kMax);
                if (fits && !set)
                {
                    error = "A bend must be from -8192 to 8191, or a change such as +64";
                }
                else if (fits)
                {
                    data.value = static_cast<std::int16_t>(*set);
                }
            }
            else if constexpr (std::is_same_v<T, core::SysEx>)
            {
                fits = field == Column::Data;
                const auto bytes = hexBytes(text);
                if (fits && !bytes)
                {
                    error = "SysEx is bytes in hex, such as F0 7E 7F 09 01 F7";
                }
                else if (fits)
                {
                    data = core::makeSysEx(*bytes);
                }
            }
            else
            {
                fits = field == Column::Data;
                if (fits)
                {
                    data = core::makeMetaText(data.kind, std::string{text});
                }
            }
        },
        result.data);
    if (!fits)
    {
        return std::string{"That field can't be changed"};
    }
    if (error)
    {
        return *error;
    }
    if (!core::isValid(result))
    {
        return std::string{"SysEx must start with F0 and end with F7, with 7-bit bytes between"};
    }
    return result;
}

EditResult stepField(const core::Event& event, Column field, int steps, const core::MeterMap& meter)
{
    const auto ppqn = meter.ppqn();
    if (field == Column::Position)
    {
        core::Event result = event;
        const auto beat = meter.signatureAt(event.tick).beatLength().toTicks(ppqn);
        result.tick = std::max<core::Tick>(event.tick + steps * beat, 0);
        return result;
    }
    if (field == Column::Length && event.isNote())
    {
        core::Event result = event;
        auto& note = std::get<core::Note>(result.data);
        note.duration = std::max<core::Tick>(
            note.duration + steps * core::NoteFraction{1, 16}.toTicks(ppqn), 0);
        return result;
    }
    // The others are numbers stepped by 1 within their ranges, which setField checks: a step
    // past the range stops at its end.
    const auto* text = std::get_if<core::MetaText>(&event.data);
    if (text != nullptr || std::holds_alternative<core::SysEx>(event.data))
    {
        return std::string{"Text and SysEx can't be stepped"};
    }
    const auto sign = steps < 0 ? "-" : "+";
    auto result = setField(event, field, sign + std::to_string(std::abs(steps)), meter);
    if (std::holds_alternative<std::string>(result))
    {
        // Out of range: to its end, a step at a time.
        core::Event last = event;
        for (int i = 0; i < std::abs(steps); ++i)
        {
            auto next = setField(last, field, std::string{sign} + "1", meter);
            if (std::holds_alternative<std::string>(next))
            {
                break;
            }
            last = std::get<core::Event>(next);
        }
        return last;
    }
    return result;
}

std::optional<std::string> overlap(const core::EventList& events, std::optional<std::size_t> self,
                                   const core::Event& note, const core::MeterMap& meter)
{
    const auto* data = std::get_if<core::Note>(&note.data);
    if (data == nullptr)
    {
        return std::nullopt;
    }
    const auto end = note.tick + std::max<core::Tick>(data->duration, 1);
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto* other = std::get_if<core::Note>(&events[i].data);
        if (i == self || other == nullptr || other->pitch != data->pitch)
        {
            continue;
        }
        const auto start = events[i].tick;
        const auto otherEnd = start + std::max<core::Tick>(other->duration, 1);
        if (note.tick < otherEnd && start < end)
        {
            return std::string{start > note.tick ? "Would run into the " : "Would overlap the "} +
                   core::pitchName(other->pitch) + " at " + positionText(meter, start);
        }
    }
    return std::nullopt;
}

} // namespace modal::ui
