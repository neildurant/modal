// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/spsc_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

using modal::core::SpscQueue;

TEST_CASE("an SPSC queue is first in, first out")
{
    SpscQueue<int, 4> queue;
    CHECK(queue.empty());
    CHECK_FALSE(queue.tryPop());

    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));
    CHECK_FALSE(queue.empty());
    CHECK(queue.tryPop() == 1);
    CHECK(queue.tryPop() == 2);
    CHECK_FALSE(queue.tryPop());
    CHECK(queue.empty());
}

TEST_CASE("an SPSC queue holds exactly its capacity")
{
    SpscQueue<int, 4> queue;
    for (int i = 0; i < 4; ++i)
    {
        CHECK_FALSE(queue.full());
        CHECK(queue.space() == static_cast<std::size_t>(4 - i));
        CHECK(queue.tryPush(i));
    }
    CHECK(queue.full());
    CHECK(queue.space() == 0);
    CHECK_FALSE(queue.tryPush(99));
    CHECK(queue.tryPop() == 0);
    CHECK(queue.tryPush(4));
    for (int i = 1; i <= 4; ++i)
    {
        CHECK(queue.tryPop() == i);
    }
}

TEST_CASE("an SPSC queue keeps order across many wrap-arounds")
{
    SpscQueue<int, 8> queue;
    int next = 0;
    int expected = 0;
    for (int round = 0; round < 1000; ++round)
    {
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE(queue.tryPush(next++));
        }
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE(queue.tryPop() == expected++);
        }
    }
}

TEST_CASE("a failed push leaves the value with the caller")
{
    SpscQueue<std::unique_ptr<int>, 2> queue;
    REQUIRE(queue.tryPush(std::make_unique<int>(1)));
    REQUIRE(queue.tryPush(std::make_unique<int>(2)));
    auto third = std::make_unique<int>(3);
    CHECK_FALSE(queue.tryPush(std::move(third)));
    REQUIRE(third);
    CHECK(*third == 3);

    // Popping moves the value out, leaving the slot empty.
    auto first = queue.tryPop();
    REQUIRE(first);
    CHECK(**first == 1);
}

TEST_CASE("an SPSC queue passes values between threads in order")
{
    constexpr std::uint32_t kCount = 200'000;
    SpscQueue<std::uint32_t, 64> queue;

    std::thread producer{[&]
                         {
                             for (std::uint32_t i = 0; i < kCount;)
                             {
                                 if (queue.tryPush(i))
                                 {
                                     ++i;
                                 }
                                 else
                                 {
                                     std::this_thread::yield();
                                 }
                             }
                         }};

    std::uint32_t expected = 0;
    bool inOrder = true;
    while (expected < kCount)
    {
        if (const auto value = queue.tryPop())
        {
            inOrder = inOrder && *value == expected;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();

    CHECK(inOrder);
    CHECK(queue.empty());
}
