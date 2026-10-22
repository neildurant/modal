// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/snapshot_exchange.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <thread>

using modal::core::SnapshotExchange;

namespace
{

thread_local bool tIsConsumer = false;
std::atomic<int> gLive{0};
std::atomic<int> gFreedOnConsumer{0};

// A snapshot that counts how many exist and whether any was freed on the consumer thread.
struct Snapshot
{
    explicit Snapshot(int v)
        : version(v)
    {
        ++gLive;
    }
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    ~Snapshot()
    {
        --gLive;
        if (tIsConsumer)
        {
            ++gFreedOnConsumer;
        }
    }

    int version;
};

std::shared_ptr<const Snapshot> make(int version)
{
    return std::make_shared<const Snapshot>(version);
}

} // namespace

TEST_CASE("the consumer picks up the latest snapshot")
{
    SnapshotExchange<Snapshot, 4> exchange;
    CHECK(exchange.current() == nullptr);
    CHECK_FALSE(exchange.update());

    exchange.publish(make(1));
    exchange.publish(make(2));
    exchange.publish(make(3));
    CHECK(exchange.update());
    REQUIRE(exchange.current());
    CHECK(exchange.current()->version == 3);
    CHECK_FALSE(exchange.update());

    // The two replaced snapshots wait for the producer to free them.
    CHECK(gLive == 3);
    exchange.reclaim();
    CHECK(gLive == 1);
}

TEST_CASE("publishing fails when the consumer falls behind")
{
    SnapshotExchange<Snapshot, 2> exchange;
    CHECK(exchange.publish(make(1)));
    CHECK(exchange.publish(make(2)));
    CHECK_FALSE(exchange.publish(make(3))); // dropped on this thread
    CHECK(gLive == 2);

    exchange.update();
    CHECK(exchange.current()->version == 2);
    CHECK(exchange.publish(make(4)));
}

// publish() reclaims before queueing, so the retired queue always has room for what's
// incoming; update()'s check that it does is a safety net that can't be reached from here.
TEST_CASE("replaced snapshots are freed by the producer, never the consumer")
{
    SnapshotExchange<Snapshot, 2> exchange;
    gFreedOnConsumer = 0;
    const auto update = [&]
    {
        tIsConsumer = true;
        const bool changed = exchange.update();
        tIsConsumer = false;
        return changed;
    };

    exchange.publish(make(1));
    exchange.publish(make(2));
    CHECK(update());           // current 2, retired {1}
    exchange.publish(make(3)); // frees 1
    exchange.publish(make(4));
    CHECK(update()); // current 4, previous 2, retired {3}
    CHECK(exchange.current()->version == 4);
    CHECK(gLive == 3);
    CHECK_FALSE(update()); // retires 2

    exchange.reclaim();
    CHECK(gLive == 1);
    CHECK(gFreedOnConsumer == 0);
}

TEST_CASE("the snapshot replaced stays alive until the next update")
{
    SnapshotExchange<Snapshot, 4> exchange;
    exchange.publish(make(1));
    exchange.update();
    const Snapshot* first = exchange.current();

    exchange.publish(make(2));
    CHECK(exchange.update());
    exchange.reclaim();
    CHECK(first->version == 1); // still alive for the consumer to finish with
    CHECK(gLive == 2);

    CHECK_FALSE(exchange.update());
    exchange.reclaim();
    CHECK(gLive == 1);
}

TEST_CASE("snapshots cross threads without the consumer freeing any")
{
    constexpr int kCount = 20'000;
    gFreedOnConsumer = 0;
    {
        SnapshotExchange<Snapshot, 8> exchange;

        std::thread producer{[&]
                             {
                                 for (int version = 1; version <= kCount;)
                                 {
                                     if (exchange.publish(make(version)))
                                     {
                                         ++version;
                                     }
                                     else
                                     {
                                         std::this_thread::yield();
                                     }
                                 }
                             }};

        bool monotonic = true;
        std::thread consumer{[&]
                             {
                                 tIsConsumer = true;
                                 int last = 0;
                                 while (last < kCount)
                                 {
                                     exchange.update();
                                     if (const auto* snapshot = exchange.current())
                                     {
                                         monotonic = monotonic && snapshot->version >= last;
                                         last = snapshot->version;
                                     }
                                     else
                                     {
                                         std::this_thread::yield();
                                     }
                                 }
                                 tIsConsumer = false;
                             }};

        producer.join();
        consumer.join();
        // The consumer's last update can leave the retired queue full, so make room before
        // retiring the snapshot the last one replaced.
        exchange.reclaim();
        exchange.update();
        exchange.reclaim();
        CHECK(monotonic);
        CHECK(exchange.current()->version == kCount);
        CHECK(gLive == 1);
    }
    CHECK(gLive == 0);
    CHECK(gFreedOnConsumer == 0);
}
