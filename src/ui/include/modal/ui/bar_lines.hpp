#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>

#include <vector>

namespace modal::ui
{

// A stretch of the timeline laid across some pixels at one scale, so the same length of time
// is the same width wherever it falls, whatever the meter: a bar of 7/8 is 7/8 as wide as a
// bar of 4/4.
struct TimeScale
{
    core::Tick start = 0;
    core::Tick length = 1; // the ticks across the width, from start
    int x = 0;
    int width = 0;

    // The pixel a tick falls in, counting from x, which may be outside the width.
    int toX(core::Tick tick) const;
};

// Beat and grid lines closer together than this, in pixels, are left out: four pixels, or
// more in larger cells.
inline int minLineSpacing(CellSize cell)
{
    return std::max(cell.width * 2 / 5, 4);
}

// The lines of a meter map across a time scale, as shapes from a top for a height in pixels:
// a bar line at the start of each bar, a beat line at each beat and a grid line at each grid
// step, counting from each bar line. Where lines meet, only the most important is drawn.
// Bar lines are a hairline wide in the cell size given (see hairline), the others a pixel.
std::vector<Shape> meterLines(const core::MeterMap& meters, const TimeScale& scale, int top,
                              int height, core::NoteFraction grid, CellSize cell);

} // namespace modal::ui
