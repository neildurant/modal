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

    for (std::size_t i = 0; i < ports_.size(); ++i)
    {
        if (ports_[i]->name == result.portName)
        {
            result.destination = core::DestinationId{static_cast<std::uint32_t>(i)};
            return result;
        }
    }

    auto apiConfig = libremidi::midi_out_configuration_for(*api);
    detail::nameClient(apiConfig);
    libremidi::midi_out out{libremidi::output_configuration{}, std::move(apiConfig)};
    if (const auto error = out.open_port(*found.port, "Modal output"); error != stdx::error{})
    {
        result.error = "can't open \"" + result.portName + "\": " + detail::errorText(error);
        return result;
    }
    result.destination = core::DestinationId{static_cast<std::uint32_t>(ports_.size())};
    ports_.push_back(std::make_unique<Port>(Port{result.portName, std::move(out)}));
    return result;
}

void MidiOutputs::send(core::DestinationId destination, std::span<const std::uint8_t> bytes,
                       std::chrono::nanoseconds) noexcept
{
    bool sent = false;
    if (destination.value < ports_.size())
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
