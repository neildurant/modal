// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/track_fields.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>

namespace modal::ui
{
namespace
{
constexpr std::array kColumns{
    TrackColumn{TrackField::Number, "No.", 4, true},
    TrackColumn{TrackField::Name, "Name", 16},
    TrackColumn{TrackField::Mute, "Mute", 5},
    TrackColumn{TrackField::Solo, "Solo", 5},
    TrackColumn{TrackField::ChaseNotes, "Chase", 6},
    TrackColumn{TrackField::Port, "Output port", 16},
    TrackColumn{TrackField::Channel, "Channel", 8, true},
    TrackColumn{TrackField::Transpose, "Transpose", 10, true},
    TrackColumn{TrackField::VelocityOffset, "Velocity +/-", 13, true},
    TrackColumn{TrackField::TimeOffset, "Time offset", 15, true},
};

std::string signedText(int value)
{
    return (value > 0 ? "+" : "") + std::to_string(value);
}
} // namespace

std::span<const TrackColumn> trackColumns()
{
    return kColumns;
}

std::string musicalOffsetText(core::TimeOffset offset, core::Ppqn ppqn)
{
    if (!offset.musical() || offset.amount() == 0)
        return core::offsetText(offset);
    const auto amount = offset.amount();
    const auto magnitude = std::abs(amount);
    std::optional<core::NoteFraction> nearest;
    auto distance = magnitude; // very small offsets stay in ticks
    const auto consider = [&](core::NoteFraction value)
    {
        const auto away = std::abs(magnitude - value.toTicks(ppqn));
        if (away < distance)
        {
            nearest = value;
            distance = away;
        }
    };
    // Prefer straight values on ties, then dotted and triplet values. Avoid arbitrary
    // fractions: the fine ticks make the display exact without obscure note names.
    for (std::int64_t d = 1; d <= 128; d *= 2)
        consider({1, d});
    for (std::int64_t n = 2; n <= core::TimeOffset::kMaxMusical.numerator(); ++n)
        consider({n, 1});
    for (std::int64_t d = 1; d <= 128; d *= 2)
    {
        consider(core::NoteFraction{1, d}.dotted());
        consider(core::NoteFraction{1, d}.triplet());
    }
    if (!nearest)
        return core::offsetText(offset);
    const auto fine = amount - (amount < 0 ? -1 : 1) * nearest->toTicks(ppqn);
    return (amount < 0 ? "-" : "+") + core::toString(*nearest) +
           (fine == 0 ? "" : (fine > 0 ? "+" : "") + std::to_string(fine) + "t");
}

std::string trackFieldText(TrackField field, const core::Track& track, std::size_t index,
                           core::Ppqn ppqn)
{
    switch (field)
    {
    case TrackField::Number:
        return std::to_string(index + 1);
    case TrackField::Name:
        return track.name;
    case TrackField::Mute:
        return track.mute ? "M" : "-";
    case TrackField::Solo:
        return track.solo ? "S" : "-";
    case TrackField::Port:
        return track.port;
    case TrackField::Channel:
        return std::to_string(track.channel);
    case TrackField::Transpose:
        return signedText(track.transpose);
    case TrackField::VelocityOffset:
        return signedText(track.velocityOffset);
    case TrackField::TimeOffset:
        return musicalOffsetText(track.timeOffset, ppqn);
    case TrackField::ChaseNotes:
        return track.chaseNotes ? "C" : "-";
    }
    return {};
}

std::optional<std::string> setTrackField(core::Track& track, TrackField field,
                                         std::string_view text, core::Ppqn ppqn)
{
    if (field == TrackField::Number)
    {
        return "The track number follows its position";
    }
    if (field == TrackField::Name)
    {
        track.name = text;
        return {};
    }
    if (field == TrackField::Port)
    {
        track.port = text;
        return {};
    }
    if (field == TrackField::TimeOffset)
    {
        const auto offset = core::parseTimeOffset(text, ppqn);
        if (!offset)
        {
            return "Use -40ms, +120t or +1/16-22t (up to 1000ms or 64 whole notes either way)";
        }
        track.timeOffset = *offset;
        return {};
    }
    if (field == TrackField::Mute || field == TrackField::Solo || field == TrackField::ChaseNotes)
    {
        const auto letter = field == TrackField::Mute ? "M" : field == TrackField::Solo ? "S" : "C";
        if (text != "on" && text != "off" && text != "1" && text != "0" && text != "-" &&
            text != letter)
        {
            return "Use on or off";
        }
        const bool on = text == "on" || text == "1" || text == letter;
        if (field == TrackField::Mute)
        {
            track.mute = on;
        }
        else if (field == TrackField::Solo)
        {
            track.solo = on;
        }
        else
        {
            track.chaseNotes = on;
        }
        return {};
    }
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
        if (text.starts_with('-') || text.starts_with('+'))
        {
            return "Enter a whole number";
        }
    }
    if (text.empty())
    {
        return "Enter a whole number";
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return "Enter a whole number";
    }
    if (field == TrackField::Channel)
    {
        if (value < core::Track::kMinChannel || value > core::Track::kMaxChannel)
        {
            return "Channel must be 1 to 16";
        }
        track.channel = value;
    }
    else
    {
        const auto limit = field == TrackField::Transpose ? core::Track::kMaxTranspose
                                                          : core::Track::kMaxVelocityOffset;
        if (value < -limit || value > limit)
        {
            return "The offset must be -127 to +127";
        }
        if (field == TrackField::Transpose)
        {
            track.transpose = value;
        }
        else
        {
            track.velocityOffset = value;
        }
    }
    return {};
}

std::optional<std::string> stepTrackField(core::Track& track, TrackField field, int steps,
                                          bool fine, core::Ppqn ppqn)
{
    if (field == TrackField::Mute || field == TrackField::Solo || field == TrackField::ChaseNotes)
    {
        return setTrackField(track, field, steps > 0 ? "on" : "off", ppqn);
    }
    if (field == TrackField::TimeOffset)
    {
        const bool musical = track.timeOffset.musical();
        const auto unit = musical && !fine ? std::max<core::Tick>(1, track.grid.toTicks(ppqn)) : 1;
        const auto limit = musical ? core::TimeOffset::kMaxMusical.toTicks(ppqn)
                                   : core::TimeOffset::kMaxMilliseconds.count();
        const auto current = track.timeOffset.amount();
        if ((steps > 0 && steps > (limit - current) / unit) ||
            (steps < 0 && -static_cast<std::int64_t>(steps) > (limit + current) / unit))
        {
            return "That step exceeds the time offset limit";
        }
        const auto amount = current + static_cast<std::int64_t>(steps) * unit;
        return setTrackField(track, field, std::to_string(amount) + (musical ? "t" : "ms"), ppqn);
    }
    if (field == TrackField::Channel || field == TrackField::Transpose ||
        field == TrackField::VelocityOffset)
    {
        const auto value = field == TrackField::Channel     ? track.channel
                           : field == TrackField::Transpose ? track.transpose
                                                            : track.velocityOffset;
        return setTrackField(track, field, std::to_string(static_cast<std::int64_t>(value) + steps),
                             ppqn);
    }
    return "Enter edits this field";
}
} // namespace modal::ui
