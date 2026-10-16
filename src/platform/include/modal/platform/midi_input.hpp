#pragma once

#include <modal/platform/midi_ports.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace modal::platform
{

// Receives from one MIDI input port. Each message (SysEx whole; MIDI clock and active sensing
// left out) goes to the callback, on a thread of the MIDI layer's own, with the time it
// arrived on the engine's clock (see clock.hpp).
class MidiInput
{
public:
    using Callback =
        std::function<void(std::span<const std::uint8_t> message, std::chrono::nanoseconds time)>;

    explicit MidiInput(Callback callback);
    ~MidiInput();

    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    // Opens the input port a name refers to (see matchPort), closing any port already open.
    PortOpenResult open(std::string_view name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace modal::platform
