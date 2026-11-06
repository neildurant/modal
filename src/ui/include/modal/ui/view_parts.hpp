// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

// Of places in order, the nth after here, or before it, going round past the ends if asked
// (as a search does, and a find doesn't); and whether it went round. Nothing if there are
// none, or not as many without going round.
template <typename Place>
std::optional<std::pair<Place, bool>> nthPlace(const std::vector<Place>& places, const Place& here,
                                               bool forward, int n, bool wrap)
{
    if (places.empty())
    {
        return std::nullopt;
    }
    const auto size = static_cast<std::ptrdiff_t>(places.size());
    auto at = forward ? std::ranges::upper_bound(places, here) - places.begin() + n - 1
                      : std::ranges::lower_bound(places, here) - places.begin() - n;
    const bool wrapped = at < 0 || at >= size;
    if (wrapped && !wrap)
    {
        return std::nullopt;
    }
    at = ((at % size) + size) % size;
    return std::pair{places[static_cast<std::size_t>(at)], wrapped};
}

// Where the cursor is among matches, for the status line: "match 3/24" on one, or "24
// matches".
template <typename Place>
std::string matchCount(const std::vector<Place>& places, const Place& here)
{
    if (places.empty())
    {
        return "no matches";
    }
    if (const auto at = std::ranges::find(places, here); at != places.end())
    {
        return "match " + std::to_string(at - places.begin() + 1) + "/" +
               std::to_string(places.size());
    }
    return places.size() == 1 ? std::string{"1 match"} : std::to_string(places.size()) + " matches";
}

// An event as what to search for (* and #): a note's exact pitch, a controller, or a bend.
std::optional<core::EventQuery> queryFor(const core::EventData& data);

// What every view draws above itself (decided in milestone 4): its own bar, a title for it,
// then the inspector.

// The inspector's height: two lines, with a quarter of a line's padding above and below.
int inspectorHeight(CellSize cell);

// The inspector, on a panel of its own: the kind of thing, then headings above their values.
// Each column is as wide as its longest value can be, so the columns stay put as the cursor
// moves, a position or end as wide as positionWidth; a value that doesn't fit is cut short,
// and those after it left out. Values e can edit are bright, the others dim.
// Notes have the kind above pitch and velocities on the left, and to the right a row each for
// Start, Length and Ends, across their coarse, fine and exact values. With an editor, its field
// is boxed.
Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth, const InspectorEditor* editor = nullptr);

// The bar naming a track, its channel and port (track 0 is the conductor), and the section
// holding a time with its bars in a meter, each value after its label.
Region trackBarRegion(const core::Song& song, std::size_t track, core::Tick tick,
                      const core::MeterMap& meter, Rect bounds, int width, CellSize cell);

// A character drawn at a time in a row, as a mark's letter (ma) or the play-from marker.
struct RowSign
{
    int x = 0;
    int y = 0;
    char32_t character = 0;
    Role role = Role::Text;
    std::u32string label = {}; // after it, if there's room before the next sign
    bool moves = true;         // false for the play-from marker, whose place matters
};

// Signs, each in a box of the background, so a line through the row doesn't run through it.
// Those in a row that would overlap go one after another from the leftmost, so marks a beat
// apart read "ab" from the first's place; at the same place, in the order given. One that
// doesn't move stays at its place, the next going after it.
void addRowSigns(Region& region, std::vector<RowSign> signs, CellSize cell);

// A mark's dot, at its time in a row of bars or notes, its letter being in the ruler's row of
// marks: a small square in the text's colour, ringed by a pixel of the background so it shows
// over bars and notes of any shade (decided November 2026, after a letter boxed there blotted
// out the bar under it). Its ring starts at x, so from where letters start, a pixel clear of
// the line at the time.
void addMarkDot(Region& region, int x, int y, CellSize cell);

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

// The sections is and as take: the one holding a time and as many after it as the count says
// less one, as far as the last; their span of time, and their names as words ("Verse", "3
// sections from Verse").
struct SectionRange
{
    std::size_t first = 0;
    std::size_t count = 1;
    core::Tick from = 0;
    core::Tick to = 0;
};
SectionRange sectionsFrom(const core::Song& song, core::Tick tick, int count);
std::string sectionsText(const core::Song& song, const SectionRange& range);

// What as takes after an operator, for the hint menu: the section itself for d and y, and
// nothing for the others, to which it's is.
std::optional<std::string> sectionDescription(std::string_view op);

// Whether a motion is is or as.
inline bool isSectionObject(std::string_view motion)
{
    return motion == "object-section-contents" || motion == "object-section";
}

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

// What the ranges to boundaries in time take, the same in each view, for the hint menu: "to
// the next bar line", "from the section's start", "to the end of the song".
std::optional<std::string> describeTimeRange(std::string_view motion);

// How many events, as words: "1 event", "3 events".
std::string eventCount(std::size_t events);

// Where a register put at a time goes, for the message after a put (decided November 2026):
// "bar 3" for one taken as bars, else its position, "3:2:120".
std::string putPlace(const core::MeterMap& meter, core::Tick at, bool bars);
// Where a register starts to end at a time (put-ending), or nothing if that's before the
// song's start.
std::optional<core::Tick> startToEnd(const EventRegister& reg, core::Tick end);

} // namespace modal::ui
