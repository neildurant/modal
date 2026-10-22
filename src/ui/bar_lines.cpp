// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar_lines.hpp>

#include <algorithm>

namespace modal::ui
{

int TimeScale::toX(core::Tick tick) const
{
    const core::Tick scaled = (tick - start) * width;
    const core::Tick ticks = std::max<core::Tick>(length, 1);
    // Rounded down, also before the start.
    const core::Tick pixel = scaled >= 0 ? scaled / ticks : -((-scaled + ticks - 1) / ticks);
    return x + static_cast<int>(pixel);
}

std::vector<Shape> meterLines(const core::MeterMap& meters, const TimeScale& scale, int top,
                              int height, core::NoteFraction grid, CellSize cell)
{
    std::vector<Shape> lines;
    if (scale.width <= 0 || scale.length <= 0)
    {
        return lines;
    }
    const core::Tick end = scale.start + scale.length;
    const core::Tick gridTicks = std::max<core::Tick>(grid.toTicks(meters.ppqn()), 1);
    // Whether lines a length apart would be far enough apart to draw.
    const auto spaced = [&](core::Tick ticks)
    { return ticks * scale.width >= minLineSpacing(cell) * scale.length; };
    const bool drawGrid = spaced(gridTicks);

    const auto line = [&](core::Tick tick, Role role, int width)
    { lines.push_back({{scale.toX(tick), top, width, height}, role, 255}); };

    const core::Tick first = std::max<core::Tick>(scale.start, 0);
    for (auto bar = meters.toBarBeatTick(first).bar;; ++bar)
    {
        const core::Tick barStart = meters.barStart(bar);
        if (barStart >= end)
        {
            break;
        }
        const core::Tick barEnd = std::min(meters.barStart(bar + 1), end);
        const core::Tick beatTicks = std::max<core::Tick>(
            meters.signatureAt(barStart).beatLength().toTicks(meters.ppqn()), 1);
        const bool drawBeats = spaced(beatTicks);

        if (barStart >= scale.start)
        {
            line(barStart, Role::BarLine, hairline(cell));
        }
        // The beats and grid steps after the bar line, in order, each drawn once.
        core::Tick nextBeat = barStart + beatTicks;
        core::Tick nextStep = barStart + gridTicks;
        for (;;)
        {
            const core::Tick beat = drawBeats ? nextBeat : barEnd;
            const core::Tick step = drawGrid ? nextStep : barEnd;
            const core::Tick tick = std::min(beat, step);
            if (tick >= barEnd)
            {
                break;
            }
            if (tick >= scale.start)
            {
                line(tick, tick == beat ? Role::BeatLine : Role::GridLine, 1);
            }
            if (tick == beat)
            {
                nextBeat += beatTicks;
            }
            if (tick == step)
            {
                nextStep += gridTicks;
            }
        }
    }
    return lines;
}

} // namespace modal::ui
