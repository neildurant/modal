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
    // The buttons as wide as each other, so they're evenly spaced, and close together, with no
    // headings over them (decided November 2026): their letters say what they are.
    TrackColumn{TrackField::Mute, "Mute", 4},
    TrackColumn{TrackField::Solo, "Solo", 4},
    TrackColumn{TrackField::Arm, "Arm", 4},
    TrackColumn{TrackField::Thru, "Thru", 4},
    TrackColumn{TrackField::ChaseNotes, "Chase", 4},
    TrackColumn{TrackField::Quantize, "Quant", 4},
    TrackColumn{TrackField::Port, "Output port", 12},
    TrackColumn{TrackField::Channel, "Ch", 4, true},
    TrackColumn{TrackField::BankMsb, "MSB", 4, true},
    TrackColumn{TrackField::BankLsb, "LSB", 4, true},
    TrackColumn{TrackField::Program, "Prog", 6, true},
    TrackColumn{TrackField::Transpose, "Trans", 6, true},
    TrackColumn{TrackField::VelocityOffset, "Vel +/-", 8, true},
    // A wider gap after it, as its values align on the right and Quantise's on the left.
    TrackColumn{TrackField::TimeOffset, "Time offset", 16, true, 2},
    // Last, as its settings can run long.
    TrackColumn{TrackField::QuantizeSummary, "Quantise", 25},
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
    case TrackField::Arm:
        return track.arm ? "R" : "-";
    case TrackField::Thru:
        return track.thru ? "T" : "-";
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
    case TrackField::Quantize:
        return track.quantize.on ? "Q" : "-";
    case TrackField::QuantizeSummary:
        return quantizeSummary(track);
    case TrackField::QuantizeGrid:
        return track.quantize.grid ? core::toString(*track.quantize.grid) : "grid";
    case TrackField::QuantizeStrength:
        return std::to_string(track.quantize.strength) + "%";
    case TrackField::QuantizeSwing:
        return std::to_string(track.quantize.swing) + "%";
    case TrackField::QuantizeMove:
        return std::string{core::quantizeMoveName(track.quantize.move)};
    }
    return {};
}

std::string_view toggleLetter(TrackField field)
{
    switch (field)
    {
    case TrackField::Mute:
        return "M";
    case TrackField::Solo:
        return "S";
    case TrackField::Arm:
        return "R";
    case TrackField::Thru:
        return "T";
    case TrackField::ChaseNotes:
        return "C";
    case TrackField::Quantize:
        return "Q";
    default:
        return {};
    }
}

std::string_view trackFieldDescription(TrackField field)
{
    switch (field)
    {
    case TrackField::Number:
        return "Track number: its place among the tracks";
    case TrackField::Name:
        return "Track name";
    case TrackField::Mute:
        return "Mute: silence this track in playback";
    case TrackField::Solo:
        return "Solo: play only the soloed tracks";
    case TrackField::Arm:
        return "Arm: record onto this track";
    case TrackField::Thru:
        return "Thru: send MIDI input on to this track while it's armed";
    case TrackField::ChaseNotes:
        return "Chase: start notes already held where playback starts";
    case TrackField::Quantize:
        return "Quantise notes live during playback";
    case TrackField::Port:
        return "Output port";
    case TrackField::Channel:
        return "MIDI channel";
    case TrackField::BankMsb:
        return "Bank select MSB, sent before the program";
    case TrackField::BankLsb:
        return "Bank select LSB, sent before the program";
    case TrackField::Program:
        return "Program, sent when playback starts";
    case TrackField::Transpose:
        return "Transpose in semitones, as it plays";
    case TrackField::VelocityOffset:
        return "Velocity offset, added as it plays";
    case TrackField::TimeOffset:
        return "Time offset, as it plays: negative sends early";
    case TrackField::QuantizeSummary:
        return "Playback quantise settings";
    case TrackField::QuantizeGrid:
    case TrackField::QuantizeStrength:
    case TrackField::QuantizeSwing:
    case TrackField::QuantizeMove:
        return "Playback quantise";
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
    if (field == TrackField::Quantize)
    {
        if (text != "on" && text != "off")
        {
            return "Use on or off";
        }
        track.quantize.on = text == "on";
        return {};
    }
    if (field == TrackField::Mute || field == TrackField::Solo || field == TrackField::Arm ||
        field == TrackField::Thru || field == TrackField::ChaseNotes)
    {
        if (text != "on" && text != "off")
        {
            return "Use on or off";
        }
        const bool on = text == "on";
        (field == TrackField::Mute   ? track.mute
         : field == TrackField::Solo ? track.solo
         : field == TrackField::Arm  ? track.arm
         : field == TrackField::Thru ? track.thru
                                     : track.chaseNotes) = on;
        return {};
    }
    auto& quantize = track.quantize;
    if (field == TrackField::QuantizeSummary)
    {
        return "Enter edits the settings in the inspector";
    }
    if (field == TrackField::QuantizeGrid)
    {
        if (text.empty() || text == "grid")
        {
            quantize.grid.reset();
            return {};
        }
        const auto fraction = core::parseNoteFraction(text);
        if (!fraction)
        {
            return "Use a note length, such as 1/16 or 1/8t, or grid for the track's grid";
        }
        quantize.grid = *fraction;
        return {};
    }
    if (field == TrackField::QuantizeMove)
    {
        const auto move = core::quantizeMove(text);
        if (!move)
        {
            return "Use starts, ends, both or lengths";
        }
        quantize.move = *move;
        return {};
    }
    if (field == TrackField::QuantizeStrength || field == TrackField::QuantizeSwing)
    {
        if (text.ends_with('%'))
        {
            text.remove_suffix(1);
        }
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        const bool strength = field == TrackField::QuantizeStrength;
        const int low = strength ? 0 : core::PlaybackQuantize::kMinSwing;
        const int high = strength ? 100 : core::PlaybackQuantize::kMaxSwing;
        if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
            value < low || value > high)
        {
            return strength ? "Strength is 0% to 100%" : "Swing is 25% to 75%, 50% straight";
        }
        (strength ? quantize.strength : quantize.swing) = value;
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
    auto& quantize = track.quantize;
    if (field == TrackField::QuantizeGrid)
    {
        // Coarser or finer through the straight and triplet lengths, from wherever it is.
        constexpr std::array<core::NoteFraction, 12> kSteps{{{1, 1},
                                                             {1, 2},
                                                             {1, 3},
                                                             {1, 4},
                                                             {1, 6},
                                                             {1, 8},
                                                             {1, 12},
                                                             {1, 16},
                                                             {1, 24},
                                                             {1, 32},
                                                             {1, 48},
                                                             {1, 64}}};
        const auto now = quantize.grid.value_or(track.grid);
        // The first at or shorter than it, or with steps up the one longer just before it.
        const auto shorter =
            std::ranges::find_if(kSteps, [&](core::NoteFraction f) { return !(now < f); });
        auto at = shorter - kSteps.begin();
        if (steps > 0 && (shorter == kSteps.end() || *shorter != now))
        {
            --at; // to the next longer is already a step
            --steps;
        }
        at = std::clamp<std::ptrdiff_t>(at - steps, 0,
                                        static_cast<std::ptrdiff_t>(kSteps.size()) - 1);
        quantize.grid = kSteps[static_cast<std::size_t>(at)];
        return {};
    }
    if (field == TrackField::QuantizeStrength)
    {
        quantize.strength = std::clamp(quantize.strength + 5 * steps, 0, 100);
        return {};
    }
    if (field == TrackField::QuantizeSwing)
    {
        quantize.swing = std::clamp(quantize.swing + steps, core::PlaybackQuantize::kMinSwing,
                                    core::PlaybackQuantize::kMaxSwing);
        return {};
    }
    if (field == TrackField::QuantizeMove)
    {
        const int at = (static_cast<int>(quantize.move) + steps % 4 + 4) % 4;
        quantize.move = static_cast<core::QuantizeMove>(at);
        return {};
    }
    if (field == TrackField::Mute || field == TrackField::Solo || field == TrackField::Arm ||
        field == TrackField::Thru || field == TrackField::ChaseNotes ||
        field == TrackField::Quantize)
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
        field == TrackField::VelocityOffset)
    {
        const int value = field == TrackField::Channel     ? track.channel
                          : field == TrackField::Transpose ? track.transpose
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

std::string quantizeSummary(const core::Track& track)
{
    const auto& quantize = track.quantize;
    auto text = quantize.grid ? core::toString(*quantize.grid) : core::toString(track.grid);
    if (quantize.strength != 100)
    {
        text += " " + std::to_string(quantize.strength) + "%";
    }
    if (quantize.swing != 50)
    {
        text += " swing " + std::to_string(quantize.swing);
    }
    if (quantize.move != core::QuantizeMove::Starts)
    {
        text += " " + std::string{core::quantizeMoveName(quantize.move)};
    }
    return text;
}

ArmedMessage armedMessage(const core::Song& song, std::span<const core::TrackId> changed,
                          bool armed)
{
    const auto tracks = song.tracks();
    const auto names = [&](auto&& wanted)
    {
        std::string text;
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& track = *tracks[i];
            if (wanted(track))
            {
                text += (text.empty() ? "" : ", ") +
                        (track.name.empty() ? "track " + std::to_string(i + 1) : track.name);
            }
        }
        return text;
    };
    const auto isChanged = [&](const core::Track& track)
    { return std::ranges::find(changed, track.id()) != changed.end(); };
    ArmedMessage result{(armed ? "Armed: " : "Disarmed: ") + names(isChanged), false};
    if (!armed)
    {
        return result;
    }
    // A track left armed is easy to miss, and records the take too.
    const auto others =
        names([&](const core::Track& track) { return track.arm && !isChanged(track); });
    if (!others.empty())
    {
        result.text += "; also armed: " + others;
        result.warning = true;
    }
    return result;
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
