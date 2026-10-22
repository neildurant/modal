// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/scheduler.hpp>
#include <modal/platform/midi_ports.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace modal::platform
{

struct OutputOpenResult : PortOpenResult
{
    core::DestinationId destination{};
};

// The MIDI output ports the engine sends to, each one a destination. Ports are opened on the
// control side, and can be opened while the engine is sending: the list of them never moves,
// and a new port is published to the engine only once it's open.
class MidiOutputs final : public core::MessageSink
{
public:
    MidiOutputs();
    ~MidiOutputs() override;

    MidiOutputs(const MidiOutputs&) = delete;
    MidiOutputs& operator=(const MidiOutputs&) = delete;

    // Opens the output port a name refers to (see matchPort), or returns the destination
    // already opened for it. Only one thread may call this.
    OutputOpenResult open(std::string_view name);

    // Creates an output port of Modal's own with a name, which other programs connect to as a
    // source of MIDI, or returns the destination already created for it. Possible where the
    // MIDI API lets programs create ports (ALSA on Linux), not with WinMM on Windows. Only one
    // thread may call this, the same as calls open.
    OutputOpenResult openOwn(std::string_view name);

    // Sends at once; due is ignored. A failed send is counted, not reported. On Windows, a
    // SysEx message blocks until it has all been sent.
    void send(core::DestinationId destination, std::span<const std::uint8_t> bytes,
              std::chrono::nanoseconds due) noexcept override;

    std::size_t failedSends() const noexcept
    {
        return failedSends_.load(std::memory_order_relaxed);
    }

private:
    struct Port;
    // Indexed by destination, as many as the scheduler can reach.
    std::array<std::unique_ptr<Port>, core::Scheduler::kMaxDestinations> ports_;
    std::atomic<std::size_t> opened_{0}; // published after the port it counts
    std::atomic<std::size_t> failedSends_{0};
};

} // namespace modal::platform
