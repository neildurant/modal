// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/playback_transform.hpp>

#include <algorithm>

namespace modal::core
{

std::optional<std::uint8_t> playbackPitch(std::uint8_t pitch, int transpose)
{
    const int result = pitch + transpose;
    if (result < 0 || result > 127)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(result);
}

std::uint8_t playbackVelocity(std::uint8_t velocity, int offset)
{
    return static_cast<std::uint8_t>(std::clamp(velocity + offset, 1, 127));
}

std::variant<QuantizedTrack, Refusal> playbackQuantized(const Song& song, const Track& track)
{
    const auto& quantize = track.quantize;
    const auto& meter = song.meter(track.id());
    const auto step = std::max<Tick>(quantize.grid.value_or(track.grid).toTicks(song.ppqn()), 1);
    const auto end = song.end();
    const auto nearest = [&](Tick tick)
    {
        const auto to = nearestGridLine(meter, tick, step, quantize.swing);
        return to < end ? to : tick;
    };
    return quantizeAll(song.events(track.id()), nearest, true,
                       {.strength = quantize.strength,
                        .move = quantize.move,
                        .step = [step](Tick) { return step; },
                        .nearestEnd = [&](Tick tick)
                        { return nearestGridLine(meter, tick, step, quantize.swing); }});
}

} // namespace modal::core
