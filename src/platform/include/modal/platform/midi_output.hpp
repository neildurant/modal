#pragma once

#include <modal/core/scheduler.hpp>
#include <modal/platform/midi_ports.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace modal::platform
{

struct OutputOpenResult : PortOpenResult
{
    core::DestinationId destination{};
};

// The MIDI output ports the engine sends to, each one a destination. Ports are opened before
// playback starts, never while the engine is sending.
class MidiOutputs final : public core::MessageSink
{
public:
    MidiOutputs();
    ~MidiOutputs() override;

    MidiOutputs(const MidiOutputs&) = delete;
    MidiOutputs& operator=(const MidiOutputs&) = delete;

    // Opens the output port a name refers to (see matchPort), or returns the destination
    // already opened for it.
    OutputOpenResult open(std::string_view name);

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
    std::vector<std::unique_ptr<Port>> ports_; // indexed by destination
    std::atomic<std::size_t> failedSends_{0};
};

} // namespace modal::platform
