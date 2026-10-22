// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/env.hpp"

#include <modal/platform/midi_ports.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

using modal::platform::matchPort;
using modal::platform::MidiPort;

namespace
{

bool hasPort(const auto& ports, std::string_view fragment)
{
    return std::ranges::any_of(ports, [&](const auto& port)
                               { return port.name.find(fragment) != std::string::npos; });
}

const std::vector<MidiPort> kPorts{
    {"Babyface Pro (70794321) Port 1"},
    {"Babyface Pro (70794321) Port 2"},
    {"loopMIDI Port"},
    {"loopMIDI Port 2"},
    {"Microsoft GS Wavetable Synth"},
};

} // namespace

// Port lists depend on the machine. Without help from the environment, this only checks
// that listing doesn't fail. On a Linux machine with snd-seq-dummy loaded (it provides
// "Midi Through" ports), set MODAL_TEST_ALSA_SEQ=1 to check the ports themselves. CI
// runners have no ALSA sequencer, so this runs only on a real Linux install.
TEST_CASE("listing MIDI ports")
{
    const auto ports = modal::platform::listMidiPorts();

    if (ports.api)
    {
        CHECK_FALSE(ports.api->empty());
    }
    else
    {
        CHECK(ports.inputs.empty());
        CHECK(ports.outputs.empty());
    }

    if (modal::test::envFlagSet("MODAL_TEST_ALSA_SEQ"))
    {
        REQUIRE(ports.api);
        CHECK(hasPort(ports.inputs, "Midi Through"));
        CHECK(hasPort(ports.outputs, "Midi Through"));
    }
}

TEST_CASE("a port name picks out the port with that name")
{
    CHECK(matchPort(kPorts, "loopMIDI Port").index == 2);
    CHECK(matchPort(kPorts, "loopMIDI Port 2").index == 3);
}

TEST_CASE("otherwise, the one port with that name ignoring case")
{
    CHECK(matchPort(kPorts, "loopmidi port").index == 2);
}

TEST_CASE("otherwise, the one port whose name contains it ignoring case")
{
    CHECK(matchPort(kPorts, "wavetable").index == 4);
    CHECK(matchPort(kPorts, ") port 2").index == 1);
}

TEST_CASE("a port name matching several ports equally well picks out none")
{
    const auto match = matchPort(kPorts, "babyface");
    CHECK_FALSE(match.index);
    CHECK(match.candidates == std::vector<std::size_t>{0, 1});

    const std::vector<MidiPort> twins{{"USB MIDI"}, {"Synth"}, {"USB MIDI"}};
    CHECK(matchPort(twins, "USB MIDI").candidates == std::vector<std::size_t>{0, 2});
}

TEST_CASE("a port name matching no port, or no name, picks out none")
{
    for (const auto* name : {"Fantom", ""})
    {
        const auto match = matchPort(kPorts, name);
        CHECK_FALSE(match.index);
        CHECK(match.candidates.empty());
    }
    CHECK_FALSE(matchPort({}, "loopMIDI").index);
}
