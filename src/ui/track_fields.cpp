// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/track_fields.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <variant>

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
    TrackColumn{TrackField::BankMsb, "MSB", 4, true},
    TrackColumn{TrackField::BankLsb, "LSB", 4, true},
    TrackColumn{TrackField::Program, "Program", 8, true},
    TrackColumn{TrackField::Transpose, "Transpose", 10, true},
    TrackColumn{TrackField::VelocityOffset, "Velocity +/-", 13, true},
    TrackColumn{TrackField::TimeOffset, "Time offset", 15, true},
    TrackColumn{TrackField::Grid, "Grid", 7},
    TrackColumn{TrackField::DefaultLength, "Length", 8},
    TrackColumn{TrackField::DefaultVelocity, "Vel", 4, true},
};

std::string signedText(int value)
{
    return (value > 0 ? "+" : "") + std::to_string(value);
}

std::optional<std::uint8_t>* setupPart(core::Track& track, TrackField field)
{
    auto& setup = track.programSetup;
    return field == TrackField::Program   ? &setup.program
           : field == TrackField::BankMsb ? &setup.bankMsb
           : field == TrackField::BankLsb ? &setup.bankLsb
                                          : nullptr;
}

std::string setupText(std::optional<std::uint8_t> value)
{
    return value ? std::to_string(*value) : "-";
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
        return core::offsetText(track.timeOffset);
    case TrackField::ChaseNotes:
        return track.chaseNotes ? "C" : "-";
    case TrackField::Program:
        return setupText(track.programSetup.program);
    case TrackField::BankMsb:
        return setupText(track.programSetup.bankMsb);
    case TrackField::BankLsb:
        return setupText(track.programSetup.bankLsb);
    case TrackField::Grid:
        return core::toString(track.grid);
    case TrackField::DefaultLength: // following the grid until set
        return track.noteLength ? core::toString(*track.noteLength) : "grid";
    case TrackField::DefaultVelocity:
        return std::to_string(track.noteVelocity);
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
        if (text != "on" && text != "off")
        {
            return "Use on or off";
        }
        const bool on = text == "on";
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
    if (field == TrackField::Grid || field == TrackField::DefaultLength)
    {
        if (field == TrackField::DefaultLength && (text.empty() || text == "grid"))
        {
            track.noteLength.reset();
            return {};
        }
        const auto fraction = core::parseNoteFraction(text);
        if (!fraction)
        {
            return field == TrackField::Grid ? "Use a note length, such as 1/16 or 1/8t"
                                             : "Use a note length such as 1/16, or grid to follow "
                                               "the grid";
        }
        if (field == TrackField::Grid)
        {
            track.grid = *fraction;
        }
        else
        {
            track.noteLength = *fraction;
        }
        return {};
    }
    auto* const setup = setupPart(track, field);
    if (setup != nullptr && (text.empty() || text == "-"))
    {
        setup->reset();
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
    if (setup != nullptr)
    {
        if (value < 0 || value > 127)
        {
            return "Enter 0 to 127, or nothing to leave it unset";
        }
        *setup = static_cast<std::uint8_t>(value);
    }
    else if (field == TrackField::DefaultVelocity)
    {
        if (value < 1 || value > 127)
        {
            return "The velocity must be 1 to 127";
        }
        track.noteVelocity = static_cast<std::uint8_t>(value);
    }
    else if (field == TrackField::Channel)
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
        const auto stepped = track.timeOffset.musical() && !fine
                                 ? track.timeOffset.stepped(track.grid, steps, ppqn)
                                 : track.timeOffset.nudged(steps);
        if (!stepped.valid(ppqn))
        {
            return "That step exceeds the time offset limit";
        }
        track.timeOffset = stepped;
        return {};
    }
    if (field == TrackField::Channel || field == TrackField::Transpose ||
        field == TrackField::VelocityOffset || field == TrackField::DefaultVelocity)
    {
        const int value = field == TrackField::Channel           ? track.channel
                          : field == TrackField::Transpose       ? track.transpose
                          : field == TrackField::DefaultVelocity ? int{track.noteVelocity}
                                                                 : track.velocityOffset;
        return setTrackField(track, field, std::to_string(static_cast<std::int64_t>(value) + steps),
                             ppqn);
    }
    if (auto* const setup = setupPart(track, field))
    {
        // From unset, up to the first number.
        const auto value = setup->has_value() ? static_cast<std::int64_t>(**setup) : -1;
        return setTrackField(track, field, std::to_string(value + steps), ppqn);
    }
    return "Enter edits this field";
}

ProgramEvents programEvents(const core::Song& song, core::TrackId track)
{
    ProgramEvents result;
    for (const auto& event : song.events(track).all())
    {
        if (std::holds_alternative<core::ProgramChange>(event.data))
        {
            result.first = result.count == 0 ? event.tick : result.first;
            ++result.count;
        }
    }
    return result;
}

bool programBefore(const core::Song& song, core::TrackId track, core::Tick tick)
{
    const auto found = programEvents(song, track);
    return found.count > 0 && found.first < tick;
}

std::vector<std::size_t> programClashes(const core::Song& song, const core::Track& track)
{
    std::vector<std::size_t> result;
    if (track.programSetup.empty())
    {
        return result;
    }
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& other = *tracks[i];
        if (other.id() != track.id() && other.port == track.port &&
            other.channel == track.channel && !other.programSetup.empty() &&
            other.programSetup != track.programSetup)
        {
            result.push_back(i);
        }
    }
    return result;
}
} // namespace modal::ui
