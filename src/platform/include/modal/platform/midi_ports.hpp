// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

// Which port a name given by the user refers to: the port with exactly that name, or else
// the one port whose name matches ignoring case, or else the one port whose name contains it
// ignoring case. When several match equally well, none is chosen and they are the candidates;
// when none match, there are no candidates.
struct PortMatch
{
    std::optional<std::size_t> index;
    std::vector<std::size_t> candidates;
};

PortMatch matchPort(std::span<const MidiPort> ports, std::string_view name);

// The outcome of opening a port by name.
struct PortOpenResult
{
    std::string portName; // the port's full name, if one was found
    std::string error;    // why it couldn't be opened, or empty if it was

    bool ok() const { return error.empty(); }
};

} // namespace modal::platform
