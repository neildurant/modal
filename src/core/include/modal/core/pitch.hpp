#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modal::core
{

// MIDI note numbers by name, with middle C (60) as C4, so 0 is C-1 and 127 is G9.

// The name of a note number from 0 to 127, with sharps: 61 is "C#4".
std::string pitchName(std::uint8_t pitch);

// A note name read back: a letter A to G in either case, optionally # or b, then the octave
// from -1 to 9. Returns nothing for anything else or outside 0-127 (such as "Cb-1").
std::optional<std::uint8_t> parsePitch(std::string_view text);

} // namespace modal::core
