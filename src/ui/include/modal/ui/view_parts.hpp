// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <cstddef>
#include <cstdint>
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
// and those after it left out. Fields set aside (the time) go at the right, their values dim.
// With an editor, the field it's on is boxed, showing what's typed and the cursor.
Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth, const InspectorEditor* editor = nullptr);

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

// Ripple, staying inside the section (see How far ripple reaches in the design). The end of
// the section holding a time.
core::Tick sectionEndAt(const core::Song& song, core::Tick tick);

// Removes the events starting in a span of time on tracks (counting from 0, first to last),
// stopping at the section's end, and moves those after it in the section earlier by its
// length, leaving silence at the section's end. Returns what it removed, as a register holds
// it, with the span's length.
EventRegister rippleDelete(core::Song& song, std::size_t first, std::size_t last, core::Tick from,
                           core::Tick to);

// The time a register's events take: its span, or without one, up to the last one's end.
core::Tick registerSpan(const EventRegister& reg);

// How many whole bars the section at a time must grow by to insert the register there without
// pushing an event on the tracks it goes onto past the section's end; 0 if it fits.
std::int64_t barsToInsert(const core::Song& song, const EventRegister& reg, std::size_t firstTrack,
                          core::Tick at);

// Inserts the register at a time: moves the events from there to the section's end later by
// its span, on the tracks it goes onto, then puts it there as putRegister does.
Put rippleInsert(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at);

// The events starting in a span of time on tracks (counting from 0, first to last), as a
// register holds them, their ticks from its start, with its length.
EventRegister takeEvents(const core::Song& song, std::size_t first, std::size_t last,
                         core::Tick from, core::Tick to);

// The bars a span of time covers, as words in a meter: "bar 3", "bars 3–5".
std::string barsText(const core::MeterMap& meter, core::Tick from, core::Tick to);

// How many events, as words: "1 event", "3 events".
std::string eventCount(std::size_t events);

} // namespace modal::ui
