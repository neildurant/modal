// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/playback_transform.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tempo_map.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>
#include <modal/core/transforms.hpp>

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
    Tick tick;   // where it plays, before any time offset: where it's stored, or quantised
    Tick stored; // where it's stored
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
    TimeOffset timeOffset;
    std::vector<ShortMessage> setup; // its bank and program setup, in the order to send them
    std::vector<std::size_t> events; // its events in the plan's list, in order
    // How it's quantised as it plays, with its effective grid, if it is, and its events by where
    // they're stored, then the order they're sent in, to find the same event in another plan.
    std::optional<PlaybackQuantize> quantize;
    std::vector<std::size_t> byStored;
};

// A track whose playback quantise left some notes where they're stored, as each would start
// where another of its pitch does; or, with none left so, that can't be applied at all, and so
// plays as stored, as a pedal would pass the next of its pedal.
struct QuantizeWarning
{
    TrackId track;
    Tick at;                     // the first note left in place, or where the pedal would go
    std::size_t leftInPlace = 0; // none: the whole track plays as stored

    friend bool operator==(const QuantizeWarning&, const QuantizeWarning&) = default;
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
    std::uint64_t song = 0;           // which song this came from (see Song::identity)
    std::vector<PlannedEvent> events; // by time; at equal times, in track and stored order
    std::vector<PlannedTrack> tracks; // the tracks in the plan, in song order
    std::vector<TrackId> unrouted;    // audible tracks left out for want of a destination
    std::vector<TransposeWarning> transposeWarnings;
    std::vector<QuantizeWarning> quantizeWarnings;

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
    bool bypassQuantize = false;                       // play every track as stored, to compare
};

// Compiles the song's audible tracks: if any track is soloed, the soloed tracks; otherwise
// the unmuted ones. Text events aren't sent and are left out. Notes transposed outside
// 0-127 are left out and reported in transposeWarnings; velocities are clamped to 1-127
// after the velocity offset. Also lists the thru targets, and plans the metronome on the
// conductor's meter if the song's settings say it plays.
PlaybackPlan compilePlan(const Song& song, const DestinationResolver& resolveDestination,
                         const PlanOptions& options = {});

// Which chased value an event sets, so later ones replace earlier ones, or none if it isn't
// chased: one slot per controller below the channel mode messages (CC 120-127), then program,
// pitch bend and pressure.
std::optional<std::size_t> chaseSlot(const PlannedEvent& event) noexcept;
inline constexpr std::size_t kChaseSlots = 123;

// What to send when playback starts at a tick, before the events from that tick on: for each
// track, its bank and program setup, so that its own events can override it and a later program
// change finds the bank it asked for; then the last program change, controller values, pitch
// bend and channel pressure before the start, in the order they were sent; and then (for tracks
// with note chasing on) the notes still sounding, which end at their usual time. Every event is
// timed to send at the start of playback.
std::vector<PlannedEvent> chase(const PlaybackPlan& plan, Tick start);

} // namespace modal::core
