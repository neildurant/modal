#pragma once

#include <modal/core/spsc_queue.hpp>

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace modal::core
{

// Which input port a message came from.
struct InputId
{
    std::uint32_t value;

    friend constexpr auto operator<=>(InputId, InputId) = default;
};

// A piece of a received message: a whole channel message, or up to kMaxBytes of a SysEx.
struct InputChunk
{
    static constexpr std::size_t kMaxBytes = 32;

    std::chrono::nanoseconds time{}; // when the message arrived, on the engine's clock
    std::array<std::uint8_t, kMaxBytes> bytes{};
    std::uint8_t size = 0;
    bool more = false; // the next chunk carries on the same SysEx
};

// Messages from one input port on their way to the engine, timestamped as they arrived. One
// thread, the one receiving from that port, pushes; the engine pops. Neither allocates or
// blocks.
class InputQueue
{
public:
    static constexpr std::size_t kCapacity = 1024; // chunks

    // The largest SysEx that can ever fit.
    static constexpr std::size_t kMaxSysEx = kCapacity * InputChunk::kMaxBytes;

    // Producer. Pushes a message as one chunk, or as several for a long SysEx: all of them,
    // or if they don't all fit, none. Only channel messages and SysEx are kept; anything else
    // (system real-time and common messages, or a malformed message) is left out.
    enum class Result : std::uint8_t
    {
        Pushed,
        Ignored,
        Dropped, // no room
    };
    Result push(std::span<const std::uint8_t> message, std::chrono::nanoseconds time) noexcept;

    // Consumer.
    std::optional<InputChunk> pop() noexcept { return chunks_.tryPop(); }

private:
    SpscQueue<InputChunk, kCapacity> chunks_;
};

} // namespace modal::core
