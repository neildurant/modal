// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/spsc_queue.hpp>

#include <cstddef>
#include <memory>
#include <utility>

namespace modal::core
{

// Hands immutable snapshots (a song, a playback plan) from one producer thread to one
// real-time consumer thread, without the consumer ever freeing memory.
//
// The producer publishes snapshots; the consumer picks up the latest one between events and
// hands the ones it replaces back, and the producer frees them in reclaim(). Snapshots pass
// through the queues by move, so the consumer never changes a reference count to zero.
template <typename T, std::size_t Capacity = 8>
class SnapshotExchange
{
public:
    // Producer only. Frees retired snapshots, then queues this one. Returns false if the
    // consumer hasn't kept up and the queue is full; the snapshot is then dropped (here, on
    // the producer thread) and the caller can publish a newer one later.
    bool publish(std::shared_ptr<const T> snapshot)
    {
        reclaim();
        return incoming_.tryPush(std::move(snapshot));
    }

    // Producer only. Frees the snapshots the consumer has finished with.
    void reclaim()
    {
        while (retired_.tryPop())
        {
        }
    }

    // Consumer only; real-time safe. Moves to the latest published snapshot, retiring the
    // ones it replaces. The snapshot that was current stays alive until the next update, so
    // the consumer can finish with it after switching. Takes no more snapshots than it can
    // retire, so if the producer stops reclaiming, the consumer keeps its current snapshot
    // rather than freeing one. Returns whether the current snapshot changed.
    bool update() noexcept
    {
        if (previous_)
        {
            if (!retired_.tryPush(std::move(previous_)))
            {
                return false;
            }
        }

        bool changed = false;
        while (!retired_.full())
        {
            auto next = incoming_.tryPop();
            if (!next)
            {
                break;
            }
            if (!changed)
            {
                previous_ = std::move(current_);
            }
            else
            {
                retired_.tryPush(std::move(current_));
            }
            current_ = std::move(*next);
            changed = true;
        }
        return changed;
    }

    // Consumer only. The current snapshot, or null before the first update that found one.
    const T* current() const noexcept { return current_.get(); }

private:
    SpscQueue<std::shared_ptr<const T>, Capacity> incoming_;
    SpscQueue<std::shared_ptr<const T>, Capacity> retired_;
    std::shared_ptr<const T> current_;  // owned by the consumer
    std::shared_ptr<const T> previous_; // likewise, until the next update
};

} // namespace modal::core
