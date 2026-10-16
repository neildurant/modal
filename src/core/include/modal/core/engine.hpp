#pragma once

#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>
#include <modal/core/snapshot_exchange.hpp>
#include <modal/core/spsc_queue.hpp>
#include <modal/core/tick.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace modal::core
{

// The engine's clock and sleep, provided by the platform layer, or faked in tests.
class EngineClock
{
public:
    virtual ~EngineClock() = default;

    virtual std::chrono::nanoseconds now() noexcept = 0;

    // Sleeps until a time on the clock, never waking early.
    virtual void sleepUntil(std::chrono::nanoseconds time) noexcept = 0;
};

// How late messages were sent: from each message's due time to just after it was handed to
// the MIDI layer.
struct LatenessStats
{
    // Upper limits of the histogram's buckets; a last bucket holds anything later.
    static constexpr std::array<std::chrono::nanoseconds, 7> kLimits{
        std::chrono::microseconds{100}, std::chrono::microseconds{250},
        std::chrono::microseconds{500}, std::chrono::milliseconds{1},
        std::chrono::milliseconds{2},   std::chrono::milliseconds{5},
        std::chrono::milliseconds{10},
    };

    std::size_t count = 0;
    std::chrono::nanoseconds max{0};
    std::array<std::size_t, kLimits.size() + 1> buckets{}; // buckets[i]: below kLimits[i]

    // The limit that at least a fraction of the messages (e.g. 0.99) were sent within, or
    // none if that's beyond the last limit or nothing has been sent.
    std::optional<std::chrono::nanoseconds> within(double fraction) const;
};

struct EngineStatus
{
    bool playing = false;
    bool finished = false;        // every event sent and every note ended
    std::optional<Tick> position; // while playing
    std::size_t droppedNotes = 0; // see Scheduler::droppedNotes
    LatenessStats lateness;
};

namespace detail
{

// What the engine plays, with the last play request's chased events, so that the
// engine never allocates them.
struct EngineSnapshot
{
    std::shared_ptr<const PlaybackPlan> plan;
    std::uint64_t playRequest = 0;
    Tick start = 0;
    std::shared_ptr<const std::vector<PlannedEvent>> chased;
};

struct EngineCommand
{
    enum class Kind : std::uint8_t
    {
        Play,
        Stop,
        Panic,
        Quit,
    };
    Kind kind = Kind::Stop;
    std::uint64_t playRequest = 0; // for Play: the snapshot that carries it
};

} // namespace detail

// Runs the scheduler on the engine thread. The control side (the editor, or a command-line
// tool) sets plans and sends transport commands; nothing it calls waits for the engine
// thread, and the engine thread never allocates, frees or waits for it.
//
// The caller creates the engine thread and calls run() on it, after raising its priority.
// Tests can call step() instead, with a fake clock.
class Engine
{
public:
    Engine(MessageSink& sink, EngineClock& clock, StopOptions options = {});

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Control side. Each returns false if the engine hasn't kept up and the request was
    // dropped; the caller can try again.

    // Replaces the plan, in playback too (see Scheduler::setPlan).
    bool setPlan(std::shared_ptr<const PlaybackPlan> plan);

    // Starts playing the current plan from a tick, chasing what's before it. Does nothing
    // without a plan.
    bool play(Tick start);

    bool stop();
    bool panic();

    // Ends run() once the commands before it have been carried out.
    bool quit();

    EngineStatus status() const;

    // Engine thread. Runs until quit.
    void run() noexcept;

    // One pass of run(): carries out new commands and plans, sends what is due, and returns
    // when to wake for the next pass (at most 1 ms ahead).
    std::chrono::nanoseconds step() noexcept;

    bool quitRequested() const noexcept { return quit_; }

    static constexpr std::chrono::nanoseconds kMaxSleep = std::chrono::milliseconds{1};

private:
    using Snapshot = detail::EngineSnapshot;
    using Command = detail::EngineCommand;

    // Measures lateness on the way to the real sink.
    class MeasuringSink final : public MessageSink
    {
    public:
        MeasuringSink(Engine& engine)
            : engine_(engine)
        {
        }

        void send(DestinationId destination, std::span<const std::uint8_t> bytes,
                  std::chrono::nanoseconds due) noexcept override;

    private:
        Engine& engine_;
    };

    bool publish(Snapshot snapshot);
    bool carryOut(const Command& command, std::chrono::nanoseconds now) noexcept;
    void publishStatus(std::chrono::nanoseconds now) noexcept;

    // Control side.
    std::shared_ptr<const Snapshot> latest_;
    std::uint64_t playRequests_ = 0;

    // Shared.
    SnapshotExchange<Snapshot, 8> snapshots_;
    SpscQueue<Command, 64> commands_;

    // Engine side.
    MessageSink& sink_;
    EngineClock& clock_;
    MeasuringSink measuring_{*this};
    Scheduler scheduler_;
    std::optional<Command> pending_; // a Play waiting for its snapshot
    bool quit_ = false;

    // Status, written by the engine thread only.
    std::atomic<bool> playing_{false};
    std::atomic<bool> finished_{false};
    static constexpr Tick kNoPosition = -1;
    std::atomic<Tick> position_{kNoPosition};
    std::atomic<std::size_t> droppedNotes_{0};
    std::atomic<std::size_t> sent_{0};
    std::atomic<std::int64_t> maxLateness_{0};
    std::array<std::atomic<std::size_t>, LatenessStats::kLimits.size() + 1> buckets_{};
};

} // namespace modal::core
