// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/inspector.hpp>

#include <cstddef>

namespace modal::ui
{

// What every view draws above itself (decided in milestone 4): its own bar, a title for it,
// then the inspector.

// The inspector's height: two lines, with a quarter of a line's padding above and below.
int inspectorHeight(CellSize cell);

// The inspector, on a panel of its own: the kind of thing, then headings above their values.
// Each column is as wide as its longest value can be, so the columns stay put as the cursor
// moves, a position or end as wide as positionWidth; a value that doesn't fit is cut short,
// and those after it left out.
Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth);

// The bar naming a track, its channel and port (track 0 is the conductor), and the section
// holding a time with its bars in a meter, each value after its label.
Region trackBarRegion(const core::Song& song, std::size_t track, core::Tick tick,
                      const core::MeterMap& meter, Rect bounds, int width, CellSize cell);

} // namespace modal::ui
