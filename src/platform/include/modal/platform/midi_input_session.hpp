// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/engine.hpp>
#include <modal/platform/midi_input.hpp>
#include <modal/platform/midi_ports.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::platform
{

// Owns the input ports feeding an engine. The engine must outlive this session; after closing
// the session at shutdown, quit the engine to end any held thru notes. All methods except the
// message callbacks run on the control thread. Time/deadlines are caller-supplied milliseconds.
class MidiInputSession
{
public:
    // Destroying a port must close it and finish its callbacks before returning.
    struct Port
    {
        virtual ~Port() = default;
    };
    struct Opened
    {
        std::unique_ptr<Port> port;
        std::string error;
    };
    // Defaults use MidiInput and listMidiPorts. Tests can supply fake ports and callbacks.
    struct Backend
    {
        std::function<std::vector<MidiPort>()> ports;
        std::function<Opened(std::string_view, MidiInput::Callback)> open;
    };

    explicit MidiInputSession(core::Engine& engine, Backend backend = {});
    ~MidiInputSession() = default;
    MidiInputSession(const MidiInputSession&) = delete;
    MidiInputSession& operator=(const MidiInputSession&) = delete;

    // Set before opening any ports. Called on the input thread, after queuing its message.
    void setWake(std::function<void()> wake) { wake_ = std::move(wake); }

    // Comma-separated names, matched as output ports are, or empty for all. Replacing inputs
    // closes the old callbacks, then awaits an engine reset before reusing their IDs. Further
    // edits while waiting replace the requested set. Returns any opening/matching problems.
    std::string setInputs(std::string_view wanted, std::int64_t now);
    std::optional<std::int64_t> deadline() const { return due_; }
    std::string poll(std::int64_t now);

    // The ports actually open, in InputId order; empty during replacement. A missing or
    // ambiguous name has no ID. Settings and tracks resolve against this same mapping.
    std::vector<MidiPort> ports() const;
    std::optional<core::InputId> inputId(std::string_view name) const;

private:
    struct Connection
    {
        std::string name;
        std::unique_ptr<Port> port;
    };
    std::string openRequested();

    static constexpr std::int64_t kResetPoll = 10;
    core::Engine& engine_;
    Backend backend_;
    std::function<void()> wake_;
    // Declared after callback dependencies, so callbacks finish while engine_/wake_ are valid.
    std::vector<Connection> connections_;
    bool opened_ = false;
    std::string wanted_;
    std::optional<std::uint64_t> reset_;
    std::optional<std::int64_t> due_;
};

} // namespace modal::platform
