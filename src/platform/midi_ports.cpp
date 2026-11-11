// SPDX-License-Identifier: GPL-3.0-or-later

#include "port_lookup.hpp"

#include <modal/platform/midi_ports.hpp>

#include <libremidi/libremidi.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>

namespace modal::platform
{

namespace
{

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, {}, lower, lower);
}

bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return !std::ranges::search(text, part, {}, lower, lower).empty();
}

} // namespace

namespace detail
{

std::optional<libremidi::API> usableApi()
{
    // available_apis() lists only the APIs usable at run time.
    const auto api = libremidi::midi1::default_api();
    const auto apis = libremidi::available_apis();
    if (std::ranges::find(apis, api) == apis.end())
    {
        return std::nullopt;
    }
    return api;
}

libremidi::observer makeObserver(libremidi::API api)
{
    libremidi::observer_configuration config;
    config.track_any = true;
    auto apiConfig = libremidi::observer_configuration_for(api);
    nameClient(apiConfig);
    return libremidi::observer{config, std::move(apiConfig)};
}

std::string describeMismatch(std::string_view kind, std::string_view name,
                             std::span<const MidiPort> ports, const PortMatch& match)
{
    if (ports.empty())
    {
        return "there are no MIDI " + std::string{kind} + " ports";
    }
    std::string text;
    std::vector<std::size_t> listed = match.candidates;
    if (listed.empty())
    {
        text = "no MIDI " + std::string{kind} + " port matches \"" + std::string{name} +
               "\"; the ports are: ";
        for (std::size_t i = 0; i < ports.size(); ++i)
        {
            listed.push_back(i);
        }
    }
    else
    {
        text =
            "\"" + std::string{name} + "\" matches several MIDI " + std::string{kind} + " ports: ";
    }
    for (std::size_t i = 0; i < listed.size(); ++i)
    {
        text += (i == 0 ? "" : ", ") + ports[listed[i]].name;
    }
    return text;
}

std::string errorText(const stdx::error& error)
{
    const auto message = error.message();
    return {message.data(), message.size()};
}

} // namespace detail

struct PortWatcher::Impl
{
    std::optional<libremidi::observer> observer;
};

PortWatcher::PortWatcher(std::function<void()> changed)
    : impl_(std::make_unique<Impl>())
{
    const auto api = detail::usableApi();
    if (!api)
    {
        return;
    }
    libremidi::observer_configuration config;
    config.track_any = true;
    config.notify_in_constructor = false;
    config.input_added = [changed](const libremidi::input_port&) { changed(); };
    config.input_removed = [changed](const libremidi::input_port&) { changed(); };
    config.output_added = [changed](const libremidi::output_port&) { changed(); };
    config.output_removed = [changed](const libremidi::output_port&) { changed(); };
    auto apiConfig = libremidi::observer_configuration_for(*api);
    detail::nameClient(apiConfig);
    impl_->observer.emplace(std::move(config), std::move(apiConfig));
}

PortWatcher::~PortWatcher() = default;

bool canCreateOwnPorts()
{
    const auto api = detail::usableApi();
    return api && *api != libremidi::API::WINDOWS_MM;
}

MidiPortList listMidiPorts()
{
    MidiPortList list;
    const auto api = detail::usableApi();
    if (!api)
    {
        return list;
    }
    const auto observer = detail::makeObserver(*api);

    list.api = std::string{libremidi::get_api_display_name(*api)};
    for (auto& label : detail::portLabels(detail::othersPorts(observer.get_input_ports())))
    {
        list.inputs.push_back({std::move(label)});
    }
    for (auto& label : detail::portLabels(detail::othersPorts(observer.get_output_ports())))
    {
        list.outputs.push_back({std::move(label)});
    }
    return list;
}

std::vector<std::string> portLabels(std::span<const NamedPort> ports)
{
    std::vector<std::string> labels;
    for (const auto& port : ports)
    {
        const bool shared = std::ranges::count(ports, port.name, &NamedPort::name) > 1;
        labels.push_back(shared && !port.device.empty() && port.device != port.name
                             ? port.name + " (" + port.device + ")"
                             : port.name);
    }
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        const auto before =
            std::count(labels.begin(), labels.begin() + static_cast<std::ptrdiff_t>(i), labels[i]);
        if (before > 0)
        {
            labels[i] += " " + std::to_string(before + 1);
        }
    }
    return labels;
}

PortMatch matchPort(std::span<const MidiPort> ports, std::string_view name)
{
    const auto matching = [&](auto matches)
    {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < ports.size(); ++i)
        {
            if (matches(ports[i].name))
            {
                result.push_back(i);
            }
        }
        return result;
    };

    if (!name.empty())
    {
        for (const auto& candidates :
             {
                 matching([&](std::string_view port) { return port == name; }),
                 matching([&](std::string_view port) { return equalIgnoringCase(port, name); }),
                 matching([&](std::string_view port) { return containsIgnoringCase(port, name); }),
             })
        {
            if (candidates.size() == 1)
            {
                return {candidates.front(), {}};
            }
            if (candidates.size() > 1)
            {
                return {std::nullopt, candidates};
            }
        }
    }
    return {};
}

} // namespace modal::platform
