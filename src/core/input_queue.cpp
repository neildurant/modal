// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/input_queue.hpp>

#include <algorithm>
#include <utility>

namespace modal::core
{

namespace
{

// A channel message's length from its status byte.
constexpr std::size_t channelMessageSize(std::uint8_t status)
{
    const auto kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 2 : 3;
}

bool isWanted(std::span<const std::uint8_t> message)
{
    if (message.empty())
    {
        return false;
    }
    const auto status = message[0];
    if (status == 0xF0)
    {
        return true;
    }
    if (status < 0x80 || status >= 0xF0 || message.size() != channelMessageSize(status))
    {
        return false;
    }
    return std::all_of(message.begin() + 1, message.end(),
                       [](std::uint8_t byte) { return byte < 0x80; });
}

} // namespace

InputQueue::Result InputQueue::push(std::span<const std::uint8_t> message,
                                    std::chrono::nanoseconds time) noexcept
{
    if (!isWanted(message))
    {
        return Result::Ignored;
    }
    const auto chunks = (message.size() + InputChunk::kMaxBytes - 1) / InputChunk::kMaxBytes;
    if (chunks > chunks_.space())
    {
        return Result::Dropped;
    }
    while (!message.empty())
    {
        InputChunk chunk;
        chunk.time = time;
        chunk.size = static_cast<std::uint8_t>(std::min(message.size(), InputChunk::kMaxBytes));
        std::copy_n(message.begin(), chunk.size, chunk.bytes.begin());
        message = message.subspan(chunk.size);
        chunk.more = !message.empty();
        chunks_.tryPush(std::move(chunk)); // room was checked above, and only we push
    }
    return Result::Pushed;
}

} // namespace modal::core
