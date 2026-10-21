#pragma once

#include <cstdint>
#include <string>

namespace modal::core
{

// A MIDI controller's standard name, such as "Volume" for 7 or "Brightness" for 74; the
// controllers from 32 to 63 are the low halves of 0 to 31, "Volume LSB". Empty for a
// controller with no standard use.
std::string controllerName(std::uint8_t controller);

} // namespace modal::core
