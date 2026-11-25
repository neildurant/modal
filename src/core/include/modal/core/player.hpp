// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/armed_range.hpp>
#include <modal/core/engine.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/recorder.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::core
{

// The control side of playback, for the editor: keeps the engine's plan up to date with the
// song, and plays and stops it from the play-from marker (decided in milestone 4), which
// playback always starts from. With rewind on, the marker stays where it was set; with it
// off, it moves to where playback stops, so the next play resumes there, or to the song's
// start once playback has stopped by itself at the end. It never waits for the engine thread.
class Player
{
public:
    // The destination for a port's name, or none if there's no such port; an empty name is
    // the default port (see the port setting).
    using PortResolver = std::function<std::optional<DestinationId>(std::string_view port)>;

    Player(Engine& engine, PortResolver resolvePort, InputResolver resolveInput = {});

    // Compiles the song and hands the plan to the engine, in playback too. Each port's name is
    // looked up once, until forgetPorts(). A plan the engine can't take yet, its queue full, is
    // kept and handed over again by poll() until it's taken, the newest replacing it (found in
    // a code review, November 2026).
    void update(const Song& song);

    // Whether a plan is waiting for the engine to take it, so poll() should be called.
    bool planWaiting() const { return waiting_ != nullptr; }

    // Plays every track as stored, to compare with its playback quantise, from the next update.
    void setBypassQuantize(bool bypass) { bypassQuantize_ = bypass; }
    bool bypassQuantize() const { return bypassQuantize_; }

    // The tracks whose playback quantise couldn't be applied at the last update, and where.
    const std::vector<QuantizeWarning>& quantizeWarnings() const { return quantizeWarnings_; }

    // Looks every port up again at the next update, as when ports have come or gone.
    void forgetPorts() { ports_.clear(); }

    // The ports the tracks name that weren't found at the last update, in song order, each
    // once; "" for the default port.
    const std::vector<std::string>& missingPorts() const { return missingPorts_; }

    const std::vector<std::string>& missingInputs() const { return missingInputs_; }

    void setThru(bool on) { engine_.setThru(on); }

    // The last channel message that came in, for the In light: a note-on, controller, program,
    // pressure or bend, but not a note-off, so the note played stays shown after it's let go.
    struct InputMessage
    {
        std::array<std::uint8_t, 3> bytes{};
        std::uint8_t size = 0;

        friend bool operator==(const InputMessage&, const InputMessage&) = default;
    };
    const std::optional<InputMessage>& lastInput() const { return lastInput_; }

    // Where playback starts from.
    Tick marker() const { return marker_; }
    void setMarker(Tick tick) { marker_ = tick; }
    // After a song switch, an acknowledgement from the previous song cannot move the marker,
    // which goes to the song's start, or back to where it was in a song returned to.
    void resetMarkerForSong(Tick tick = 0);

    // Whether the marker stays put when playback stops (on to begin with).
    void setRewind(bool rewind) { rewind_ = rewind; }
    bool rewind() const { return rewind_; }

    // Plays from the marker, starting again there if already playing. Each returns whether the
    // engine took the request; if not, its queue full, nothing changes here either.
    [[nodiscard]] bool play(const Song& song);
    [[nodiscard]] bool playLoop(const Song& song, Tick from, Tick to);
    [[nodiscard]] bool stop();

    // Records from the marker onto the song's armed tracks (see Recorder), in the song's record
    // mode, after its count-in or pre-roll, the metronome clicking on the song's metronome port,
    // or the default output where it names none (until the metronome has controls of its own,
    // decided November 2026). Stopping as playback stops; recording carries on past the song's
    // end until then. Returns whether the engine took it; not while playing, or while the last
    // pass is still to be finished.
    //
    // Given a punch range (decided 24 November 2026), it records that instead: from its start,
    // after the count-in or pre-roll before it, up to its end, where recording ends while
    // playback carries on, the take finished there (see punchedOut()).
    [[nodiscard]] bool record(const Song& song, std::optional<ArmedSpan> punch = std::nullopt);

    // Punches in on the fly (decided 24 November 2026): while playing, not looping, and before
    // the punch's start, recording starts there without a lead-in, the music already playing
    // being one, and ends at its end as record() does. The plan is swapped for the pass's, the
    // tracks replaced falling silent from punch-in and the click as the metronome says.
    [[nodiscard]] bool punchIn(const Song& song, ArmedSpan punch);

    // Whether recording, from record() until stopped or punched out; and whether the pass has
    // a click, which it hasn't without a metronome port or default output, nor a count-in then.
    bool recording() const { return recorder_.has_value() && playing_ && !punchedOut_; }
    // Whether a punch has reached its end while playing on, its take to be finished.
    bool punchedOut() const { return recorder_.has_value() && punchedOut_; }
    bool clicking() const { return click_.has_value(); }

    // Where a pass is: counting in, the song silent; playing the pre-roll, before the record
    // point; or recording. Nothing when not recording.
    enum class RecordPhase : std::uint8_t
    {
        CountIn,
        PreRoll,
        Recording,
    };
    std::optional<RecordPhase> recordPhase() const;

    enum class LeadInPhase : std::uint8_t
    {
        CountIn,
        PreRoll,
    };
    std::optional<LeadInPhase> leadInPhase() const;

    // A pass recorded: the song with its take, and the tracks it went onto.
    struct Take
    {
        Song song;
        std::vector<TrackId> tracks;
        bool reached = false; // whether it got past the record point, recording anything at all
    };
    // While a pass is being recorded, or until it's finished: the song with the take so far,
    // as if recording stopped at the playhead (or where it stopped), to show as it's played.
    // The song as it is otherwise, and before the record point.
    Song previewTake(const Song& song);

    // The tracks the pass is recording onto, until it's finished; none when not recording.
    std::vector<TrackId> recordingTracks() const;

    // Whether a pass has stopped and the engine has said where, or punched out, so
    // finishRecording() can end it.
    bool recordingStopped() const
    {
        return recorder_.has_value() && ((!playing_ && stopExact_) || punchedOut_);
    }
    // Ends the stopped pass, taking any input still to come: the take, from the record point to
    // where it stopped, or its punch's end if sooner, applied to the song as it is now. Nothing
    // until recordingStopped(). After a punch, playback carries on, playing the song with it.
    std::optional<Take> finishRecording(const Song& song);

    // Plays the song's click once, the downbeat's or the other beats', on its port (the
    // default output where it names none), as long as it is at the song's first tempo. Returns
    // false if there's nowhere for it to go, or the engine is busy.
    [[nodiscard]] bool auditionClick(const Song& song, bool downbeat);

    // Plays one of a track's notes, stored at a tick, on the track's port and channel, as
    // playback would with its transpose and velocity offset, for its length, but at least
    // kMinAudition, so a very short note is heard and an instrument doesn't miss its end, and
    // at most kMaxAudition (decided 12 November 2026), the next cutting it off. Returns false
    // if the track or its port isn't there, the note is transposed out of range, or the engine
    // is busy.
    static constexpr std::chrono::milliseconds kMinAudition{150};
    static constexpr std::chrono::milliseconds kMaxAudition{1000};
    [[nodiscard]] bool auditionNote(const Song& song, TrackId track, Tick tick, const Note& note);

    // Sends a track's controller, pitch bend or channel pressure value now, on its port and
    // channel, as cancelling a ramp's draft puts back the value at the play position. False
    // for another kind of event, or no port.
    [[nodiscard]] bool sendValue(const Song& song, TrackId track, const EventData& data);

    // Space: stops if playing, and otherwise plays.
    [[nodiscard]] bool toggle(const Song& song);

    // Stops, and sends All Notes Off and Reset All Controllers (see Engine::panic).
    [[nodiscard]] bool panic();

    // Called now and then by the editor: notices where the engine stopped, and stops at the
    // song's end. Returns whether playback stopped by itself.
    bool poll();

    bool playing() const { return playing_; }
    bool looping() const { return playing_ && looping_; }
    Tick startPosition() const { return started_; }

    // Takes the MIDI input the engine has passed back since last asked, so its queue never
    // fills, and returns how many messages there were. Recording keeps them, in a later step.
    std::size_t takeInput();

    // Whether more accepted input is still waiting for the engine. Check before takeInput()
    // so the final published batch is collected before an idle GUI stops polling.
    bool inputPending() const noexcept { return engine_.inputPending(); }

    // How many input messages have been lost so far: dropped because the engine didn't keep up,
    // or not passed back because takeInput() didn't.
    std::size_t inputLost() const;

    // Whether playback has stopped but the engine hasn't yet said where, after which the
    // marker may move there (without rewind); poll() until it has.
    bool stopping() const { return !playing_ && !stopExact_; }

    // The playhead while playing, once the engine has started.
    std::optional<Tick> position() const;

private:
    std::optional<DestinationId> destination(const std::string& port);
    // A port's destination, looked up once, without noting it missing.
    std::optional<DestinationId> lookUp(const std::string& port);
    std::shared_ptr<const PlaybackPlan> compile(const Song& song);
    bool playAt(const Song& song, Tick entry, std::optional<PlaybackLoop> loop);

    // Notes that playback stopped: where the engine was last seen, until it says where.
    void noteStopped();
    void stoppedAt(Tick tick);

    Engine& engine_;
    PortResolver resolvePort_;
    InputResolver resolveInput_;
    std::vector<std::string> missingInputs_;
    std::optional<InputMessage> lastInput_;
    std::map<std::string, std::optional<DestinationId>, std::less<>> ports_;
    std::vector<std::string> missingPorts_;
    std::vector<QuantizeWarning> quantizeWarnings_;
    bool bypassQuantize_ = false;
    std::shared_ptr<const PlaybackPlan> waiting_; // a plan the engine hasn't taken yet
    Tick marker_ = 0;
    bool rewind_ = true;
    bool playing_ = false;
    bool looping_ = false;
    std::uint64_t play_ = 0;             // the engine's id for the last play
    Tick started_ = 0;                   // where the last play started
    Tick playbackEntry_ = 0;             // where the lead-in for playback ends
    bool stopExact_ = true;              // whether the engine has said where the last play stopped
    Tick stoppedAt_ = 0;                 // where it said
    std::optional<Recorder> recorder_;   // the pass being recorded, until finished
    std::optional<DestinationId> click_; // the metronome's while recording
    Tick recordPoint_ = 0;               // where the pass records from
    std::optional<Tick> punchOut_;       // where it stops recording, punching
    bool punchedOut_ = false;            // whether the playhead has passed punchOut_
    bool replacing_ = false;             // whether it replaces, rather than overdubs
    Tick previewAt_ = 0;                 // where the take was last shown up to
    bool countIn_ = false;               // whether the current play counts in
};

} // namespace modal::core
