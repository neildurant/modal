#pragma once

#include <cstdint>

namespace modal::core
{

// A position or length on the song timeline, in ticks.
using Tick = std::int64_t;

// Pulses (ticks) per quarter note. A song carries its own resolution; conversions take it
// from the song rather than assuming kPpqn.
struct Ppqn
{
    std::int32_t value;

    constexpr Tick quarter() const { return value; }
    constexpr Tick whole() const { return Tick{4} * value; }

    friend constexpr bool operator==(Ppqn, Ppqn) = default;
};

// The resolution of new songs, and the resolution files are converted to on import.
// This is the only place a tick resolution is written down.
inline constexpr Ppqn kPpqn{960};

} // namespace modal::core
