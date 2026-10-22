// SPDX-License-Identifier: GPL-3.0-or-later

// modal-ports: list the MIDI input and output ports Modal can see.

#include <modal/platform/midi_ports.hpp>

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

namespace
{

void printPorts(std::string_view heading, std::span<const modal::platform::MidiPort> ports)
{
    std::cout << heading << ":\n";
    if (ports.empty())
    {
        std::cout << "  (none)\n";
    }
    for (std::size_t i = 0; i < ports.size(); ++i)
    {
        std::cout << "  " << i << "  " << ports[i].name << '\n';
    }
}

} // namespace

int main()
{
    const auto ports = modal::platform::listMidiPorts();
    if (!ports.api)
    {
        std::cerr << "No MIDI API is available. On Linux, check that /dev/snd/seq exists\n"
                     "(the snd-seq kernel module is loaded) and that you can read and write it.\n";
        return 1;
    }
    std::cout << "MIDI API: " << *ports.api << "\n\n";
    printPorts("Inputs", ports.inputs);
    std::cout << '\n';
    printPorts("Outputs", ports.outputs);
    return 0;
}
