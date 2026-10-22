// SPDX-License-Identifier: GPL-3.0-or-later

#include "port_lookup.hpp"

#include <modal/platform/midi_output.hpp>

#include <libremidi/libremidi.hpp>

#include <string>

namespace modal::platform
{

struct MidiOutputs::Port
{
    std::string name;
    libremidi::midi_out out;
    bool own = false; // created by Modal, rather than one of the system's
};

MidiOutputs::MidiOutputs() = default;
MidiOutputs::~MidiOutputs() = default;

OutputOpenResult MidiOutputs::open(std::string_view name)
{
    OutputOpenResult result;
    const auto api = detail::usableApi();
    if (!api)
    {
        result.error = "MIDI isn't available on this system";
        return result;
    }
    const auto observer = detail::makeObserver(*api);
    auto found = detail::findPort(observer.get_output_ports(), name, "output");
    static_cast<PortOpenResult&>(result) = found.result;
    if (!found.port)
    {
        return result;
    }

    const auto opened = opened_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < opened; ++i)
    {
        if (!ports_[i]->own && ports_[i]->name == result.portName)
        {
            result.destination = core::DestinationId{static_cast<std::uint32_t>(i)};
            return result;
        }
    }

    if (opened == ports_.size())
    {
        result.error = "can't open \"" + result.portName + "\": no more than " +
                       std::to_string(ports_.size()) + " output ports can be open";
        return result;
    }
    auto apiConfig = libremidi::midi_out_configuration_for(*api);
    detail::nameClient(apiConfig);
    libremidi::midi_out out{libremidi::output_configuration{}, std::move(apiConfig)};
    if (const auto error = out.open_port(*found.port, "Modal output"); error != stdx::error{})
    {
        result.error = "can't open \"" + result.portName + "\": " + detail::errorText(error);
        return result;
    }
    result.destination = core::DestinationId{static_cast<std::uint32_t>(opened)};
    ports_[opened] = std::make_unique<Port>(Port{result.portName, std::move(out), false});
    opened_.store(opened + 1, std::memory_order_release);
    return result;
}

OutputOpenResult MidiOutputs::openOwn(std::string_view name)
{
    OutputOpenResult result;
    result.portName = name;
    const auto opened = opened_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < opened; ++i)
    {
        if (ports_[i]->own && ports_[i]->name == result.portName)
        {
            result.destination = core::DestinationId{static_cast<std::uint32_t>(i)};
            return result;
        }
    }

    const auto api = detail::usableApi();
    if (!api)
    {
        result.error = "MIDI isn't available on this system";
        return result;
    }
    if (*api == libremidi::API::WINDOWS_MM)
    {
        result.error = "Windows can't give Modal ports of its own; a virtual cable such as "
                       "loopMIDI does the job";
        return result;
    }
    if (opened == ports_.size())
    {
        result.error = "can't create \"" + result.portName + "\": no more than " +
                       std::to_string(ports_.size()) + " output ports can be open";
        return result;
    }
    auto apiConfig = libremidi::midi_out_configuration_for(*api);
    detail::nameClient(apiConfig);
    libremidi::midi_out out{libremidi::output_configuration{}, std::move(apiConfig)};
    if (const auto error = out.open_virtual_port(name); error != stdx::error{})
    {
        result.error = "can't create \"" + result.portName + "\": " + detail::errorText(error);
        return result;
    }
    result.destination = core::DestinationId{static_cast<std::uint32_t>(opened)};
    ports_[opened] = std::make_unique<Port>(Port{result.portName, std::move(out), true});
    opened_.store(opened + 1, std::memory_order_release);
    return result;
}

void MidiOutputs::send(core::DestinationId destination, std::span<const std::uint8_t> bytes,
                       std::chrono::nanoseconds) noexcept
{
    bool sent = false;
    if (destination.value < opened_.load(std::memory_order_acquire))
    {
        try
        {
            sent = ports_[destination.value]->out.send_message(bytes) == stdx::error{};
        }
        catch (...)
        {
        }
    }
    if (!sent)
    {
        failedSends_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace modal::platform
