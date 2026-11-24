// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/view.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// The background of a field free text is typed into, columns wide at x, y: inset by a hairline
// above and below so fields in rows one under another show apart. Text over it keeps the
// view's background, which shapes are painted over.
void addField(Region& region, int x, int y, int columns, CellSize cell);

// A track's button, three cells wide at x, y, the same in every view showing it: filled in its
// colour when on, its letter grey when off; Thru on a track not armed is its letter in its
// colour, unfilled, as it does nothing until armed. Boxed when the cursor's on it. While
// recording, an armed track's Arm button says R, as it's being recorded, rather than A, which
// says what the key does (decided 13 November 2026).
void addTrackButton(Region& region, int x, int y, CellSize cell, TrackField field,
                    const core::Track& track, Role background, bool cursor, bool recording = false);
inline constexpr int kTrackButtonColumns = 3;

// The inspector, on a panel of its own: the kind of thing, then headings above their values.
// Each column is as wide as its longest value can be, so the columns stay put as the cursor
// moves, a position or end as wide as positionWidth; a value that doesn't fit is cut short,
// and those after it left out. Every value is dim but while editing, when those that can be
// edited are bright (decided 17 November 2026, after use, the same in every view). An
// inspector edited as a panel (see InspectorEditor) has its Keep and Cancel buttons at its
// bottom right while it's being edited, the fields always kept clear of them.
// Notes have the kind above pitch and velocities on the left, and to the right a row each for
// Start, Length and Ends, across their coarse, fine and exact values. With an editor, its field
// is boxed.
// A value in a box of some columns: what's being typed if the editor is on its field, with the
// cursor, or else the value itself, cut short if need be. A field with choices shows an arrow at
// its right, and while its list is open, where the field is goes in listAt, for the list to drop
// from (see listRegion).
void inspectorValue(Region& region, const InspectorField& field, std::u32string text, int x, int y,
                    int columns, CellSize cell, const InspectorEditor* editor,
                    bool rightAligned = false, Rect* listAt = nullptr);

Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth, const InspectorEditor* editor = nullptr,
                       Rect* listAt = nullptr, bool panel = false);

// How many columns a panel's Keep and Cancel buttons take, and where their row is.
inline constexpr int kPanelButtonColumns = 20; // "[ Keep ]  [ Cancel ]"
int panelButtonsY(Rect bounds, CellSize cell);

// The editor's drop-down list, if it has one open, from the field at listAt, kept within a
// space; to go in the frame after what it covers.
void addChoiceList(Frame& frame, const InspectorEditor* editor, Rect listAt, Rect within,
                   CellSize cell);

// Text padded with spaces to a number of characters, on its left (aligning it right) or its
// right; text already as long is left as it is.
std::string padText(std::string text, std::size_t width, bool alignRight);

// A time's place in a meter: its position, the time from the song's start and the meter
// there, each padded to the widest the song needs, so what follows stays put as it changes.
struct PlaceParts
{
    std::string position;
    std::string time;
    std::string meter;
};
PlaceParts placeParts(const core::Song& song, const core::MeterMap& meter, core::Tick tick);

// What comes before recording, as the song's settings have it: "Count-in 1 bar", "Count-in 3
// beats", "Pre-roll 2 bars", or "No count-in".
std::string countInText(const core::CountInSettings& countIn);

// When the metronome plays: "off", "while recording" or "always".
std::string whenText(core::MetronomeSettings::When when);

// The tempo at a time, as `120 BPM`, its number padded to the song's widest tempo.
std::string tempoText(const core::Song& song, core::Tick tick);

// The name of the section holding a time, padded to the song's longest name, which is cut
// short at 16 characters, or fewer where there's less room.
std::string sectionText(const core::Song& song, core::Tick tick, std::size_t most = 16);

// A value in a view's bar after its label, dim; or a label alone. Where the bar is too narrow
// for everything, those with the highest drop are left out first, and those at 0 never.
struct BarPair
{
    std::string label;
    std::string value;
    int drop = 0;
};

// What may be left out of a view's bar, from the first to go to the last: a track's
// quantise, program and bank, which come and go, quantise the longest; the section's bars; the
// piano roll's grid; and the section.
inline constexpr int kDropQuantise = 6;
inline constexpr int kDropProgram = 5;
inline constexpr int kDropBank = 4;
inline constexpr int kDropBars = 3;
inline constexpr int kDropGrid = 2;
inline constexpr int kDropSection = 1;

// A view's bar of labelled values, on the left and the right, leaving out what it must.
Region labelledBar(const std::vector<BarPair>& left, const std::vector<BarPair>& right, Rect bounds,
                   int width, CellSize cell);

// A track's number and name, channel and port (track 0 being the conductor), the names padded
// to the song's longest so what follows them stays put from track to track; then its
// playback quantise, program and bank where it has them.
std::vector<BarPair> trackPairs(const core::Song& song, std::size_t track);

// Where the cursor is: its position, time and meter, fixed in width; for a track, the meter
// followed by "own" where the track has a meter of its own, which the song's changes don't
// reach (decided 13 November 2026).
std::vector<BarPair> cursorPlacePairs(const core::Song& song, const core::MeterMap& meter,
                                      core::Tick tick,
                                      std::optional<core::TrackId> track = std::nullopt);

// The bar naming a track; and, in a view with a timeline, where the cursor is at the right:
// its section and the section's bars, then its place.
Region trackBarRegion(const core::Song& song, std::size_t track, std::optional<core::Tick> tick,
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
    // Drawn in place of the character, as a meter is, after the other signs at its place, as
    // it's for a whole bar: whole if there's room before the next sign, or else a tick.
    std::u32string text = {};
};

// Signs, each in a box of the background, so a line through the row doesn't run through it.
// Those in a row that would overlap go one after another from the leftmost, so marks a beat
// apart read "ab" from the first's place; at the same place, in the order given, a text last.
// One that doesn't move stays at its place, the next going after it.
void addRowSigns(Region& region, std::vector<RowSign> signs, CellSize cell);

// A mark's dot, at its time in a row of bars or notes, its letter being in the ruler's row of
// marks: a small square in the text's colour, ringed by a pixel of the background so it shows
// over bars and notes of any shade (decided November 2026, after a letter boxed there blotted
// out the bar under it). Its ring starts at x, so from where letters start, a pixel clear of
// the line at the time.
// Where the tempo changes in view, in a ruler's row of signs: a triangle and whole BPM, as the
// exact value is in the status line and the event list. A section starting at the tempo already in
// force isn't a change, and neither is the song's first, which every song has.
void addTempoSigns(std::vector<RowSign>& signs, const core::TempoMap& tempo, core::Tick from,
                   core::Tick to, int y, const std::function<int(core::Tick)>& toX);

void addMarkDot(Region& region, int x, int y, CellSize cell);

// An armed range in a ruler (decided 24 November 2026): a thin line along the bottom of the
// ruler from x0 to x1, with a cap rising at each end that's in view, so its boundaries show
// whatever the selection. The punch range's is dashed, in the recording colour. A range
// running out of view on a side has no cap there, reaching the edge.
struct RangeBracket
{
    int x0 = 0;
    int x1 = 0;
    bool startShown = true; // a cap at x0
    bool endShown = true;   // a cap at x1
    Role role = Role::IndicatorArm;
    bool dashed = true;
};
void addRangeBracket(Region& region, const RangeBracket& bracket, int bottom, CellSize cell);

// Puts the register's events into the song at a time, its first track's onto a track (counting
// from 0) and the others onto those after it, as far as there are tracks, merging with what's
// there (see core::merged). Returns how many it put, and onto how many tracks.
struct Put
{
    std::size_t events = 0;
    std::size_t tracks = 0;
    std::size_t ramps = 0;
    // A ramp that couldn't go where it would, and the track it was for, the song left as it
    // was on that track; puts are tried on a copy first, to refuse the whole.
    std::optional<core::Song::RampEdit> refused = std::nullopt;
    core::TrackId refusedTrack{0};
};
Put putRegister(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at);

// Notes the bars of the tracks a register was taken from, from the first, over its span from a
// tick, for material taken as whole bars, which brings them.
void takeBars(EventRegister& reg, const core::Song& song, std::size_t first, core::Tick from);

// Gives the tracks a register is put on, from the first, the bars it brought, where theirs
// differ there (putRegister does, after the events). Returns whether any track's changed.
bool putBars(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at);

// What a put there will say of the bars it brings: ", keeping its 7/8", or nothing.
std::string keptBarsText(const core::Song& song, const EventRegister& reg, std::size_t firstTrack,
                         core::Tick at);

// Ripple, staying inside the section (see How far ripple reaches in the design). The end of
// the section holding a time.
core::Tick sectionEndAt(const core::Song& song, core::Tick tick);

// What a put added at the song's end to hold what it put, after a song had a number of
// sections: "; added section Section 3", or nothing.
std::string sectionsAddedText(const core::Song& song, std::size_t before);

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

// How a lane is named in its list and the inspector: Velocity, 74 Brightness, Pitch bend,
// Aftertouch.
// Why an ordinary event edit would disturb a ramp's owned points, if it would.
std::optional<std::string> rampEventEditRefusal(const core::Song& song, core::TrackId track,
                                                const core::EventList& changed);

std::string laneTitle(const core::Lane& lane);

// A ramp's shapes and their names, in the order Up and Down step through them.
inline constexpr std::array<std::pair<core::ControllerRamp::Shape, std::string_view>, 4>
    kRampShapes{{
        {core::ControllerRamp::Shape::Linear, "Linear"},
        {core::ControllerRamp::Shape::Curve, "Curve"},
        {core::ControllerRamp::Shape::SCurve, "S-curve"},
        {core::ControllerRamp::Shape::EqualPower, "Equal power"},
    }};
std::string shapeName(core::ControllerRamp::Shape shape);

// A ramp in the inspector: its span, its values, its shape, the curve's amount and how dense
// its points are, as the piano roll's lane edits them and the event list shows them.
Inspection rampInspection(const core::ControllerRamp& ramp, const core::MeterMap& meter);

// A ramp on tracks (counting from 0, first to last) that a cut at a time, or either end of a
// span, would go through, as taking time out, inserting it or taking a span does: one starting
// before the time and ending at it or after. And a refusal naming it, saying what would cut it.
struct RampCut
{
    core::TrackId track{0};
    std::size_t index = 0;
};
std::optional<RampCut> rampCut(const core::Song& song, std::size_t first, std::size_t last,
                               core::Tick at);
std::optional<RampCut> rampCut(const core::Song& song, std::size_t first, std::size_t last,
                               core::Tick from, core::Tick to);
std::string rampCutText(const core::Song& song, const RampCut& cut);
// Why shortening a section by some bars (a negative count) would be refused for a ramp it
// would cut, if one would.
std::optional<std::string> shorteningCuts(const core::Song& song, std::size_t section,
                                          std::int64_t bars);
// Why a ramp couldn't go where it would, in words.
std::string rampRefusalText(const core::Song::RampEdit& edit);
// Why putting a register at a time, or inserting it there (the section growing by some bars
// first), would be refused for the ramps, tried on a copy of the song: an insertion inside a
// ramp, or a ramp that wouldn't be allowed where it would go.
std::optional<std::string> rampPutRefusal(const core::Song& song, const EventRegister& reg,
                                          std::size_t firstTrack, core::Tick at, bool insert,
                                          std::int64_t bars);
// How many ramps a register holds.
std::size_t rampCount(const EventRegister& reg);

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
