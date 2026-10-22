// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/controllers.hpp>

#include <array>
#include <string_view>

namespace modal::core
{

namespace
{

// The MIDI 1.0 standard's names, as most synths' manuals give them.
constexpr std::array<std::string_view, 128> kNames = []
{
    std::array<std::string_view, 128> names{};
    names[0] = "Bank Select";
    names[1] = "Modulation";
    names[2] = "Breath";
    names[4] = "Foot";
    names[5] = "Portamento Time";
    names[6] = "Data Entry";
    names[7] = "Volume";
    names[8] = "Balance";
    names[10] = "Pan";
    names[11] = "Expression";
    names[12] = "Effect 1";
    names[13] = "Effect 2";
    names[64] = "Sustain";
    names[65] = "Portamento";
    names[66] = "Sostenuto";
    names[67] = "Soft";
    names[68] = "Legato";
    names[69] = "Hold 2";
    names[70] = "Sound Variation";
    names[71] = "Resonance";
    names[72] = "Release Time";
    names[73] = "Attack Time";
    names[74] = "Brightness";
    names[75] = "Decay Time";
    names[76] = "Vibrato Rate";
    names[77] = "Vibrato Depth";
    names[78] = "Vibrato Delay";
    names[84] = "Portamento Control";
    names[91] = "Reverb";
    names[92] = "Tremolo";
    names[93] = "Chorus";
    names[94] = "Detune";
    names[95] = "Phaser";
    names[96] = "Data Increment";
    names[97] = "Data Decrement";
    names[98] = "NRPN LSB";
    names[99] = "NRPN MSB";
    names[100] = "RPN LSB";
    names[101] = "RPN MSB";
    names[120] = "All Sound Off";
    names[121] = "Reset All Controllers";
    names[122] = "Local Control";
    names[123] = "All Notes Off";
    names[124] = "Omni Off";
    names[125] = "Omni On";
    names[126] = "Mono On";
    names[127] = "Poly On";
    return names;
}();

} // namespace

std::string controllerName(std::uint8_t controller)
{
    if (controller >= 128)
    {
        return {};
    }
    if (controller >= 32 && controller < 64)
    {
        const auto msb = kNames[controller - 32u];
        return msb.empty() ? std::string{} : std::string{msb} + " LSB";
    }
    return std::string{kNames[controller]};
}

} // namespace modal::core
