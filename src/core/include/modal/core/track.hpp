#pragma once

#include <modal/core/event.hpp>
#include <modal/core/tick.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

// A track's events in send order: by tick, and at the same tick, non-note events (in the
// order added) before notes (in the order added).
class EventList
{
public:
    std::span<const Event> all() const { return events_; }
    std::size_t size() const { return events_.size(); }
    bool empty() const { return events_.empty(); }
    const Event& operator[](std::size_t index) const { return events_[index]; }

    // Events starting in [from, to).
    std::span<const Event> startingIn(Tick from, Tick to) const;

    // Inserts after the events already at its tick that it sorts with: after other non-note
    // events but before notes, or after everything for a note. Returns its index.
    std::size_t insert(Event event);

    void erase(std::size_t index);

    friend bool operator==(const EventList&, const EventList&) = default;

private:
    std::vector<Event> events_;
};

// Keeps notes of one pitch from overlapping where an added note meets an old one, as the
// editor and the recorder never create overlaps: whichever starts first ends where the other
// starts, and at the same tick the added note replaces the old. Old notes that overlap each
// other, as an imported file's may, are left as they are.
void resolveOverlaps(std::vector<Event>& old, std::vector<Event>& added);

// Events with others added, put in without overlaps (see resolveOverlaps).
EventList merged(const EventList& old, std::vector<Event> added);

// Identifies a track for its whole life, across edits and song snapshots.
struct TrackId
{
    std::uint64_t value;

    friend constexpr auto operator<=>(TrackId, TrackId) = default;
};

// A track's settings. Its meter and events are held by the song's sections.
class Track
{
public:
    static constexpr int kMinChannel = 1;
    static constexpr int kMaxChannel = 16;
    static constexpr int kMaxTranspose = 127; // semitones either way
    static constexpr int kMaxVelocityOffset = 127;
    static constexpr std::chrono::milliseconds kMaxTimeOffset{1000};

    explicit Track(TrackId id);

    TrackId id() const { return id_; }

    std::string name;
    std::string port; // output port name, resolved through the machine's port aliases
    int channel = kMinChannel;
    bool mute = false;
    bool solo = false;
    bool arm = false;
    bool thru = true;       // while armed, send MIDI input on to the track's port and channel
    bool chaseNotes = true; // off for drums and other sounds where a late attack sounds wrong

    // Applied at playback only; the stored events never change.
    int transpose = 0;
    int velocityOffset = 0;
    std::chrono::milliseconds timeOffset{0}; // negative sends early

    // Whether channel and the playback settings are in range.
    bool hasValidSettings() const;

    friend bool operator==(const Track&, const Track&) = default;

private:
    TrackId id_;
};

} // namespace modal::core
