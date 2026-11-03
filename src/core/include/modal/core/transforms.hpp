// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/scale.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::core
{

// The operators' changes to a track's events (decided for milestone 6). Each takes the track's
// events and the indices of those a range takes, in order, and gives the events after the
// change, or why it can't be made: the whole change is refused rather than made in part.
//
// Notes of a pitch never come to overlap: two at the same start are refused; and a note run
// into the next of its pitch is refused when pitches move, but when times move (shift,
// quantise, humanise) ends where the next starts, as notes played legato and quantised need.

struct Refusal
{
    enum class Reason : std::uint8_t
    {
        AboveHighest, // a note would go above G9 (127)
        BelowLowest,  // or below C-1 (0)
        BeforeStart,  // an event would go before the song's start
        PastEnd,      // or at or after its end
        Overlap,      // a note would overlap another of its pitch
        Order,        // a pedal would move past the next of its pedal
    };

    Reason reason;
    Event event;                // the event, as it would be
    std::optional<Event> other; // what it would overlap or move past, as that would be
};

using Transformed = std::variant<EventList, Refusal>;

// Notes up or down by semitones; other events stay as they are.
Transformed transpose(const EventList& events, std::span<const std::size_t> which, int semitones);

// Events later or earlier by some ticks, only the notes if notesOnly is set; none at or after
// the end.
// Pitches changed in a key (decided November 2026): moved by degrees of its scale, or mirrored
// about a pivot's degree; a note outside the scale keeps its distance above the scale's note
// below it, so a chromatic passing note stays one. Refused as transpose() is.
Transformed transposeInKey(const EventList& events, std::span<const std::size_t> which,
                           const Key& key, int degrees);
Transformed invertInKey(const EventList& events, std::span<const std::size_t> which, const Key& key,
                        int pivot);

// Pitches mirrored about a pivot, chromatically: a note 3 above it goes 3 below.
Transformed invert(const EventList& events, std::span<const std::size_t> which, int pivot);

// The notes' order in time reversed, each starting as far before the last note's end as it
// ended after the first note's start, so the rhythm is mirrored too.
Transformed retrograde(const EventList& events, std::span<const std::size_t> which);

Transformed shift(const EventList& events, std::span<const std::size_t> which, Tick by, Tick end,
                  bool notesOnly);

// Time stretched by a ratio (numerator over denominator: 2/1 half time, 1/2 double time) about
// the first event taken (decided November 2026): each event's distance from it and each note's
// length scaled; notes only, with notesOnly. Not rippling: what it runs into, it overlaps, a
// note onto another of its pitch refused, as is an event past the end.
Transformed stretch(const EventList& events, std::span<const std::size_t> which,
                    std::int64_t numerator, std::int64_t denominator, Tick end, bool notesOnly);

// Notes' lengths (decided November 2026): legato, each note or chord held until the next one
// starts, but for the last, which keeps its length, as there's nothing to say how long; a
// fixed length; or scaled by a percentage, never shorter than a tick.
enum class LengthChange : std::uint8_t
{
    Legato,
    Fixed,
    Percent,
};

Transformed changeLengths(const EventList& events, std::span<const std::size_t> which,
                          LengthChange how, Tick amount,
                          const std::function<bool(Tick first, Tick tick)>& together);

// The switch controllers, on or off rather than a curve: sustain, sostenuto, soft, legato and
// portamento (CC 64 to 68).
bool isSwitchController(int controller);

// Thinning dense continuous data (decided November 2026): of each controller's events (not
// the switches'), pitch bend's, channel aftertouch's or each note's poly aftertouch, a gesture
// being those with no gap longer than gestureGap, the first and last of each kept, and of the
// rest those at least spacing after the last kept and changed from it by step or more.
struct ThinWhat
{
    enum class Kind : std::uint8_t
    {
        All,
        Controller,
        Bend,
        Aftertouch,
    };

    Kind kind = Kind::All;
    int controller = 0;

    friend bool operator==(const ThinWhat&, const ThinWhat&) = default;
};

Transformed thin(const EventList& events, std::span<const std::size_t> which, ThinWhat what,
                 Tick spacing, int step, Tick gestureGap);

// The events taken for which drop says so, removed.
Transformed removed(const EventList& events, std::span<const std::size_t> which,
                    const std::function<bool(const Event&)>& drop);

// A controller's events made another's, or a pitch's notes another pitch's.
Transformed remapController(const EventList& events, std::span<const std::size_t> which, int from,
                            int to);
Transformed remapPitch(const EventList& events, std::span<const std::size_t> which, int from,
                       int to);

// The pedals that follow quantised notes: sustain, sostenuto and soft (CC 64, 66 and 67).
bool isPedal(const Event& event);

// Whether any of the events taken is a pedal.
bool hasPedals(const EventList& events, std::span<const std::size_t> which);

// Notes' starts moved to the grid line nearest, their lengths kept; and with pedals set, each
// pedal taken moved as far as the latest note at or before it moved, so it stays as far from
// the note (decided October 2026), those before any note staying put. A pedal moved past the
// next of its pedal is refused.
//
// How, for the transform menu's quantise (decided November 2026): how far towards the line
// (100% onto it, 50% halfway), and what's moved: starts, as q does; ends, keeping starts;
// both; or lengths, rounded to whole steps, keeping starts. A note whose end would come at or
// before its start, as a short one's would, is a step long instead: from its start with both, to
// the next line after its start with ends (decided November 2026, after use); without the step,
// it's left as it was.
struct QuantizeHow
{
    enum class Move : std::uint8_t
    {
        Starts,
        Ends,
        Both,
        Lengths,
    };

    int strength = 100;
    Move move = Move::Starts;
    std::function<Tick(Tick)> step = {}; // the grid step at a tick, for lengths and short notes
};

Transformed quantize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& nearest, bool pedals,
                     const QuantizeHow& how = {});

// The grid line nearest a tick, the lines a step apart from each bar line, which is one too.
// Halfway between two, the later. With swing, the line between each pair of steps from the
// bar line is moved to that percentage of the pair (50% straight, 66% a triplet feel, under
// 50% early).
Tick nearestGridLine(const MeterMap& meter, Tick tick, Tick step, int swing = 50);

// A change to velocities, as typed after ^: a value (80), a change (+10, -10) or a percentage
// (80%). Velocities stay between 1 and 127.
struct VelocityChange
{
    enum class Kind : std::uint8_t
    {
        Set,
        Add,
        Percent,
    };

    Kind kind;
    int amount;

    friend bool operator==(const VelocityChange&, const VelocityChange&) = default;
};

// None if the text isn't one: a value outside 1 to 127, a change beyond 126 either way, or a
// percentage outside 0 to 1000.
std::optional<VelocityChange> parseVelocityChange(std::string_view text);

// How velocity changes keep within the floor and 127 (decided October 2026): with a soft
// knee, a relative change (+10, 120%) is made in full in the middle of the range but eases off
// within kVelocityKnee of the limit it goes towards, never quite reaching it, and is made in
// full away from it; otherwise, plain clipping. Nothing goes below the floor, as 0 is note-off.
struct VelocityLimits
{
    bool soft = true;
    int floor = 1;

    friend bool operator==(const VelocityLimits&, const VelocityLimits&) = default;
};

inline constexpr int kVelocityKnee = 16;

std::uint8_t changedVelocity(std::uint8_t velocity, VelocityChange change,
                             VelocityLimits limits = {});

// A velocity moved towards a target as a relative change is, softly or clipped.
std::uint8_t eased(std::uint8_t velocity, double target, VelocityLimits limits);

Transformed changeVelocity(const EventList& events, std::span<const std::size_t> which,
                           VelocityChange change, VelocityLimits limits = {});

// Lifting moves each note's velocity a percentage of the way towards 127, and lowering towards
// the floor, keeping their order and proportions; neither reaches the limit short of 100%.
Transformed liftVelocity(const EventList& events, std::span<const std::size_t> which, int percent,
                         VelocityLimits limits = {});
Transformed lowerVelocity(const EventList& events, std::span<const std::size_t> which, int percent,
                          VelocityLimits limits = {});

// Velocities kept between low and high, those outside moved to the nearer.
Transformed limitVelocity(const EventList& events, std::span<const std::size_t> which, int low,
                          int high, VelocityLimits limits = {});

// A crescendo or diminuendo, by each note's time from the first note's to the last's: values
// set from one to the other (60-100), or the notes' own scaled by a changing percentage
// (80%-120%), as relative changes are.
struct VelocityRamp
{
    bool percent = false;
    int from = 0;
    int to = 0;

    friend bool operator==(const VelocityRamp&, const VelocityRamp&) = default;
};

Transformed rampVelocity(const EventList& events, std::span<const std::size_t> which,
                         VelocityRamp ramp, VelocityLimits limits = {});

// Compressing, expanding and shaping velocities over time (decided October and November 2026),
// relative to each note's local average, that of the notes taken starting within half the
// window either side of it: compressing pulls each note a percentage of the way towards the
// centre (the local average, the notes' average or a fixed value), expanding pushes it that
// much again away from it, and shaping scales the local averages' own contour about the
// notes' average (130% bigger swells, 70% flatter) keeping each note's place on it. A
// threshold leaves alone the notes not above (or below) it.
struct VelocityDynamics
{
    enum class Kind : std::uint8_t
    {
        Compress,
        Expand,
        Shape,
    };
    enum class Centre : std::uint8_t
    {
        Local,
        Average,
        Fixed,
    };

    Kind kind = Kind::Compress;
    int percent = 50;
    std::function<Tick(Tick)> window = {}; // the window's length at a note's start
    Centre centre = Centre::Local;
    int fixed = 64;
    std::optional<int> above = std::nullopt;
    std::optional<int> below = std::nullopt;
};

Transformed velocityDynamics(const EventList& events, std::span<const std::size_t> which,
                             const VelocityDynamics& dynamics, VelocityLimits limits = {});

// The chords among the notes taken, by index, each with its notes from lowest to highest:
// notes starting together, within the window from the chord's first, as together() says.
std::vector<std::vector<std::size_t>>
chordsOf(const EventList& events, std::span<const std::size_t> which,
         const std::function<bool(Tick first, Tick tick)>& together);

// Within each chord of two notes or more (decided October 2026): accenting the top note up by
// an amount and the others down by it; balancing by pitch, a straight slope of velocities from
// the lowest note to the highest (70-95, or 95-70 to bring out the bass); and compressing or
// expanding the chord's velocities about its average by a percentage, as over time.
enum class ChordVelocity : std::uint8_t
{
    Accent,
    Balance,
    Compress,
    Expand,
};

Transformed chordVelocity(const EventList& events, std::span<const std::size_t> which,
                          const std::function<bool(Tick first, Tick tick)>& together,
                          ChordVelocity how, VelocityRamp amount, VelocityLimits limits = {});

// Two numbers with a dash between, each 1 to 127 (20-110), or percentages up to 1000
// (80%-120%) where percentages are allowed; none if the text isn't one.
std::optional<VelocityRamp> parseVelocityRange(std::string_view text, bool percentages);

// A percentage from 0 to 100, with or without its %: 25%, 25.
std::optional<int> parsePercent(std::string_view text);

// Small random changes to notes: each start moved by up to maxShift(its tick) either way, not
// before 0 nor at or after the end, and each velocity by up to maxVelocity either way, between
// 1 and 127. random(low, high) gives a whole number from low to high, both included.
Transformed humanize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& maxShift, int maxVelocity, Tick end,
                     const std::function<std::int64_t(std::int64_t, std::int64_t)>& random);

// Cuts the notes that sound across a time, those `which` takes, in two there (cutting at a
// range's edges, decided October 2026): a part ending there, its release velocity the usual
// 64, and a part starting there with the note's own velocity and release.
EventList cutAt(const EventList& events, Tick tick, const std::function<bool(const Note&)>& which);

} // namespace modal::core
