// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/engine.hpp>

#include <algorithm>
#include <cassert>
#include <span>
#include <utility>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

} // namespace

Engine::Engine(MessageSink& sink, EngineClock& clock, StopOptions options)
    : inputs_(std::make_unique<std::array<InputQueue, kMaxInputs>>())
    , stamped_(std::make_unique<SpscQueue<StampedInput, kMaxStamped>>())
    , sink_(sink)
    , clock_(clock)
    , scheduler_(measuring_, options)
{
}

bool Engine::setPlan(std::shared_ptr<const PlaybackPlan> plan)
{
    assert(plan);
    Snapshot next = latest_ ? *latest_ : Snapshot{};
    // Input retains the mapping alone, so delayed collection cannot pin whole old plans.
    next.inputTempo = std::make_shared<const TempoMap>(plan->tempo);
    next.plan = std::move(plan);
    if (next.loop)
    {
        next.loopChase = std::make_shared<const std::vector<PlannedEvent>>(
            chase(*next.plan, next.loop->restartFrom(next.start), false));
    }
    return publish(std::move(next)) && woken(true);
}

bool Engine::play(Tick start, std::optional<PlannedCountIn> countIn,
                  std::optional<PlaybackLoop> loop)
{
    if (!latest_)
    {
        return !loop;
    }
    if (commands_.full() || (loop && (start < 0 || start >= loop->to || loop->from >= loop->to ||
                                      loop->to > latest_->plan->end)))
    {
        return false;
    }
    Snapshot next = *latest_;
    next.playRequest = playRequests_ + 1;
    next.start = start;
    next.chased = std::make_shared<const std::vector<PlannedEvent>>(chase(*next.plan, start));
    next.countIn = countIn ? std::make_shared<const PlannedCountIn>(std::move(*countIn)) : nullptr;
    next.loop = loop;
    next.loopChase = loop ? std::make_shared<const std::vector<PlannedEvent>>(
                                chase(*next.plan, loop->restartFrom(start), false))
                          : nullptr;
    PlayData data{playRequests_ + 1, next.chased, next.countIn};
    if (!publish(std::move(next)))
    {
        return false;
    }
    ++playRequests_;
    // The scheduler reads a play's chased events and count-in until the next play starts,
    // which can be after the snapshots that carried them have gone. So they're kept here
    // until the engine says it has started a later play (found in a code review, November
    // 2026).
    const auto started = startedPlay_.load(std::memory_order_acquire);
    std::erase_if(plays_, [&](const PlayData& play) { return play.request < started; });
    plays_.push_back(std::move(data));
    return woken(commands_.tryPush({Command::Kind::Play, playRequests_}));
}

bool Engine::stop()
{
    return woken(commands_.tryPush({Command::Kind::Stop}));
}

bool Engine::panic()
{
    return woken(commands_.tryPush({Command::Kind::Panic}));
}

bool Engine::audition(DestinationId destination, std::uint8_t channel, std::uint8_t pitch,
                      std::uint8_t velocity, nanoseconds length)
{
    Command command{Command::Kind::Audition};
    command.destination = destination;
    command.note = {static_cast<std::uint8_t>(0x90 | (channel & 0x0F)),
                    static_cast<std::uint8_t>(pitch & 0x7F),
                    static_cast<std::uint8_t>(std::clamp<int>(velocity, 1, 127))};
    command.length = length;
    return woken(commands_.tryPush(command));
}

bool Engine::send(DestinationId destination, std::span<const std::uint8_t> message)
{
    if (message.size() < 2 || message.size() > 3)
    {
        return false;
    }
    Command command{Command::Kind::Send};
    command.destination = destination;
    std::ranges::copy(message, command.note.begin());
    command.size = static_cast<std::uint8_t>(message.size());
    return woken(commands_.tryPush(command));
}

void Engine::endAudition(nanoseconds now) noexcept
{
    if (auditioned_)
    {
        sink_.send(auditioned_->destination, auditioned_->off, now);
        auditioned_.reset();
    }
}

std::optional<std::uint64_t> Engine::resetInputs()
{
    const auto request = inputResetRequests_ + 1;
    if (!woken(commands_.tryPush({Command::Kind::ResetInputs, request})))
    {
        return std::nullopt;
    }
    inputResetRequests_ = request;
    return request;
}

bool Engine::inputsReset(std::uint64_t request) const noexcept
{
    return request != 0 && inputsReset_.load(std::memory_order_acquire) >= request;
}

bool Engine::quit()
{
    return woken(commands_.tryPush({Command::Kind::Quit}));
}

EngineStatus Engine::status() const
{
    EngineStatus status;
    status.playing = playing_.load(std::memory_order_relaxed);
    status.finishedPlay = finishedPlay_.load(std::memory_order_relaxed);
    status.finished = status.finishedPlay != 0;
    status.countingIn = countingIn_.load(std::memory_order_relaxed);
    if (const Tick position = position_.load(std::memory_order_relaxed); position != kNoPosition)
    {
        status.position = position;
    }
    status.droppedNotes = droppedNotes_.load(std::memory_order_relaxed);
    status.lateness = lateness_.read();
    status.wakeLateness = wakeLateness_.read();
    status.sendTime = sendTime_.read();
    status.inputReceived = inputReceived_.load(std::memory_order_relaxed);
    status.inputDropped = inputDropped_.load(std::memory_order_relaxed);
    status.inputUnreported = inputUnreported_.load(std::memory_order_relaxed);
    status.thruDelay = thruDelay_.read();
    status.thruDroppedNotes = thruDroppedNotes_.load(std::memory_order_relaxed);
    return status;
}

bool Engine::receive(InputId input, std::span<const std::uint8_t> message,
                     nanoseconds time) noexcept
{
    assert(input.value < kMaxInputs);
    switch ((*inputs_)[input.value].push(message, time))
    {
    case InputQueue::Result::Pushed:
        inputQueued_.fetch_add(1, std::memory_order_release);
        clock_.wake();
        return true;
    case InputQueue::Result::Ignored:
        return true;
    case InputQueue::Result::Dropped:
        inputDropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool Engine::inputPending() const noexcept
{
    return inputQueued_.load(std::memory_order_acquire) !=
           inputReceived_.load(std::memory_order_acquire);
}

bool Engine::woken(bool taken) noexcept
{
    // Woken at once for what it's been given, rather than at its next look for commands, up
    // to 1 ms later (found in a code review, November 2026).
    if (taken)
    {
        clock_.wake();
    }
    return taken;
}

bool Engine::publish(Snapshot snapshot)
{
    auto shared = std::make_shared<const Snapshot>(std::move(snapshot));
    if (!snapshots_.publish(shared))
    {
        return false;
    }
    latest_ = std::move(shared);
    return true;
}

void Engine::run() noexcept
{
    while (!quit_)
    {
        const auto wake = step();
        if (!quit_)
        {
            clock_.sleepUntil(wake.time, wake.nextEvent);
        }
    }
}

EngineWake Engine::step() noexcept
{
    const auto now = clock_.now();
    // Capture a finite backlog: producers can only append behind it. Whole messages stay
    // together and ports take turns, so a flood cannot trap playback in the input loop.
    auto input = queuedInput();
    const auto* before = snapshots_.current();
    if (snapshots_.update())
    {
        // The replaced snapshot stays alive until the next update. Finish the captured
        // input against its old mapping before changing tempo or Thru targets.
        if (before != nullptr)
        {
            takeInput(input, before, kMaxInputs * InputQueue::kCapacity);
        }
        const auto& snapshot = *snapshots_.current();
        const auto loopChase = snapshot.playRequest == currentPlay_ && snapshot.loopChase
                                   ? std::span<const PlannedEvent>{*snapshot.loopChase}
                                   : std::span<const PlannedEvent>{};
        scheduler_.setPlan(*snapshot.plan, now, loopChase);
        thru_.setTargets(snapshot.plan->thru);
    }

    takeInput(input, snapshots_.current());

    // Commands are carried out in order; a Play whose snapshot hasn't arrived yet waits, and
    // so do the commands after it.
    while (!quit_)
    {
        auto command = pending_ ? std::exchange(pending_, std::nullopt) : commands_.tryPop();
        if (!command)
        {
            break;
        }
        // Finish input captured before this transport boundary while its play/mapping is
        // still current. This is bounded by queue capacities, not by ongoing input traffic.
        takeInput(input, snapshots_.current(), kMaxInputs * InputQueue::kCapacity);
        if (!carryOut(*command, now))
        {
            pending_ = command;
            break;
        }
    }

    const auto sendTime = clock_.now();
    scheduler_.sendDue(sendTime);
    if (auditioned_ && sendTime >= auditioned_->due)
    {
        endAudition(sendTime);
    }
    publishStatus(sendTime);

    auto next = scheduler_.nextWakeTime();
    if (auditioned_)
    {
        next = std::min(next.value_or(nanoseconds::max()), auditioned_->due);
    }
    return {std::min(sendTime + kMaxSleep, next.value_or(nanoseconds::max())), next};
}

bool Engine::carryOut(const Command& command, nanoseconds now) noexcept
{
    switch (command.kind)
    {
    case Command::Kind::Play:
    {
        const Snapshot* snapshot = snapshots_.current();
        if (snapshot == nullptr || snapshot->playRequest < command.request)
        {
            return false;
        }
        if (snapshot->playRequest == command.request) // otherwise a later Play replaced it
        {
            noteStop(now);
            const auto loopChase = snapshot->loopChase
                                       ? std::span<const PlannedEvent>{*snapshot->loopChase}
                                       : std::span<const PlannedEvent>{};
            scheduler_.play(snapshot->start, *snapshot->chased, now, snapshot->countIn.get(),
                            snapshot->loop, loopChase);
            currentPlay_ = command.request;
            // After the scheduler has let go of the play before's chased events and count-in.
            startedPlay_.store(command.request, std::memory_order_release);
        }
        return true;
    }
    case Command::Kind::Stop:
        noteStop(now);
        scheduler_.stop(now);
        currentPlay_ = 0;
        return true;
    case Command::Kind::Audition:
        endAudition(now);
        sink_.send(command.destination, command.note, now);
        auditioned_ = Auditioned{
            command.destination,
            {static_cast<std::uint8_t>(0x80 | (command.note[0] & 0x0F)), command.note[1], 0},
            now + command.length};
        return true;
    case Command::Kind::Send:
        sink_.send(command.destination, std::span{command.note.data(), command.size}, now);
        return true;
    case Command::Kind::Panic:
        endAudition(now);
        noteStop(now);
        thru_.endAll(now);
        scheduler_.panic(now);
        currentPlay_ = 0;
        return true;
    case Command::Kind::ResetInputs:
    {
        // Producers are closed. Capture any final callback after the step's initial snapshot
        // and finish its messages before releasing state and allowing IDs to be reused.
        auto input = queuedInput();
        takeInput(input, snapshots_.current(), kMaxInputs * InputQueue::kCapacity);
        thru_.endAll(clock_.now());
        inputsReset_.store(command.request, std::memory_order_release);
        return true;
    }
    case Command::Kind::Quit:
        endAudition(now);
        noteStop(now);
        scheduler_.stop(now);
        currentPlay_ = 0;
        thru_.endAll(now);
        quit_ = true;
        return true;
    }
    return true;
}

// Stamps input and passes it back, and sends channel messages thru. SysEx isn't sent thru,
// since WinMM blocks while sending it.
Engine::InputBatch Engine::queuedInput() const noexcept
{
    InputBatch result{};
    for (std::size_t i = 0; i < kMaxInputs; ++i)
        result[i] = (*inputs_)[i].available();
    return result;
}

void Engine::takeInput(InputBatch& queued, const Snapshot* snapshot, std::size_t most) noexcept
{
    thru_.setEnabled(thruOn_.load(std::memory_order_relaxed));
    std::size_t received = 0;
    std::size_t unreported = 0;
    std::size_t empty = 0;
    while (received < most && empty < kMaxInputs)
    {
        const auto i = nextInput_;
        nextInput_ = (nextInput_ + 1) % kMaxInputs;
        if (queued[i] == 0)
        {
            ++empty;
            continue;
        }
        empty = 0;
        for (std::size_t message = 0; message < kInputQuantum && received < most && queued[i] > 0;
             ++message)
        {
            // SysEx is pushed atomically and finished before yielding to another port or
            // transport command, so its framing and recording stamp cannot be split.
            bool first = true;
            bool dropping = false;
            bool more = false;
            do
            {
                const auto chunk = (*inputs_)[i].pop();
                assert(chunk && queued[i] > 0);
                --queued[i];
                more = chunk->more;
                if (!dropping)
                {
                    StampedInput stamped{InputId{static_cast<std::uint32_t>(i)}, *chunk, first,
                                         currentPlay_};
                    if (currentPlay_ != 0)
                    {
                        assert(snapshot != nullptr);
                        stamped.tempo = snapshot->inputTempo;
                        stamped.tick = scheduler_.tickAt(chunk->time).value_or(0);
                        stamped.songTime =
                            scheduler_.songTimeAt(chunk->time).value_or(nanoseconds{0});
                    }
                    if (!stamped_->tryPush(std::move(stamped)))
                    {
                        ++unreported;
                        dropping = more;
                    }
                }
                const std::span<const std::uint8_t> bytes{chunk->bytes.data(), chunk->size};
                if (first && !more && bytes[0] != 0xF0)
                    thru_.receive(InputId{static_cast<std::uint32_t>(i)}, bytes, chunk->time);
                first = false;
            } while (more);
            ++received;
        }
    }
    if (unreported > 0)
        inputUnreported_.store(inputUnreported_.load(std::memory_order_relaxed) + unreported,
                               std::memory_order_relaxed);
    thruDroppedNotes_.store(thru_.droppedNotes(), std::memory_order_relaxed);
    if (received > 0)
        inputReceived_.store(inputReceived_.load(std::memory_order_relaxed) + received,
                             std::memory_order_release);
}

void Engine::noteStop(nanoseconds now) noexcept
{
    if (currentPlay_ == 0)
    {
        return;
    }
    stopTick_.store(scheduler_.tickAt(now).value_or(0), std::memory_order_relaxed);
    stoppedPlay_.store(currentPlay_, std::memory_order_release);
}

std::optional<Tick> Engine::stoppedAt(std::uint64_t play) const noexcept
{
    if (play == 0 || stoppedPlay_.load(std::memory_order_acquire) != play)
    {
        return std::nullopt;
    }
    return stopTick_.load(std::memory_order_relaxed);
}

void Engine::publishStatus(nanoseconds now) noexcept
{
    playing_.store(scheduler_.isPlaying(), std::memory_order_relaxed);
    finishedPlay_.store(scheduler_.finished() ? currentPlay_ : 0, std::memory_order_relaxed);
    countingIn_.store(scheduler_.countingIn(now), std::memory_order_relaxed);
    position_.store(scheduler_.position(now).value_or(kNoPosition), std::memory_order_relaxed);
    droppedNotes_.store(scheduler_.droppedNotes(), std::memory_order_relaxed);
}

void Engine::MeasuringSink::send(DestinationId destination, std::span<const std::uint8_t> bytes,
                                 nanoseconds due) noexcept
{
    const auto start = engine_.clock_.now();
    engine_.sink_.send(destination, bytes, due);
    const auto end = engine_.clock_.now();
    engine_.lateness_.record(end - due);
    engine_.wakeLateness_.record(start - due);
    engine_.sendTime_.record(end - start);
}

void Engine::ThruSink::send(DestinationId destination, std::span<const std::uint8_t> bytes,
                            nanoseconds arrival) noexcept
{
    engine_.sink_.send(destination, bytes, arrival);
    engine_.thruDelay_.record(engine_.clock_.now() - arrival);
}

} // namespace modal::core
