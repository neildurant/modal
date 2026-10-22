// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/note_fraction.hpp>
#include <modal/core/tick.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace modal::core
{

// A time signature. The beat is one denominator note, so 6/8 has six eighth-note beats.
// Remainder bars use ordinary signatures too, e.g. 1/4 or 5/16.
struct TimeSignature
{
    std::int32_t numerator;
    std::int32_t denominator;

    static constexpr std::int32_t kMaxNumerator = 255;  // SMF stores one byte
    static constexpr std::int32_t kMaxDenominator = 64; // a power of two

    constexpr bool isValid() const
    {
        return numerator >= 1 && numerator <= kMaxNumerator && denominator >= 1 &&
               denominator <= kMaxDenominator && (denominator & (denominator - 1)) == 0;
    }

    constexpr NoteFraction beatLength() const { return {1, denominator}; }
    constexpr NoteFraction barLength() const { return {numerator, denominator}; }

    friend constexpr bool operator==(TimeSignature, TimeSignature) = default;
};

inline constexpr TimeSignature kDefaultTimeSignature{4, 4};

struct MeterChange
{
    Tick tick;
    TimeSignature signature;

    friend constexpr bool operator==(const MeterChange&, const MeterChange&) = default;
};

// A position as bar, beat and tick within the beat. Bars and beats count from 1, as shown
// to the user; the tick counts from 0.
struct BarBeatTick
{
    std::int64_t bar;
    std::int64_t beat;
    Tick tick;

    friend constexpr bool operator==(const BarBeatTick&, const BarBeatTick&) = default;
};

// Reads a position written as a bar ("17") or a bar and beat ("17:3"), counting from 1.
std::optional<BarBeatTick> parseBarBeat(std::string_view text);

// One track's bars: a time signature at each change, starting with one at tick 0. Bars are a
// view of the shared timeline, so changing the meter never moves events in time.
//
// Every change falls on a bar line of the meter before it. Setting a meter mid-bar ends that
// bar early with a remainder bar holding the exact leftover: bar 2 of 4/4 changed at beat 3
// becomes a 2/4 bar, and changed half a beat in becomes a 1/8 bar. A leftover no time
// signature can express (an odd number of ticks) moves the change on to the next beat.
// Ticks are never negative.
class MeterMap
{
public:
    explicit MeterMap(Ppqn ppqn, TimeSignature initial = kDefaultTimeSignature);

    Ppqn ppqn() const { return ppqn_; }

    // All changes in tick order, including remainder bars. The first is always at tick 0.
    std::span<const MeterChange> changes() const { return changes_; }

    // Sets the meter from the given tick on, replacing any change there and adding a remainder
    // bar if the tick is mid-bar. Returns the tick where the new meter starts, which is later
    // than the one given only if the change had to move on to the next beat.
    Tick set(Tick tick, TimeSignature signature);

    // Removes the change at the given tick; the meter before it carries on. Later changes
    // stay where they are where possible, gaining remainder bars where they now fall mid-bar.
    // The change at tick 0 can't be removed. Returns whether a change was removed.
    bool remove(Tick tick);

    TimeSignature signatureAt(Tick tick) const;

    BarBeatTick toBarBeatTick(Tick tick) const;

    // The tick of a position, or nothing if the beat or tick is outside its bar or beat.
    std::optional<Tick> toTick(BarBeatTick position) const;

    // Where a bar (counting from 1) starts.
    Tick barStart(std::int64_t bar) const;

    // The first beat at or after a tick (not before 0), where the metronome clicks.
    Tick beatAtOrAfter(Tick tick) const;

    friend bool operator==(const MeterMap& a, const MeterMap& b)
    {
        return a.ppqn_ == b.ppqn_ && a.changes_ == b.changes_;
    }

private:
    struct Segment
    {
        Tick barTicks;
        Tick beatTicks;
        std::int64_t firstBar; // counting from 0
    };

    Tick beatTicks(TimeSignature signature) const;
    std::size_t changeAt(Tick tick) const;

    // Rebuilds changes_ from the given changes (in tick order, the first at tick 0) so every
    // change falls on a bar line, adding remainder bars and merging repeats, then recomputes
    // segments_. Returns the tick each wanted change ended up at.
    std::vector<Tick> rebuild(const std::vector<MeterChange>& wanted);

    // Recomputes segments_ from changes_.
    void updateSegments();

    Ppqn ppqn_;
    std::vector<MeterChange> changes_;
    std::vector<Segment> segments_; // one per change
};

} // namespace modal::core
