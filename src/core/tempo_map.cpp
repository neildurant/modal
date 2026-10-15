#include <modal/core/tempo_map.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::int64_t kNanosPerMicro = 1000;
constexpr double kMicrosPerMinute = 60'000'000.0;

constexpr std::int64_t ceilDiv(std::int64_t a, std::int64_t b)
{
    return (a + b - 1) / b;
}

// Time from the start of a segment to a tick within it, rounded up to a whole nanosecond.
// Splitting into whole quarter notes and remaining ticks keeps every intermediate value far
// from overflow without 128-bit arithmetic.
nanoseconds elapsedTime(Tick ticks, Tempo tempo, Ppqn ppqn)
{
    const std::int64_t nanosPerQuarter = tempo.microsPerQuarter * kNanosPerMicro;
    const std::int64_t quarters = ticks / ppqn.value;
    const std::int64_t remainder = ticks % ppqn.value;
    return nanoseconds{quarters * nanosPerQuarter +
                       ceilDiv(remainder * nanosPerQuarter, ppqn.value)};
}

// The last tick at or before a time from the start of a segment.
Tick elapsedTicks(nanoseconds time, Tempo tempo, Ppqn ppqn)
{
    const std::int64_t nanosPerQuarter = tempo.microsPerQuarter * kNanosPerMicro;
    const std::int64_t quarters = time.count() / nanosPerQuarter;
    const std::int64_t remainder = time.count() % nanosPerQuarter;
    return quarters * ppqn.value + remainder * ppqn.value / nanosPerQuarter;
}

} // namespace

Tempo Tempo::fromBpm(double bpm)
{
    const auto micros = std::llround(kMicrosPerMinute / bpm);
    return Tempo{std::clamp<std::int64_t>(micros, kMinMicrosPerQuarter, kMaxMicrosPerQuarter)};
}

double Tempo::bpm() const
{
    return kMicrosPerMinute / static_cast<double>(microsPerQuarter);
}

TempoMap::TempoMap(Ppqn ppqn, Tempo initial)
    : ppqn_(ppqn)
    , changes_{{0, initial}}
    , startTimes_{nanoseconds{0}}
{
    assert(ppqn.value > 0);
    assert(initial.isValid());
}

void TempoMap::set(Tick tick, Tempo tempo)
{
    assert(tick >= 0);
    assert(tempo.isValid());

    const auto it = std::ranges::lower_bound(changes_, tick, {}, &TempoChange::tick);
    const auto index = static_cast<std::size_t>(std::distance(changes_.begin(), it));
    if (it != changes_.end() && it->tick == tick)
    {
        it->tempo = tempo;
    }
    else
    {
        changes_.insert(it, {tick, tempo});
        startTimes_.insert(startTimes_.begin() + static_cast<std::ptrdiff_t>(index), nanoseconds{});
    }
    updateStartTimes(index);
}

bool TempoMap::remove(Tick tick)
{
    if (tick == 0)
    {
        return false;
    }
    const auto it = std::ranges::lower_bound(changes_, tick, {}, &TempoChange::tick);
    if (it == changes_.end() || it->tick != tick)
    {
        return false;
    }
    const auto index = static_cast<std::size_t>(std::distance(changes_.begin(), it));
    changes_.erase(it);
    startTimes_.erase(startTimes_.begin() + static_cast<std::ptrdiff_t>(index));
    updateStartTimes(index);
    return true;
}

Tempo TempoMap::tempoAt(Tick tick) const
{
    return changes_[segmentAt(tick)].tempo;
}

nanoseconds TempoMap::toTime(Tick tick) const
{
    assert(tick >= 0);
    const auto i = segmentAt(tick);
    return startTimes_[i] + elapsedTime(tick - changes_[i].tick, changes_[i].tempo, ppqn_);
}

Tick TempoMap::toTick(nanoseconds time) const
{
    assert(time >= nanoseconds{0});
    const auto i = segmentAt(time);
    return changes_[i].tick + elapsedTicks(time - startTimes_[i], changes_[i].tempo, ppqn_);
}

std::size_t TempoMap::segmentAt(Tick tick) const
{
    // The last change at or before the tick; changes_[0] is at tick 0.
    const auto it = std::ranges::upper_bound(changes_, tick, {}, &TempoChange::tick);
    return static_cast<std::size_t>(std::distance(changes_.begin(), it)) - 1;
}

std::size_t TempoMap::segmentAt(nanoseconds time) const
{
    const auto it = std::ranges::upper_bound(startTimes_, time);
    return static_cast<std::size_t>(std::distance(startTimes_.begin(), it)) - 1;
}

void TempoMap::updateStartTimes(std::size_t from)
{
    for (auto i = std::max<std::size_t>(from, 1); i < changes_.size(); ++i)
    {
        const auto& previous = changes_[i - 1];
        startTimes_[i] = startTimes_[i - 1] +
                         elapsedTime(changes_[i].tick - previous.tick, previous.tempo, ppqn_);
    }
}

} // namespace modal::core
