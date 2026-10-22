// SPDX-License-Identifier: GPL-3.0-or-later

#include "port_lookup.hpp"

#include <modal/platform/midi_input.hpp>

#include <libremidi/libremidi.hpp>

#include <optional>
#include <string>

namespace modal::platform
{

struct MidiInput::Impl
{
    Callback callback;
    std::optional<libremidi::midi_in> in;
};

MidiInput::MidiInput(Callback callback)
    : impl_(std::make_unique<Impl>(Impl{std::move(callback), std::nullopt}))
{
}

MidiInput::~MidiInput() = default;

PortOpenResult MidiInput::open(std::string_view name)
{
    impl_->in.reset();
    const auto api = detail::usableApi();
    if (!api)
    {
        return {{}, "MIDI isn't available on this system"};
    }
    const auto observer = detail::makeObserver(*api);
    auto found = detail::findPort(observer.get_input_ports(), name, "input");
    if (!found.port)
    {
        return found.result;
    }

    libremidi::input_configuration config;
    config.on_message = [impl = impl_.get()](libremidi::message&& message)
    {
        impl->callback({message.bytes.data(), message.bytes.size()},
                       std::chrono::nanoseconds{message.timestamp});
    };
    config.ignore_sysex = false;
    config.timestamps = libremidi::timestamp_mode::SystemMonotonic;
    auto apiConfig = libremidi::midi_in_configuration_for(*api);
    detail::nameClient(apiConfig);

    auto& in = impl_->in.emplace(config, std::move(apiConfig));
    if (const auto error = in.open_port(*found.port, "Modal input"); error != stdx::error{})
    {
        impl_->in.reset();
        found.result.error =
            "can't open \"" + found.result.portName + "\": " + detail::errorText(error);
    }
    return found.result;
}

} // namespace modal::platform
