// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/input_queue.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace modal::core
{

// Turns MIDI input stamped by the engine into a take on each armed track, and applies it to
// the song when recording stops. It runs on the control side, not the engine thread.
//
// Each armed track takes its chosen input (or all inputs), on its own channel, with its time
// offset taken off so
// the take plays back as it was heard. Input before the record point (during a count-in or
// pre-roll) is dropped, except notes and pedals still held there, which start there. Notes
// and pedals still held when recording stops end there. A new note on a pitch already
// sounding ends the earlier one where it starts, so a take never overlaps notes of one pitch;
// a note whose note-off arrives on the same tick lasts one tick.
class Recorder
{
public:
    // Records the input that arrives during the given play (see Engine::lastPlay) onto the
    // song's armed tracks, from the record point `start`. Named inputs are resolved once for
    // this pass; a missing input leaves its track untouched, including in replace mode.
    Recorder(Song song, std::uint64_t play, Tick start, RecordMode mode = RecordMode::Replace,
             InputResolver resolveInput = {});

    // Input from another play, or from none, is ignored.
    void receive(const StampedInput& input);

    // The song with the take applied, recording having stopped at `stop`. If that's not after
    // the record point, the song is unchanged.
    Song finish(Tick stop) const;

    // The same, onto the song as it is when recording stops, so edits made while recording
    // stay: the take goes onto those of the armed tracks it still has.
    Song finish(Song song, Tick stop) const;

    // The tracks being recorded onto, in song order.
    std::vector<TrackId> tracks() const;

private:
    // One event of a take, as it arrived; a note's duration is known once it has ended.
    struct TakeEvent
    {
        Event event;
        bool ended = true; // false for a note still held
        bool removed = false;
    };

    struct HeldNote
    {
        InputId input;
        std::uint8_t inChannel;
        std::uint8_t pitch;
        std::size_t index; // into TrackTake::events
    };

    struct TrackTake
    {
        TrackId track;
        TimeOffset timeOffset;
        std::optional<InputId> input;
        std::vector<TakeEvent> events;
        std::vector<HeldNote> held;
        std::array<std::optional<std::size_t>, 128> latestNote; // the last note on each pitch
    };

    // A SysEx whose later chunks haven't all arrived.
    struct PartialSysEx
    {
        InputId input;
        std::chrono::nanoseconds songTime;
        std::vector<std::uint8_t> bytes;
    };

    void receiveMessage(InputId input, std::span<const std::uint8_t> message,
                        std::chrono::nanoseconds songTime);
    void noteOn(TrackTake& take, InputId input, std::uint8_t inChannel, std::uint8_t pitch,
                std::uint8_t velocity, Tick tick);
    void noteOff(TrackTake& take, InputId input, std::uint8_t inChannel, std::uint8_t pitch,
                 std::uint8_t releaseVelocity, Tick tick);
    EventList applied(const EventList& events, const TrackTake& take, Tick stop) const;

    Song song_;
    std::uint64_t play_;
    Tick start_;
    RecordMode mode_;
    std::vector<TrackTake> takes_;
    std::vector<PartialSysEx> partial_;
};

} // namespace modal::core
