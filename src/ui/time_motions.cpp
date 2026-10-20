#include <modal/ui/time_motions.hpp>

#include <algorithm>
#include <iterator>

namespace modal::ui
{

core::Tick beatAfter(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count; ++i)
    {
        tick = meter.beatAtOrAfter(tick + 1);
    }
    return tick;
}

core::Tick beatBefore(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count && tick > 0; ++i)
    {
        const auto position = meter.toBarBeatTick(tick - 1);
        tick = *meter.toTick({position.bar, position.beat, 0});
    }
    return tick;
}

core::Tick barLineAfter(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count; ++i)
    {
        tick = meter.barStart(meter.toBarBeatTick(tick).bar + 1);
    }
    return tick;
}

core::Tick barLineBefore(const core::MeterMap& meter, core::Tick tick, std::int64_t count)
{
    for (std::int64_t i = 0; i < count && tick > 0; ++i)
    {
        tick = meter.barStart(meter.toBarBeatTick(tick - 1).bar);
    }
    return tick;
}

core::Tick offsetInBar(const core::MeterMap& meter, core::Tick tick)
{
    return tick - meter.barStart(meter.toBarBeatTick(tick).bar);
}

core::Tick sameInBar(const core::MeterMap& meter, core::Tick tick, std::int64_t count,
                     core::Tick offset)
{
    const auto bar = std::max<std::int64_t>(meter.toBarBeatTick(tick).bar + count, 1);
    const core::Tick start = meter.barStart(bar);
    return start + std::min(offset, meter.barStart(bar + 1) - start - 1);
}

std::optional<core::Tick> nextOf(std::span<const core::Tick> ticks, core::Tick tick,
                                 std::int64_t count)
{
    const auto it = std::ranges::upper_bound(ticks, tick);
    const auto at = std::distance(ticks.begin(), it) + count - 1;
    if (at >= static_cast<std::ptrdiff_t>(ticks.size()))
    {
        return std::nullopt;
    }
    return ticks[static_cast<std::size_t>(at)];
}

std::optional<core::Tick> previousOf(std::span<const core::Tick> ticks, core::Tick tick,
                                     std::int64_t count)
{
    const auto it = std::ranges::lower_bound(ticks, tick);
    const auto at = std::distance(ticks.begin(), it) - count;
    if (at < 0)
    {
        return std::nullopt;
    }
    return ticks[static_cast<std::size_t>(at)];
}

bool together(const core::Song& song, core::Tick first, core::Tick tick,
              std::chrono::milliseconds window)
{
    const auto& tempo = song.tempo();
    return tempo.toTime(tick) - tempo.toTime(first) <= window;
}

} // namespace modal::ui
