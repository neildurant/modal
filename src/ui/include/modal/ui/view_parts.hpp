// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/view.hpp>

#include <cstddef>
#include <string>

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

// Puts the register's events into the song at a time, its first track's onto a track (counting
// from 0) and the others onto those after it, as far as there are tracks, merging with what's
// there (see core::merged). Returns how many it put, and onto how many tracks.
struct Put
{
    std::size_t events = 0;
    std::size_t tracks = 0;
};
Put putRegister(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at);

// How many events, as words: "1 event", "3 events".
std::string eventCount(std::size_t events);

} // namespace modal::ui
