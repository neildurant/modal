// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/track_fields.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>

namespace modal::ui
{
namespace
{
constexpr std::array kColumns{
    TrackColumn{TrackField::Number, "No.", 4},
    TrackColumn{TrackField::Name, "Name", 18},
    TrackColumn{TrackField::Mute, "Mute", 5},
    TrackColumn{TrackField::Solo, "Solo", 5},
    TrackColumn{TrackField::Port, "Output port", 20},
    TrackColumn{TrackField::Channel, "Channel", 8},
    TrackColumn{TrackField::Transpose, "Transpose", 10},
    TrackColumn{TrackField::VelocityOffset, "Velocity +/-", 13},
    TrackColumn{TrackField::TimeOffset, "Time offset", 14},
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

std::string trackFieldText(TrackField field, const core::Track& track, std::size_t index)
{
    switch (field)
    {
    case TrackField::Number:
        return std::to_string(index + 1);
    case TrackField::Name:
        return track.name;
    case TrackField::Mute:
        return track.mute ? "on" : "off";
    case TrackField::Solo:
        return track.solo ? "on" : "off";
    case TrackField::Port:
        return track.port;
    case TrackField::Channel:
        return std::to_string(track.channel);
    case TrackField::Transpose:
        return signedText(track.transpose);
    case TrackField::VelocityOffset:
        return signedText(track.velocityOffset);
    case TrackField::TimeOffset:
        return core::offsetText(track.timeOffset);
    case TrackField::ChaseNotes:
        return track.chaseNotes ? "on" : "off";
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
        if (text != "on" && text != "off" && text != "1" && text != "0")
        {
            return "Use on or off";
        }
        const bool on = text == "on" || text == "1";
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
