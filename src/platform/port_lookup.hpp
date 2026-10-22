// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Finding ports through libremidi, shared by the input and output classes.

#include <modal/platform/midi_ports.hpp>

#include <libremidi/libremidi.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::platform::detail
{

inline std::string portName(const libremidi::port_information& port)
{
    return port.display_name.empty() ? port.port_name : port.display_name;
}

// The platform's MIDI 1.0 API, if it can be used here.
std::optional<libremidi::API> usableApi();

// The observer for that API, seeing every port, including other applications' virtual ones.
libremidi::observer makeObserver(libremidi::API api);

template <class Port>
struct FoundPort
{
    std::optional<Port> port;
    PortOpenResult result;
};

std::string describeMismatch(std::string_view kind, std::string_view name,
                             std::span<const MidiPort> ports, const PortMatch& match);

// The port a name refers to, out of the ports of one kind ("input" or "output").
template <class Port>
FoundPort<Port> findPort(const std::vector<Port>& ports, std::string_view name,
                         std::string_view kind)
{
    std::vector<MidiPort> names;
    for (const auto& port : ports)
    {
        names.push_back({portName(port)});
    }
    const auto match = matchPort(names, name);
    if (!match.index)
    {
        return {std::nullopt, {{}, describeMismatch(kind, name, names, match)}};
    }
    return {ports[*match.index], {names[*match.index].name, {}}};
}

std::string errorText(const stdx::error& error);

// Names Modal's ALSA sequencer client, where the API has one.
template <class Configuration>
void nameClient(Configuration& configuration)
{
    std::visit(
        [](auto& api)
        {
            if constexpr (requires { api.client_name; })
            {
                api.client_name = "Modal";
            }
        },
        configuration);
}

} // namespace modal::platform::detail
