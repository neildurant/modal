// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>

#include <array>
#include <charconv>
#include <system_error>

namespace modal::core
{

namespace
{

constexpr std::array<std::string_view, 12> kNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                  "F#", "G",  "G#", "A",  "A#", "B"};

// Semitones above C for each letter from A.
constexpr std::array<int, 7> kLetters{9, 11, 0, 2, 4, 5, 7};

} // namespace

std::string pitchName(std::uint8_t pitch)
{
    return std::string{kNames[pitch % 12]} + std::to_string(pitch / 12 - 1);
}

std::optional<std::uint8_t> parsePitch(std::string_view text)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    const char letter = static_cast<char>(text[0] | 0x20); // lower case
    if (letter < 'a' || letter > 'g')
    {
        return std::nullopt;
    }
    int semitone = kLetters[static_cast<std::size_t>(letter - 'a')];
    text.remove_prefix(1);
    if (!text.empty() && (text[0] == '#' || text[0] == 'b'))
    {
        semitone += text[0] == '#' ? 1 : -1;
        text.remove_prefix(1);
    }

    int octave = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, octave);
    if (text.empty() || error != std::errc{} || ptr != end || octave < -1 || octave > 9)
    {
        return std::nullopt;
    }
    const int pitch = (octave + 1) * 12 + semitone;
    if (pitch < 0 || pitch > 127)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(pitch);
}

} // namespace modal::core
