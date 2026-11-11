// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/note_fraction.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/event_text.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

namespace modal::ui
{

namespace
{

std::string padded(core::Tick tick, int digits)
{
    auto text = std::to_string(tick);
    return text.size() < static_cast<std::size_t>(digits)
               ? std::string(static_cast<std::size_t>(digits) - text.size(), '0') + text
               : text;
}

// The common note values a length is shown with when it's close to one: the straight
// values from a whole note to 1/16, dotted and triplet values with denominators up to 16,
// and whole notes beyond.
std::vector<core::NoteFraction> commonLengths(core::Tick length, core::Ppqn ppqn)
{
    std::vector<core::NoteFraction> lengths;
    for (std::int64_t d = 1; d <= 16; d *= 2)
    {
        lengths.push_back({1, d});
    }
    for (std::int64_t d = 2; d <= 8; d *= 2)
    {
        lengths.push_back(core::NoteFraction{1, d}.dotted());
        lengths.push_back(core::NoteFraction{1, d}.triplet());
    }
    for (std::int64_t wholes = 2; wholes <= length / ppqn.whole() + 1; ++wholes)
    {
        lengths.push_back({wholes, 1});
    }
    return lengths;
}

} // namespace

int tickDigits(core::Ppqn ppqn)
{
    return static_cast<int>(std::to_string(ppqn.quarter() - 1).size());
}

std::string positionText(const core::MeterMap& meter, core::Tick tick)
{
    const auto position = meter.toBarBeatTick(tick);
    return std::to_string(position.bar) + ":" + std::to_string(position.beat) + ":" +
           padded(position.tick, tickDigits(meter.ppqn()));
}

std::string lengthText(core::Tick length, core::Ppqn ppqn, core::TimeSignature signature)
{
    const core::Tick bar = signature.barLength().toTicks(ppqn);
    const core::Tick beat = signature.beatLength().toTicks(ppqn);
    auto text = std::to_string(length / bar) + ":" + std::to_string(length % bar / beat) + ":" +
                padded(length % beat, tickDigits(ppqn));

    // The nearest common value within 5% of its own length, if any.
    std::optional<core::NoteFraction> nearest;
    core::Tick off = 0;
    for (const auto& candidate : commonLengths(length, ppqn))
    {
        const core::Tick ticks = candidate.toTicks(ppqn);
        const core::Tick distance = std::abs(length - ticks);
        if (20 * distance <= ticks && (!nearest || distance < off))
        {
            nearest = candidate;
            off = distance;
        }
    }
    if (nearest)
    {
        text += (off == 0 ? " (" : " (≈") + toString(*nearest) + ")";
    }
    return text;
}

std::string inputText(std::span<const std::uint8_t> message)
{
    constexpr std::size_t kWidth = 13;
    if (message.size() < 2 || message[0] < 0x80 || message[0] >= 0xF0)
    {
        return std::string(kWidth, ' ');
    }
    const auto data = [&](std::size_t i) { return i < message.size() ? int{message[i]} : 0; };
    std::string name;
    std::string value;
    switch (message[0] & 0xF0)
    {
    case 0x80:
    case 0x90:
        name = core::pitchName(message[1]);
        value = std::to_string(data(2));
        break;
    case 0xA0:
        name = "PPrs";
        value = std::to_string(data(2));
        break;
    case 0xB0:
        name = "CC" + std::to_string(data(1));
        value = std::to_string(data(2));
        break;
    case 0xC0:
        name = "Prog";
        value = std::to_string(data(1));
        break;
    case 0xD0:
        name = "Pres";
        value = std::to_string(data(1));
        break;
    default: // 0xE0
    {
        const auto bend = ((data(2) << 7) | data(1)) - 8192;
        name = "Bend";
        value = (bend > 0 ? "+" : "") + std::to_string(bend);
        break;
    }
    }
    auto channel = std::to_string((message[0] & 0x0F) + 1);
    channel.insert(0, 2 - channel.size(), ' ');
    name.resize(std::max<std::size_t>(name.size(), 5), ' ');
    value.insert(0, value.size() < 5 ? 5 - value.size() : 0, ' ');
    return channel + " " + name + value;
}

} // namespace modal::ui
