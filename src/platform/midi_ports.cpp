#include <modal/platform/midi_ports.hpp>

#include <libremidi/libremidi.hpp>

#include <algorithm>

namespace modal::platform
{

namespace
{

std::string portName(const libremidi::port_information& port)
{
    return port.display_name.empty() ? port.port_name : port.display_name;
}

} // namespace

MidiPortList listMidiPorts()
{
    const auto api = libremidi::midi1::default_api();

    // available_apis() lists only the APIs usable at run time.
    MidiPortList list;
    const auto apis = libremidi::available_apis();
    if (std::ranges::find(apis, api) == apis.end())
    {
        return list;
    }

    libremidi::observer_configuration config;
    config.track_any = true;
    const libremidi::observer observer{config, libremidi::observer_configuration_for(api)};

    list.api = std::string{libremidi::get_api_display_name(api)};
    for (const auto& port : observer.get_input_ports())
    {
        list.inputs.push_back({portName(port)});
    }
    for (const auto& port : observer.get_output_ports())
    {
        list.outputs.push_back({portName(port)});
    }
    return list;
}

} // namespace modal::platform
