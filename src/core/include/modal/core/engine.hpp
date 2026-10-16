#pragma once

#include <modal/core/input_queue.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>
#include <modal/core/snapshot_exchange.hpp>
#include <modal/core/spsc_queue.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/timing_stats.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace modal::core
{

// The engine's clock and sleep, provided by the platform layer, or faked in tests.
class EngineClock
{
public:
    virtual ~EngineClock() = default;

    virtual std::chrono::nanoseconds now() noexcept = 0;

    // Sleeps until a time on the clock, never waking early. nextEvent, if there is one, is
    // when a message is next due, at or soon after the time. Waking late for it matters, so a
    // clock may take extra care to be on time for it, at some cost in CPU time, and return at
    // nextEvent instead.
    virtual void sleepUntil(std::chrono::nanoseconds time,
                            std::optional<std::chrono::nanoseconds> nextEvent) noexcept = 0;

    // Ends the current sleep early, or if the engine isn't sleeping, the next one, so a wake
    // between the engine looking for work and going to sleep isn't lost. Safe to call from
    // any thread.
    virtual void wake() noexcept = 0;
};

// When the engine next needs to wake: for the next event, or within 1 ms to look for
// commands, whichever is sooner.
struct EngineWake
{
    std::chrono::nanoseconds time;
    std::optional<std::chrono::nanoseconds> nextEvent;

    friend bool operator==(const EngineWake&, const EngineWake&) = default;
};

struct EngineStatus
{
    bool playing = false;
    bool finished = false;        // every event sent and every note ended
    std::optional<Tick> position; // while playing
    std::size_t droppedNotes = 0; // see Scheduler::droppedNotes

    // How late messages were sent: from each one's due time to just after it was handed to
    // the MIDI layer. That is the sum of how late the engine got to it (waking, and any
    // messages sent before it in the same wake-up) and how long the send call took.
    TimingStats lateness;
    TimingStats wakeLateness; // from the due time to the start of the send call
    TimingStats sendTime;     // in the send call

    // MIDI input: messages the engine has taken in, and those dropped because it didn't
    // keep up (or a SysEx too long ever to fit).
    std::size_t inputReceived = 0;
    std::size_t inputDropped = 0;
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
    EngineWake step() noexcept;

    bool quitRequested() const noexcept { return quit_; }

    // Input side: from the thread receiving from an input, which must be the only one
    // calling this for that input. Queues a message for the engine and wakes it. Returns
    // false if the message was dropped because the engine hasn't kept up. Messages the engine
    // doesn't use (see InputQueue::push) are left out.
    bool receive(InputId input, std::span<const std::uint8_t> message,
                 std::chrono::nanoseconds time) noexcept;

    static constexpr std::chrono::nanoseconds kMaxSleep = std::chrono::milliseconds{1};
    static constexpr std::size_t kMaxInputs = 16;

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
    void takeInput() noexcept;
    void publishStatus(std::chrono::nanoseconds now) noexcept;

    // Control side.
    std::shared_ptr<const Snapshot> latest_;
    std::uint64_t playRequests_ = 0;

    // Shared.
    SnapshotExchange<Snapshot, 8> snapshots_;
    SpscQueue<Command, 64> commands_;
    std::unique_ptr<std::array<InputQueue, kMaxInputs>> inputs_; // too big for the stack
    std::atomic<std::size_t> inputDropped_{0};                   // written by input threads

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
    std::atomic<std::size_t> inputReceived_{0};
    SharedTimingStats lateness_;
    SharedTimingStats wakeLateness_;
    SharedTimingStats sendTime_;
};

} // namespace modal::core
