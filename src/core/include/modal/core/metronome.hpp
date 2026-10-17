#pragma once

#include <modal/core/note_fraction.hpp>

#include <cstdint>
#include <string>

namespace modal::core
{

// The metronome: MIDI notes on each beat of the reference meter, the first beat of each bar
// accented. It is never offset, muted or soloed. When recording it carries on past the song's
// end until stopped; otherwise it stops when the song ends.
struct MetronomeSettings
{
    enum class When : std::uint8_t
    {
        Off,
        Recording,
        Always,
    };

    When when = When::Recording;
    std::string port; // output port name; until settings exist, chosen when recording
    int channel = 10;
    std::uint8_t downbeatNote = 76; // Hi Wood Block
    std::uint8_t downbeatVelocity = 110;
    std::uint8_t beatNote = 77; // Low Wood Block
    std::uint8_t beatVelocity = 80;
    NoteFraction length{1, 32}; // cut short where the next beat comes sooner

    // Whether the channel, notes and velocities are in range (velocities 1-127).
    bool isValid() const
    {
        return channel >= 1 && channel <= 16 && downbeatNote < 128 && beatNote < 128 &&
               downbeatVelocity >= 1 && downbeatVelocity < 128 && beatVelocity >= 1 &&
               beatVelocity < 128;
    }

    friend bool operator==(const MetronomeSettings&, const MetronomeSettings&) = default;
};

} // namespace modal::core
