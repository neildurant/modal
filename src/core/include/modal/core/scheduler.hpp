#pragma once

#include <modal/core/playback_plan.hpp>
#include <modal/core/tick.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace modal::core
{

// Where the scheduler sends its messages: the MIDI layer in the engine, a recorder in tests.
class MessageSink
{
public:
    virtual ~MessageSink() = default;

    // Sends one complete message (a channel message or a whole SysEx) to a destination. due is
    // when the scheduler meant it to go, on the caller's clock. Called on the engine thread,
    // so it must not block or allocate.
    virtual void send(DestinationId destination, std::span<const std::uint8_t> bytes,
                      std::chrono::nanoseconds due) noexcept = 0;
};

// What stop sends besides note-offs for the notes sounding, on each channel the plan uses.
struct StopOptions
{
    bool allNotesOff = false;  // CC 123
    bool resetSustain = false; // CC 64 at 0
};

// Plays a playback plan: sends each event at its time, and the note-offs from its own table
// of sounding notes. It does no timing itself. The engine calls sendDue() at or after
// nextWakeTime(), passing the time on its clock; tests pass made-up times.
//
// Everything here runs on the engine thread without allocating or throwing. The caller keeps
// the plan and chased events alive while the scheduler uses them.
class Scheduler
{
public:
    static constexpr std::size_t kMaxSoundingNotes = 4096; // more note-ons aren't sent
    static constexpr std::size_t kMaxDestinations = 64;    // more aren't reached by panic

    explicit Scheduler(MessageSink& sink, StopOptions options = {});

    void setStopOptions(StopOptions options) noexcept { options_ = options; }

    // Replaces the plan. While playing, first sends what is due by now from the old plan, then
    // carries on from the same tick in the new one: events not yet sent come from the new plan,
    // and sounding notes end at their original tick, at the new tempo and time offset. Notes
    // on tracks no longer in the plan (muted, removed or unrouted) end now.
    void setPlan(const PlaybackPlan& plan, std::chrono::nanoseconds now) noexcept;

    // Starts playing from a tick, after sending the chased events (from chase()). With a
    // lead-in, the start tick is reached that long after now. If already playing, the notes
    // sounding end first. Does nothing without a plan.
    void play(Tick start, std::span<const PlannedEvent> chased,
              std::chrono::nanoseconds now) noexcept;

    // Ends the notes sounding and stops, with the stop options.
    void stop(std::chrono::nanoseconds now) noexcept;

    // Ends the notes sounding and stops, then sends All Notes Off and Reset All Controllers on
    // all 16 channels of every destination any plan has used.
    void panic(std::chrono::nanoseconds now) noexcept;

    // Sends everything due by now, in time order; at equal times, note-offs go first.
    void sendDue(std::chrono::nanoseconds now) noexcept;

    // When something is next due, or none if stopped or there's nothing left to send.
    std::optional<std::chrono::nanoseconds> nextWakeTime() const noexcept;

    bool isPlaying() const noexcept { return playing_; }

    // Whether playback has reached the end: every event sent and every note ended.
    bool finished() const noexcept;

    // The tick being played at a time, or none if stopped. During a lead-in, this is before
    // the start tick, and never before tick 0.
    std::optional<Tick> position(std::chrono::nanoseconds now) const noexcept;

    // As position(), but before tick 0 (during a lead-in) it gives ticks before 0, at the
    // tempo at tick 0. For stamping MIDI input.
    std::optional<Tick> tickAt(std::chrono::nanoseconds time) const noexcept;

    std::size_t soundingNotes() const noexcept { return sounding_.size(); }

    // Note-ons not sent because the table of sounding notes was full.
    std::size_t droppedNotes() const noexcept { return droppedNotes_; }

private:
    struct SoundingNote
    {
        DestinationId destination;
        std::uint8_t channel;
        std::uint8_t pitch;
        std::uint8_t releaseVelocity;
        TrackId track;                // of the note that ends last
        Tick endTick;                 // likewise
        std::chrono::nanoseconds end; // plan time of the note-off
    };

    void send(const PlannedEvent& event) noexcept;
    void noteOn(const PlannedEvent& event, std::chrono::nanoseconds due) noexcept;
    void noteOff(std::size_t index, std::chrono::nanoseconds due) noexcept;
    void endAllNotes(std::chrono::nanoseconds now) noexcept;
    void sendControl(DestinationId destination, std::uint8_t channel, std::uint8_t controller,
                     std::uint8_t value, std::chrono::nanoseconds due) noexcept;
    std::optional<std::size_t> nextNoteOff() const noexcept;
    void rememberDestinations(const PlaybackPlan& plan) noexcept;

    MessageSink& sink_;
    StopOptions options_;
    const PlaybackPlan* plan_ = nullptr;

    bool playing_ = false;
    Tick startTick_ = 0;                 // events stored before it were chased, not played
    std::chrono::nanoseconds origin_{0}; // the time on the caller's clock of plan time 0
    std::size_t next_ = 0;               // the next plan event to send
    std::span<const PlannedEvent> chased_;

    std::vector<SoundingNote> sounding_; // capacity reserved up front, never grown
    std::size_t droppedNotes_ = 0;

    std::array<DestinationId, kMaxDestinations> destinations_{};
    std::size_t destinationCount_ = 0;
};

} // namespace modal::core
