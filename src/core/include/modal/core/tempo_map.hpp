#pragma once

#include <modal/core/tick.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

// A tempo, stored as whole microseconds per quarter note, as in Standard MIDI Files.
struct Tempo
{
    std::int64_t microsPerQuarter;

    // The range Modal accepts: 1 to 1000 BPM. Files outside it are clamped on import.
    static constexpr std::int64_t kMinMicrosPerQuarter = 60'000;
    static constexpr std::int64_t kMaxMicrosPerQuarter = 60'000'000;

    // The nearest tempo in whole microseconds per quarter note, clamped to the range.
    static Tempo fromBpm(double bpm);
    double bpm() const;

    constexpr bool isValid() const
    {
        return microsPerQuarter >= kMinMicrosPerQuarter && microsPerQuarter <= kMaxMicrosPerQuarter;
    }

    friend constexpr bool operator==(Tempo, Tempo) = default;
};

inline constexpr Tempo kDefaultTempo{500'000}; // 120 BPM

// The tempo in BPM, with as few decimals as read back to the same microseconds per quarter:
// 120, 114.6228.
std::string bpmText(Tempo tempo);

struct TempoChange
{
    Tick tick;
    Tempo tempo;

    friend constexpr bool operator==(const TempoChange&, const TempoChange&) = default;
};

// The song's tempo over time: a step change at each entry, starting with one at tick 0.
// Tempo is global and is internal timing only; it is never sent as MIDI.
//
// Conversions are exact on the nanosecond grid: toTime() gives the first whole nanosecond
// not before a tick, and toTick() the last tick at or before a time, so
// toTick(toTime(t)) == t for every tick. Ticks and times are never negative.
class TempoMap
{
public:
    explicit TempoMap(Ppqn ppqn, Tempo initial = kDefaultTempo);

    Ppqn ppqn() const { return ppqn_; }

    // All changes in tick order. The first is always at tick 0.
    std::span<const TempoChange> changes() const { return changes_; }

    // Sets the tempo from the given tick on, replacing any change already at that tick.
    void set(Tick tick, Tempo tempo);

    // Removes the change at the given tick. The change at tick 0 can't be removed.
    // Returns whether a change was removed.
    bool remove(Tick tick);

    Tempo tempoAt(Tick tick) const;
    std::chrono::nanoseconds toTime(Tick tick) const;
    Tick toTick(std::chrono::nanoseconds time) const;

    // As toTick(), but a time before 0 gives a tick before 0, at the tempo at tick 0, such as
    // for MIDI input during a count-in at the start of the song.
    Tick toTickExtended(std::chrono::nanoseconds time) const;

private:
    // Index of the change in effect at the tick, or at the time.
    std::size_t segmentAt(Tick tick) const;
    std::size_t segmentAt(std::chrono::nanoseconds time) const;

    // Recomputes start times from the given change onwards.
    void updateStartTimes(std::size_t from);

    Ppqn ppqn_;
    std::vector<TempoChange> changes_;
    std::vector<std::chrono::nanoseconds> startTimes_; // when each change takes effect
};

} // namespace modal::core
