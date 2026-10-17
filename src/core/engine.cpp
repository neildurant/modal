#include <modal/core/engine.hpp>

#include <algorithm>
#include <cassert>
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
    next.plan = std::move(plan);
    return publish(std::move(next));
}

bool Engine::play(Tick start, std::optional<PlannedCountIn> countIn)
{
    if (!latest_)
    {
        return true;
    }
    if (commands_.full())
    {
        return false;
    }
    Snapshot next = *latest_;
    next.playRequest = playRequests_ + 1;
    next.start = start;
    next.chased = std::make_shared<const std::vector<PlannedEvent>>(chase(*next.plan, start));
    next.countIn = countIn ? std::make_shared<const PlannedCountIn>(std::move(*countIn)) : nullptr;
    if (!publish(std::move(next)))
    {
        return false;
    }
    ++playRequests_;
    return commands_.tryPush({Command::Kind::Play, playRequests_});
}

bool Engine::stop()
{
    return commands_.tryPush({Command::Kind::Stop});
}

bool Engine::panic()
{
    return commands_.tryPush({Command::Kind::Panic});
}

bool Engine::quit()
{
    return commands_.tryPush({Command::Kind::Quit});
}

EngineStatus Engine::status() const
{
    EngineStatus status;
    status.playing = playing_.load(std::memory_order_relaxed);
    status.finished = finished_.load(std::memory_order_relaxed);
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
    if (snapshots_.update())
    {
        const auto& plan = *snapshots_.current()->plan;
        scheduler_.setPlan(plan, now);
        thru_.setTargets(plan.thru);
    }

    // Commands are carried out in order; a Play whose snapshot hasn't arrived yet waits, and
    // so do the commands after it.
    while (!quit_)
    {
        auto command = pending_ ? std::exchange(pending_, std::nullopt) : commands_.tryPop();
        if (!command)
        {
            break;
        }
        if (!carryOut(*command, now))
        {
            pending_ = command;
            break;
        }
    }

    takeInput();

    const auto sendTime = clock_.now();
    scheduler_.sendDue(sendTime);
    publishStatus(sendTime);

    const auto next = scheduler_.nextWakeTime();
    return {std::min(sendTime + kMaxSleep, next.value_or(nanoseconds::max())), next};
}

bool Engine::carryOut(const Command& command, nanoseconds now) noexcept
{
    switch (command.kind)
    {
    case Command::Kind::Play:
    {
        const Snapshot* snapshot = snapshots_.current();
        if (snapshot == nullptr || snapshot->playRequest < command.playRequest)
        {
            return false;
        }
        if (snapshot->playRequest == command.playRequest) // otherwise a later Play replaced it
        {
            scheduler_.play(snapshot->start, *snapshot->chased, now, snapshot->countIn.get());
            currentPlay_ = command.playRequest;
        }
        return true;
    }
    case Command::Kind::Stop:
        scheduler_.stop(now);
        currentPlay_ = 0;
        return true;
    case Command::Kind::Panic:
        thru_.endAll(now);
        scheduler_.panic(now);
        currentPlay_ = 0;
        return true;
    case Command::Kind::Quit:
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
void Engine::takeInput() noexcept
{
    const bool thruOn = thruOn_.load(std::memory_order_relaxed);
    std::size_t received = 0;
    std::size_t unreported = 0;
    for (std::uint32_t i = 0; i < kMaxInputs; ++i)
    {
        // Pushes are all or nothing, so a SysEx's chunks come together.
        bool continuing = false;
        bool dropping = false; // the rest of a SysEx that couldn't all be passed back
        while (const auto chunk = (*inputs_)[i].pop())
        {
            const bool first = !continuing;
            continuing = chunk->more;
            if (first)
            {
                dropping = false;
            }

            if (!dropping)
            {
                StampedInput stamped{InputId{i}, *chunk, first, currentPlay_};
                if (currentPlay_ != 0)
                {
                    stamped.tick = scheduler_.tickAt(chunk->time).value_or(0);
                    stamped.songTime = scheduler_.songTimeAt(chunk->time).value_or(nanoseconds{0});
                }
                if (!stamped_->tryPush(std::move(stamped)))
                {
                    ++unreported;
                    dropping = chunk->more;
                }
            }

            if (chunk->more)
            {
                continue;
            }
            ++received;
            const std::span<const std::uint8_t> bytes{chunk->bytes.data(), chunk->size};
            if (thruOn && first && bytes[0] != 0xF0)
            {
                thru_.receive(InputId{i}, bytes, chunk->time);
            }
        }
    }
    if (unreported > 0)
    {
        inputUnreported_.store(inputUnreported_.load(std::memory_order_relaxed) + unreported,
                               std::memory_order_relaxed);
    }
    thruDroppedNotes_.store(thru_.droppedNotes(), std::memory_order_relaxed);
    if (received > 0)
    {
        inputReceived_.store(inputReceived_.load(std::memory_order_relaxed) + received,
                             std::memory_order_relaxed);
    }
}

void Engine::publishStatus(nanoseconds now) noexcept
{
    playing_.store(scheduler_.isPlaying(), std::memory_order_relaxed);
    finished_.store(scheduler_.finished(), std::memory_order_relaxed);
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
