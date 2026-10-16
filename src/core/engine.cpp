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
    : sink_(sink)
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

bool Engine::play(Tick start)
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
    return status;
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
            clock_.sleepUntil(wake);
        }
    }
}

nanoseconds Engine::step() noexcept
{
    const auto now = clock_.now();
    if (snapshots_.update())
    {
        scheduler_.setPlan(*snapshots_.current()->plan, now);
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

    const auto sendTime = clock_.now();
    scheduler_.sendDue(sendTime);
    publishStatus(sendTime);

    auto wake = sendTime + kMaxSleep;
    if (const auto next = scheduler_.nextWakeTime())
    {
        wake = std::min(wake, *next);
    }
    return wake;
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
            scheduler_.play(snapshot->start, *snapshot->chased, now);
        }
        return true;
    }
    case Command::Kind::Stop:
        scheduler_.stop(now);
        return true;
    case Command::Kind::Panic:
        scheduler_.panic(now);
        return true;
    case Command::Kind::Quit:
        scheduler_.stop(now);
        quit_ = true;
        return true;
    }
    return true;
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

} // namespace modal::core
