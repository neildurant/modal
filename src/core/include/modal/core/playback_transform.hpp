// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/transforms.hpp>

#include <cstdint>
#include <optional>
#include <variant>

namespace modal::core
{

// Playback and Freeze use the same pitch, velocity and quantise rules.
std::optional<std::uint8_t> playbackPitch(std::uint8_t pitch, int transpose);
std::uint8_t playbackVelocity(std::uint8_t velocity, int offset);
// The events as quantise plays them, with pedalling following notes; or its pedal-order
// refusal. The plan, the views and Freeze all use this result.
std::variant<QuantizedTrack, Refusal> playbackQuantized(const Song& song, const Track& track);

} // namespace modal::core
