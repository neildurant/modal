// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/input_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

using namespace modal::core;
using namespace std::chrono_literals;
using Bytes = std::vector<std::uint8_t>;

namespace
{

// A SysEx of a given total length, its data bytes counting up.
Bytes sysEx(std::size_t size)
{
    Bytes bytes(size);
    bytes.front() = 0xF0;
    for (std::size_t i = 1; i + 1 < size; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(i % 128);
    }
    bytes.back() = 0xF7;
    return bytes;
}

// The next message, joined up from its chunks.
Bytes popMessage(InputQueue& queue)
{
    Bytes message;
    while (const auto chunk = queue.pop())
    {
        message.insert(message.end(), chunk->bytes.begin(), chunk->bytes.begin() + chunk->size);
        if (!chunk->more)
        {
            break;
        }
    }
    return message;
}

} // namespace

TEST_CASE("channel messages go through the input queue whole, with their times")
{
    auto queue = std::make_unique<InputQueue>();
    CHECK(queue->push(Bytes{0x90, 60, 100}, 5ms) == InputQueue::Result::Pushed);
    CHECK(queue->push(Bytes{0xC3, 7}, 6ms) == InputQueue::Result::Pushed);

    auto chunk = queue->pop();
    REQUIRE(chunk);
    CHECK(chunk->time == 5ms);
    CHECK(chunk->size == 3);
    CHECK_FALSE(chunk->more);
    CHECK(Bytes(chunk->bytes.begin(), chunk->bytes.begin() + 3) == Bytes{0x90, 60, 100});
    chunk = queue->pop();
    REQUIRE(chunk);
    CHECK(chunk->time == 6ms);
    CHECK(chunk->size == 2);
    CHECK_FALSE(queue->pop());
}

TEST_CASE("a long SysEx goes through in chunks and comes out the same")
{
    auto queue = std::make_unique<InputQueue>();
    for (const std::size_t size :
         {std::size_t{2}, InputChunk::kMaxBytes, InputChunk::kMaxBytes + 1, std::size_t{1000}})
    {
        const auto message = sysEx(size);
        CHECK(queue->push(message, 1ms) == InputQueue::Result::Pushed);
        CHECK(popMessage(*queue) == message);
        CHECK_FALSE(queue->pop());
    }
}

TEST_CASE("the input queue leaves out what the engine doesn't use")
{
    auto queue = std::make_unique<InputQueue>();
    for (const auto& message : std::vector<Bytes>{
             {},
             {0xF8},          // clock
             {0xFE},          // active sensing
             {0xFA},          // start
             {0xF1, 0x10},    // MTC quarter frame
             {0xF2, 0, 0},    // song position
             {60, 100},       // no status
             {0x90, 60},      // too short
             {0xC0, 1, 2},    // too long
             {0x90, 60, 200}, // a data byte with the top bit set
         })
    {
        CHECK(queue->push(message, 1ms) == InputQueue::Result::Ignored);
    }
    CHECK_FALSE(queue->pop());
}

TEST_CASE("the input queue drops a message it has no room for, rather than part of one")
{
    auto queue = std::make_unique<InputQueue>();
    for (std::size_t i = 0; i < InputQueue::kCapacity - 2; ++i)
    {
        REQUIRE(queue->push(Bytes{0x90, 60, 100}, 1ms) == InputQueue::Result::Pushed);
    }
    CHECK(queue->push(sysEx(3 * InputChunk::kMaxBytes), 1ms) == InputQueue::Result::Dropped);
    CHECK(queue->push(sysEx(2 * InputChunk::kMaxBytes), 1ms) == InputQueue::Result::Pushed);
    CHECK(queue->push(Bytes{0x90, 60, 100}, 1ms) == InputQueue::Result::Dropped);

    for (std::size_t i = 0; i < InputQueue::kCapacity - 2; ++i)
    {
        REQUIRE(popMessage(*queue) == Bytes{0x90, 60, 100});
    }
    CHECK(popMessage(*queue) == sysEx(2 * InputChunk::kMaxBytes));

    CHECK(queue->push(sysEx(InputQueue::kMaxSysEx + 1), 1ms) == InputQueue::Result::Dropped);
    CHECK(queue->push(sysEx(InputQueue::kMaxSysEx), 1ms) == InputQueue::Result::Pushed);
}
