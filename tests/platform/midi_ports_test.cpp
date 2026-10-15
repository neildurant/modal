#include <modal/platform/midi_ports.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace
{

bool envFlagSet(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view{value} == "1";
}

bool hasPort(const auto& ports, std::string_view fragment)
{
    return std::ranges::any_of(ports, [&](const auto& port)
                               { return port.name.find(fragment) != std::string::npos; });
}

} // namespace

// Port lists depend on the machine. Without help from the environment, this only checks
// that listing doesn't fail. CI on Linux loads snd-seq-dummy, which provides "Midi Through"
// ports, and sets MODAL_TEST_ALSA_SEQ=1 so the ports themselves are checked.
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

    if (envFlagSet("MODAL_TEST_ALSA_SEQ"))
    {
        REQUIRE(ports.api);
        CHECK(hasPort(ports.inputs, "Midi Through"));
        CHECK(hasPort(ports.outputs, "Midi Through"));
    }
}
