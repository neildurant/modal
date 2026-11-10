// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/midi_input_session.hpp>

#include <algorithm>
#include <chrono>
#include <span>
#include <utility>

namespace modal::platform
{

namespace
{

struct SystemInput final : MidiInputSession::Port
{
    explicit SystemInput(MidiInput::Callback callback)
        : input(std::move(callback))
    {
    }
    MidiInput input;
};

} // namespace

MidiInputSession::MidiInputSession(core::Engine& engine, Backend backend)
    : engine_(engine)
    , backend_(std::move(backend))
{
    if (!backend_.ports)
        backend_.ports = [] { return listMidiPorts().inputs; };
    if (!backend_.open)
        backend_.open = [](std::string_view name, MidiInput::Callback callback) -> Opened
        {
            auto port = std::make_unique<SystemInput>(std::move(callback));
            auto result = port->input.open(name);
            if (!result.ok())
                return {nullptr, std::move(result.error)};
            return {std::move(port), {}};
        };
}

std::string MidiInputSession::setInputs(std::vector<std::string> wanted, std::int64_t now)
{
    if (opened_ && wanted == wanted_)
        return {};
    wanted_ = std::move(wanted);
    if (due_)
        return {}; // complete the reset, then open the latest requested set
    if (!connections_.empty())
    {
        connections_.clear(); // joins old callbacks before the reset can be queued
        due_ = now;
        return poll(now);
    }
    return openRequested();
}

std::string MidiInputSession::poll(std::int64_t now)
{
    if (!due_ || now < *due_)
        return {};
    if (!reset_)
        reset_ = engine_.resetInputs();
    if (!reset_ || !engine_.inputsReset(*reset_))
    {
        due_ = now + kResetPoll;
        return {};
    }
    reset_.reset();
    due_.reset();
    return openRequested();
}

std::vector<MidiPort> MidiInputSession::ports() const
{
    std::vector<MidiPort> result;
    for (const auto& connection : connections_)
        result.push_back({connection.name});
    return result;
}

std::optional<core::InputId> MidiInputSession::inputId(std::string_view name) const
{
    const auto match = matchPort(ports(), name);
    return match.index ? std::optional{core::InputId{static_cast<std::uint32_t>(*match.index)}}
                       : std::nullopt;
}

std::string MidiInputSession::openRequested()
{
    opened_ = true;
    const auto available = backend_.ports();
    std::vector<std::string> names;
    std::vector<std::string> problems;
    const auto add = [&](const std::string& name)
    {
        if (std::ranges::find(names, name) == names.end())
            names.push_back(name);
    };
    for (const auto& name : wanted_)
    {
        const auto match = matchPort(available, name);
        if (match.index)
            add(available[*match.index].name);
        else
            problems.push_back(name +
                               (match.candidates.empty() ? " not found" : " matches several"));
    }
    for (const auto& name : names)
    {
        if (connections_.size() == core::Engine::kMaxInputs)
        {
            problems.push_back("only " + std::to_string(core::Engine::kMaxInputs) +
                               " inputs can be open, so not " + name);
            continue;
        }
        const core::InputId id{static_cast<std::uint32_t>(connections_.size())};
        auto opened = backend_.open(
            name,
            [this, id](std::span<const std::uint8_t> message, std::chrono::nanoseconds time)
            {
                engine_.receive(id, message, time);
                if (wake_)
                    wake_();
            });
        if (!opened.port)
        {
            problems.push_back(opened.error.empty() ? "can't open " + name
                                                    : std::move(opened.error));
            continue;
        }
        connections_.push_back({name, std::move(opened.port)});
    }
    std::string text;
    for (const auto& problem : problems)
        text += (text.empty() ? "" : "; ") + problem;
    return text.empty() ? text : "MIDI inputs: " + text;
}

} // namespace modal::platform
