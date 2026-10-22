// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

// MSVC warns (C4324) that the cache-line alignment below pads the class, which is the point.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace modal::core
{

// A fixed-capacity, lock-free queue for exactly one producer thread and one consumer thread.
// Pushing and popping never allocate, block or throw, so either side can be the real-time
// engine thread. Popped slots are left moved-from, so for types like std::shared_ptr the
// queue never destroys a live value itself.
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_nothrow_default_constructible_v<T> &&
                      std::is_nothrow_move_constructible_v<T> &&
                      std::is_nothrow_move_assignable_v<T>,
                  "T must be default-constructible and movable without throwing");

public:
    static constexpr std::size_t capacity() { return Capacity; }

    // Producer only. On failure (the queue is full) the value is left untouched.
    bool tryPush(T&& value) noexcept
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) == Capacity)
        {
            return false;
        }
        slots_[tail & kMask] = std::move(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool tryPush(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
    {
        T copy = value;
        return tryPush(std::move(copy));
    }

    // Producer only: whether a push would fail now.
    bool full() const noexcept { return space() == 0; }

    // Producer only: how many pushes would succeed now. The consumer can only make more
    // room, so they'll all still succeed when made.
    std::size_t space() const noexcept
    {
        return Capacity -
               (tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire));
    }

    // Consumer only.
    std::optional<T> tryPop() noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        std::optional<T> value{std::move(slots_[head & kMask])};
        head_.store(head + 1, std::memory_order_release);
        return value;
    }

    // Consumer only: whether a pop would fail now.
    bool empty() const noexcept
    {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Indexes only ever increase; unsigned wrap-around keeps tail - head correct. Each sits
    // on its own cache line so the two threads don't contend for one.
    alignas(64) std::atomic<std::size_t> head_{0}; // written by the consumer
    alignas(64) std::atomic<std::size_t> tail_{0}; // written by the producer
    alignas(64) std::array<T, Capacity> slots_{};
};

} // namespace modal::core

#ifdef _MSC_VER
#pragma warning(pop)
#endif
