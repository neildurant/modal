#pragma once

#include <optional>
#include <string>
#include <vector>

namespace modal::platform
{

struct MidiPort
{
    std::string name;
};

struct MidiPortList
{
    // Display name of the MIDI API in use, e.g. "ALSA (sequencer)", or empty if the
    // platform's MIDI API can't be used (on Linux, when /dev/snd/seq is missing or
    // not accessible).
    std::optional<std::string> api;
    std::vector<MidiPort> inputs;
    std::vector<MidiPort> outputs;
};

// Lists the MIDI ports visible through the platform's default MIDI 1.0 API,
// including virtual ports created by other applications.
MidiPortList listMidiPorts();

} // namespace modal::platform
