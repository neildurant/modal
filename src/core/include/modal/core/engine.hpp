// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/count_in.hpp>
#include <modal/core/input_queue.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>
#include <modal/core/snapshot_exchange.hpp>
#include <modal/core/spsc_queue.hpp>
#include <modal/core/thru.hpp>
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
    bool finished = false;          // every event sent and every note ended
    std::uint64_t finishedPlay = 0; // the completed play request, or 0 while unfinished
    bool countingIn = false;        // in a count-in, before the record point
    std::optional<Tick> position;   // while playing
    std::uint32_t pass = 0;         // the loop's pass being played (see Scheduler::passAt)
    std::size_t droppedNotes = 0;   // see Scheduler::droppedNotes

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
    std::size_t inputUnreported = 0; // not passed back, because nextReceived() didn't keep up

    // MIDI thru: from each message's arrival to just after it was sent on, and note-ons not
    // sent because too many thru notes were held (see Thru::kMaxHeldNotes).
    TimingStats thruDelay;
    std::size_t thruDroppedNotes = 0;
};

namespace detail
{

// What the engine plays, with the last play request's chased events, so that the
// engine never allocates them.
struct EngineSnapshot
{
    std::shared_ptr<const PlaybackPlan> plan;
    std::shared_ptr<const TempoMap> inputTempo;
    std::uint64_t playRequest = 0;
    Tick start = 0;
    std::shared_ptr<const std::vector<PlannedEvent>> chased;
    std::shared_ptr<const PlannedCountIn> countIn;
    std::optional<PlaybackLoop> loop;
    std::shared_ptr<const std::vector<PlannedEvent>> loopChase;
    std::uint64_t loopChange = 0; // counts setLoop requests, so one isn't lost among others
};

struct EngineCommand
{
    enum class Kind : std::uint8_t
    {
        Play,
        Stop,
        Panic,
        Quit,
        ResetInputs,
        Audition,
        Send,
    };
    Kind kind = Kind::Stop;
    std::uint64_t request = 0; // Play: its snapshot; ResetInputs: its acknowledgement
    // Audition: the note-on, where to and for how long.
    DestinationId destination{0};
    std::array<std::uint8_t, 3> note{};
    std::chrono::nanoseconds length{};
    std::uint8_t size = 3; // Send: how many of note's bytes the message is
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
    [[nodiscard]] bool setPlan(std::shared_ptr<const PlaybackPlan> plan);

    // Starts playing the current plan from a tick, chasing what's before it, after the
    // count-in if there is one (see planRecordStart). Does nothing without a plan.
    [[nodiscard]] bool play(Tick start, std::optional<PlannedCountIn> countIn = std::nullopt,
                            std::optional<PlaybackLoop> loop = std::nullopt);

    // The id of the last play() request, as StampedInput::play gives it.
    std::uint64_t lastPlay() const noexcept { return playRequests_; }

    // Input the engine has taken in, stamped with when and where in the song it arrived, in
    // the order taken in (each input in arrival order). Only one thread may call this.
    std::optional<StampedInput> nextReceived() noexcept { return stamped_->tryPop(); }

    // Changes the loop of the play going on, at once, or ends it (see Scheduler::setLoop);
    // nothing while stopped. Returns false where the engine hasn't kept up, or the loop
    // reaches past the plan's end.
    [[nodiscard]] bool setLoop(std::optional<PlaybackLoop> loop);

    [[nodiscard]] bool stop();

    // Where a play stopped (by stop, panic, quit or another play), once the engine has done
    // so: before its start tick if stopped during a count-in or lead-in. For the last play
    // only; the tick of an earlier one may have been replaced.
    std::optional<Tick> stoppedAt(std::uint64_t play) const noexcept;

    // Stops, sends All Notes Off and Reset All Controllers (see Scheduler::panic), and ends
    // every held thru note and pedal.
    [[nodiscard]] bool panic();

    // Close all input producers before requesting this, and keep them closed until the
    // acknowledgement. Drains their queued messages and releases held thru notes and pedals,
    // without stopping playback. No request is made if the command queue is full; retry later.
    [[nodiscard]] std::optional<std::uint64_t> resetInputs();
    bool inputsReset(std::uint64_t request) const noexcept;

    // Turns MIDI thru on or off for every track (on to begin with). Notes and pedals already
    // held still end where they went.
    void setThru(bool on) { thruOn_.store(on, std::memory_order_relaxed); }

    // Plays one note now, ending it after a length, as when trying the click's sound; one
    // still sounding from before ends first. It's not playback, so stop doesn't end it early,
    // but panic and quit do.
    [[nodiscard]] bool audition(DestinationId destination, std::uint8_t channel, std::uint8_t pitch,
                                std::uint8_t velocity, std::chrono::nanoseconds length);

    // Sends one short message now, of two or three bytes, such as a controller's value, as
    // cancelling a ramp's draft puts back the value playing (decided 17 November 2026).
    [[nodiscard]] bool send(DestinationId destination, std::span<const std::uint8_t> message);

    // Ends run() once the commands before it have been carried out.
    [[nodiscard]] bool quit();

    EngineStatus status() const;

    // Accepted messages not yet passed through the engine. Read before collecting stamped
    // input: once false, all prior messages' stamped chunks have been published.
    bool inputPending() const noexcept;

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
    static constexpr std::size_t kMaxStamped = 4096; // chunks waiting for nextReceived()
    // Normal input work per pass, in whole messages, rotating ports after each quantum.
    // Before transport commands, the remaining captured backlog is drained; later arrivals
    // wait for the next pass, so commands cannot be held up by a producer refilling a queue.
    static constexpr std::size_t kInputBatch = 32;
    static constexpr std::size_t kInputQuantum = 8;

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

    // Measures thru delay on the way to the real sink; the due time is the arrival time.
    class ThruSink final : public MessageSink
    {
    public:
        ThruSink(Engine& engine)
            : engine_(engine)
        {
        }

        void send(DestinationId destination, std::span<const std::uint8_t> bytes,
                  std::chrono::nanoseconds arrival) noexcept override;

    private:
        Engine& engine_;
    };

    bool publish(Snapshot snapshot);
    bool woken(bool taken) noexcept;
    bool carryOut(const Command& command, std::chrono::nanoseconds now) noexcept;
    using InputBatch = std::array<std::size_t, kMaxInputs>;
    InputBatch queuedInput() const noexcept;
    void takeInput(InputBatch& queued, const Snapshot* snapshot,
                   std::size_t most = kInputBatch) noexcept;
    void publishStatus(std::chrono::nanoseconds now) noexcept;
    void noteStop(std::chrono::nanoseconds now) noexcept;

    // Control side.
    std::shared_ptr<const Snapshot> latest_;
    std::uint64_t playRequests_ = 0;
    std::uint64_t inputResetRequests_ = 0;
    // What each play not yet replaced was given to chase and count in with, kept alive for the
    // scheduler, which holds them without owning them, until a later play has started.
    struct PlayData
    {
        std::uint64_t request;
        std::shared_ptr<const std::vector<PlannedEvent>> chased;
        std::shared_ptr<const PlannedCountIn> countIn;
    };
    std::vector<PlayData> plays_;

    // Shared.
    SnapshotExchange<Snapshot, 8> snapshots_;
    SpscQueue<Command, 64> commands_;
    std::unique_ptr<std::array<InputQueue, kMaxInputs>> inputs_; // too big for the stack
    std::atomic<std::size_t> inputQueued_{0};  // accepted whole messages, from all input threads
    std::atomic<std::size_t> inputDropped_{0}; // written by input threads
    std::atomic<bool> thruOn_{true};
    std::unique_ptr<SpscQueue<StampedInput, kMaxStamped>> stamped_;

    // Engine side.
    MessageSink& sink_;
    EngineClock& clock_;
    MeasuringSink measuring_{*this};
    Scheduler scheduler_;
    ThruSink thruSink_{*this};
    Thru thru_{thruSink_};
    std::optional<Command> pending_; // a Play waiting for its snapshot
    // The auditioned note still sounding: where, its note-off, and when that's due.
    struct Auditioned
    {
        DestinationId destination;
        std::array<std::uint8_t, 3> off;
        std::chrono::nanoseconds due;
    };
    std::optional<Auditioned> auditioned_;
    void endAudition(std::chrono::nanoseconds now) noexcept;
    std::uint64_t currentPlay_ = 0; // 0 while stopped
    std::uint64_t loopChange_ = 0;  // the last setLoop request carried out
    std::size_t nextInput_ = 0;     // rotating input service, shared by normal and transport drains
    bool quit_ = false;

    // Status, written by the engine thread only.
    std::atomic<std::uint64_t> inputsReset_{0}; // acknowledgement, after releasing held input
    std::atomic<std::uint64_t> startedPlay_{0}; // the last play begun, once the scheduler has it
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> finishedPlay_{0};
    std::atomic<bool> countingIn_{false};
    static constexpr Tick kNoPosition = -1;
    std::atomic<Tick> position_{kNoPosition};
    std::atomic<std::uint32_t> pass_{0};
    std::atomic<std::size_t> droppedNotes_{0};
    std::atomic<std::size_t> inputReceived_{0};
    std::atomic<std::size_t> inputUnreported_{0};
    SharedTimingStats lateness_;
    SharedTimingStats wakeLateness_;
    SharedTimingStats sendTime_;
    SharedTimingStats thruDelay_;
    std::atomic<std::size_t> thruDroppedNotes_{0};
    std::atomic<Tick> stopTick_{0};
    std::atomic<std::uint64_t> stoppedPlay_{0}; // published after stopTick_
};

} // namespace modal::core
