#include <modal/core/engine.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

// Single writer, so a plain load and store does instead of a read-modify-write.
template <typename T>
void add(std::atomic<T>& counter, T amount)
{
    counter.store(counter.load(std::memory_order_relaxed) + amount, std::memory_order_relaxed);
}

} // namespace

std::optional<nanoseconds> LatenessStats::within(double fraction) const
{
    if (count == 0)
    {
        return std::nullopt;
    }
    const auto needed = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(count)));
    std::size_t sofar = 0;
    for (std::size_t i = 0; i < kLimits.size(); ++i)
    {
        sofar += buckets[i];
        if (sofar >= needed)
        {
            return kLimits[i];
        }
    }
    return std::nullopt;
}

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
    status.lateness.count = sent_.load(std::memory_order_relaxed);
    status.lateness.max = nanoseconds{maxLateness_.load(std::memory_order_relaxed)};
    for (std::size_t i = 0; i < buckets_.size(); ++i)
    {
        status.lateness.buckets[i] = buckets_[i].load(std::memory_order_relaxed);
    }
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
    engine_.sink_.send(destination, bytes, due);
    const auto lateness = std::max(engine_.clock_.now() - due, nanoseconds{0});

    const auto& limits = LatenessStats::kLimits;
    const auto bucket =
        static_cast<std::size_t>(std::ranges::upper_bound(limits, lateness) - limits.begin());
    add(engine_.buckets_[bucket], std::size_t{1});
    add(engine_.sent_, std::size_t{1});
    if (lateness.count() > engine_.maxLateness_.load(std::memory_order_relaxed))
    {
        engine_.maxLateness_.store(lateness.count(), std::memory_order_relaxed);
    }
}

} // namespace modal::core
