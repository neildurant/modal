#pragma once

#include <modal/core/playback_plan.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace modal::core
{

// One count-in click, by how long before the record point it comes.
struct CountInClick
{
    std::chrono::nanoseconds before;
    bool accent; // where a bar would start, running the record point's meter backwards

    friend bool operator==(const CountInClick&, const CountInClick&) = default;
};

// A count-in as the scheduler plays it: metronome clicks before the record point, while the
// song stays silent. It always clicks, whatever the metronome's When.
struct PlannedCountIn
{
    DestinationId destination;
    std::uint8_t channel; // 0-15
    std::uint8_t accentNote;
    std::uint8_t accentVelocity;
    std::uint8_t beatNote;
    std::uint8_t beatVelocity;
    std::chrono::nanoseconds clickLength; // cut short where the next beat comes sooner
    std::chrono::nanoseconds length;      // from the first click to the record point
    std::vector<CountInClick> clicks;     // in time order
};

// Where playback starts to record from a record point, and the count-in before it if any.
struct RecordStart
{
    Tick playFrom;
    std::optional<PlannedCountIn> countIn;
};

// Plans the start of recording from the song's count-in and metronome settings: a count-in
// at the tempo and in the conductor's meter at the record point (none without a metronome
// destination), or a pre-roll from that many bars before it, but not before the song's start.
RecordStart planRecordStart(const Song& song, Tick recordPoint,
                            std::optional<DestinationId> metronomeDestination);

} // namespace modal::core
