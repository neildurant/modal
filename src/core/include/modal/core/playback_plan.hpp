#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tempo_map.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace modal::core
{

// Where the scheduler sends events: a MIDI output port now, perhaps a hosted plugin later.
struct DestinationId
{
    std::uint32_t value;

    friend constexpr auto operator<=>(DestinationId, DestinationId) = default;
};

// A channel message of one to three bytes.
struct ShortMessage
{
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;

    std::span<const std::uint8_t> view() const { return {bytes.data(), size}; }

    friend bool operator==(const ShortMessage&, const ShortMessage&) = default;
};

// One event ready to send, with everything the track applies at playback already applied:
// channel, transpose, velocity offset and time offset.
struct PlannedEvent
{
    std::chrono::nanoseconds time; // when to send, from the song's start; can be negative
    Tick tick;                     // the stored position, before any time offset
    TrackId track;
    DestinationId destination;

    // A channel message, or SysEx when sysex is set (message is then empty). The plan owns
    // the SysEx bytes; the engine only reads them.
    ShortMessage message;
    std::shared_ptr<const std::vector<std::uint8_t>> sysex;

    // For note-ons: when the note ends, and its note-off velocity. The engine sends the
    // note-off itself, from its table of sounding notes.
    Tick endTick = 0;
    std::chrono::nanoseconds noteOffTime{0};
    std::uint8_t releaseVelocity = 0;

    bool isNoteOn() const { return message.size == 3 && (message.bytes[0] & 0xF0) == 0x90; }
};

// A track whose transpose took some notes outside 0-127, so they won't be heard.
struct TransposeWarning
{
    TrackId track;
    std::size_t droppedEvents; // notes, and poly pressure on those pitches

    friend bool operator==(const TransposeWarning&, const TransposeWarning&) = default;
};

// An armed track that MIDI thru sends to: on its channel, with its transpose and velocity
// offset, but not its time offset.
struct ThruTarget
{
    TrackId track;
    DestinationId destination;
    std::uint8_t channel; // 0-15
    int transpose;
    int velocityOffset;

    friend bool operator==(const ThruTarget&, const ThruTarget&) = default;
};

struct PlannedTrack
{
    TrackId id;
    DestinationId destination;
    std::uint8_t channel; // 0-15
    bool chaseNotes;
    std::chrono::nanoseconds timeOffset;
};

// The metronome as the scheduler plays it: a click on each beat of the meter, from the
// playback start on.
struct PlannedMetronome
{
    DestinationId destination;
    std::uint8_t channel; // 0-15
    std::uint8_t downbeatNote;
    std::uint8_t downbeatVelocity;
    std::uint8_t beatNote;
    std::uint8_t beatVelocity;
    Tick length; // cut short where the next beat comes sooner
    MeterMap meter;
    bool pastEnd; // carries on past the song's end until stopped, as when recording
};

// A song compiled for playback, built off the engine thread and handed to it as an immutable
// snapshot. Only audible tracks with a destination are included.
struct PlaybackPlan
{
    explicit PlaybackPlan(TempoMap tempoMap)
        : tempo(std::move(tempoMap))
    {
    }

    TempoMap tempo;
    std::vector<PlannedEvent> events; // by time; at equal times, in track and stored order
    std::vector<PlannedTrack> tracks; // the tracks in the plan, in song order
    std::vector<TrackId> unrouted;    // audible tracks left out for want of a destination
    std::vector<TransposeWarning> transposeWarnings;

    // The armed tracks with thru on and a destination, in song order. Muted and un-soloed
    // tracks are included, since thru is how a track being recorded is heard.
    std::vector<ThruTarget> thru;

    // How far the earliest track's negative time offset reaches before a tick's time. Playback
    // starts this much early, so offset events at the start point still play in time.
    std::chrono::nanoseconds leadIn{0};

    // The metronome, if it is to click during this playback.
    std::optional<PlannedMetronome> metronome;

    // The index of the first event at or after a time.
    std::size_t firstAtOrAfter(std::chrono::nanoseconds time) const;

    const PlannedTrack* track(TrackId id) const;
};

// Chooses a destination for a track, or none to leave it out.
using DestinationResolver = std::function<std::optional<DestinationId>(const Track&)>;

struct PlanOptions
{
    bool recording = false; // whether this playback records, for the metronome's When
    std::optional<DestinationId> metronomeDestination; // none leaves the metronome out
};

// Compiles the song's audible tracks: if any track is soloed, the soloed tracks; otherwise
// the unmuted ones. Text events aren't sent and are left out. Notes transposed outside
// 0-127 are left out and reported in transposeWarnings; velocities are clamped to 1-127
// after the velocity offset. Also lists the thru targets, and plans the metronome on the
// reference meter if the song's settings say it plays.
PlaybackPlan compilePlan(const Song& song, const DestinationResolver& resolveDestination,
                         const PlanOptions& options = {});

// What to send when playback starts at a tick, before the events from that tick on: for each
// track, the last program change, controller values, pitch bend and channel pressure before
// the start, in the order they were sent, and then (for tracks with note chasing on) the
// notes still sounding, which end at their usual time. Channel mode messages (CC 120-127)
// aren't chased. Every event is timed to send at the start of playback.
std::vector<PlannedEvent> chase(const PlaybackPlan& plan, Tick start);

} // namespace modal::core
