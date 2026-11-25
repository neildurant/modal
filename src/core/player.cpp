// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/playback_transform.hpp>
#include <modal/core/player.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <span>
#include <utility>
#include <variant>

namespace modal::core
{

Player::Player(Engine& engine, PortResolver resolvePort, InputResolver resolveInput)
    : engine_(engine)
    , resolvePort_(std::move(resolvePort))
    , resolveInput_(std::move(resolveInput))
{
}

std::optional<DestinationId> Player::lookUp(const std::string& port)
{
    auto it = ports_.find(port);
    if (it == ports_.end())
    {
        it = ports_.emplace(port, resolvePort_(port)).first;
    }
    return it->second;
}

std::optional<DestinationId> Player::destination(const std::string& port)
{
    const auto found = lookUp(port);
    if (!found && std::ranges::find(missingPorts_, port) == missingPorts_.end())
    {
        missingPorts_.push_back(port);
    }
    return found;
}

void Player::update(const Song& song)
{
    auto plan = compile(song);
    waiting_ = engine_.setPlan(plan) ? nullptr : std::move(plan);
}

std::shared_ptr<const PlaybackPlan> Player::compile(const Song& song)
{
    missingPorts_.clear();
    missingInputs_.clear();
    // Resolve on every update: input IDs are unavailable during an asynchronous replacement.
    std::map<std::string, std::optional<InputId>, std::less<>> inputs;
    for (const auto& track : song.tracks())
    {
        // Every input but all of them, the default's too when there's a machine to ask.
        const bool all =
            track->input == Track::kAllInputs || (track->input.empty() && !resolveInput_);
        if (!all && !inputs.contains(track->input))
        {
            const auto id = resolveInput_ ? resolveInput_(track->input) : std::nullopt;
            inputs.emplace(track->input, id);
            if (!id)
                missingInputs_.push_back(track->input);
        }
    }
    PlanOptions options;
    if (resolveInput_)
    {
        options.resolveInput = [&](std::string_view name) { return inputs.find(name)->second; };
    }
    options.bypassQuantize = bypassQuantize_;
    // The click's port: while recording, the one looked up as it began, as edits replace the
    // plan; otherwise, with the metronome set to always, the one it names now (found missing in
    // the hardware trial, 13 November 2026, when always clicked only while recording).
    options.recording = recorder_.has_value();
    options.metronomeDestination = recorder_ ? click_
                                   : song.metronome.when == MetronomeSettings::When::Always
                                       ? lookUp(song.metronome.port)
                                       : std::nullopt;
    // Replacing, the tracks being recorded are silent from the record point, as the take will
    // leave them.
    if (recorder_ && replacing_)
    {
        options.replacing = recorder_->tracks();
        options.replacingFrom = recordPoint_;
    }
    auto plan = std::make_shared<const PlaybackPlan>(
        compilePlan(song, [&](const Track& track) { return destination(track.port); }, options));
    quantizeWarnings_ = plan->quantizeWarnings;
    return plan;
}

bool Player::record(const Song& song, std::optional<ArmedSpan> punch)
{
    if (playing_ || recorder_ || (punch && punch->to <= punch->from))
    {
        return false;
    }
    const auto from = punch ? punch->from : marker_;
    const auto click = lookUp(song.metronome.port);
    auto start = planRecordStart(song, from, click);
    recorder_.emplace(song, 0, from, song.recordMode, resolveInput_);
    click_ = click;
    recordPoint_ = from;
    playbackEntry_ = from;
    punchOut_ = punch ? std::optional{punch->to} : std::nullopt;
    punchedOut_ = false;
    replacing_ = song.recordMode == RecordMode::Replace;
    auto plan = compile(song);
    const bool countIn = start.countIn.has_value();
    if (!engine_.setPlan(plan) || !engine_.play(start.playFrom, std::move(start.countIn)))
    {
        // The pass's plan, if taken, records nothing without a play, but plays as the pass
        // would: the tracks replaced silent, the click carrying on. So the song is planned
        // again for playing, to wait for the engine if it must (found in a code review,
        // November 2026).
        recorder_.reset();
        click_.reset();
        update(song);
        return false;
    }
    waiting_.reset();
    play_ = engine_.lastPlay();
    previewAt_ = from;
    countIn_ = countIn;
    recorder_.emplace(song, play_, from, song.recordMode, resolveInput_);
    playing_ = true;
    stopExact_ = true;
    started_ = start.playFrom;
    return true;
}

bool Player::punchIn(const Song& song, ArmedSpan punch)
{
    if (!playing_ || looping_ || recorder_ || punch.to <= punch.from)
        return false;
    if (const auto at = position(); at && *at >= punch.from)
        return false;
    // Input this play has brought so far was before punch-in, and isn't held for a recorder;
    // what follows is, as it arrives.
    recorder_.emplace(song, play_, punch.from, song.recordMode, resolveInput_);
    click_ = lookUp(song.metronome.port);
    recordPoint_ = punch.from;
    punchOut_ = punch.to;
    punchedOut_ = false;
    replacing_ = song.recordMode == RecordMode::Replace;
    previewAt_ = punch.from;
    update(song);
    return true;
}

bool Player::auditionClick(const Song& song, bool downbeat)
{
    const auto& metronome = song.metronome;
    const auto destination = lookUp(metronome.port);
    if (!destination || !metronome.isValid())
    {
        return false;
    }
    const auto length =
        song.tempo().toTime(std::max(metronome.length.toTicks(song.ppqn()), Tick{1}));
    return engine_.audition(*destination, static_cast<std::uint8_t>(metronome.channel - 1),
                            downbeat ? metronome.downbeatNote : metronome.beatNote,
                            downbeat ? metronome.downbeatVelocity : metronome.beatVelocity, length);
}

bool Player::auditionNote(const Song& song, TrackId track, Tick tick, const Note& note)
{
    const auto* found = song.track(track);
    if (found == nullptr || !found->hasValidSettings(song.ppqn()))
    {
        return false;
    }
    const auto destination = lookUp(found->port);
    const auto pitch = playbackPitch(note.pitch, found->transpose);
    if (!destination || !pitch)
    {
        return false;
    }
    const auto& tempo = song.tempo();
    const auto heard = tempo.toTime(tick + std::max(note.duration, Tick{1})) - tempo.toTime(tick);
    return engine_.audition(
        *destination, static_cast<std::uint8_t>(found->channel - 1), *pitch,
        playbackVelocity(note.velocity, found->velocityOffset),
        std::clamp<std::chrono::nanoseconds>(heard, kMinAudition, kMaxAudition));
}

bool Player::sendValue(const Song& song, TrackId track, const EventData& data)
{
    const auto* found = song.track(track);
    if (found == nullptr || !found->hasValidSettings(song.ppqn()))
    {
        return false;
    }
    const auto channel = static_cast<std::uint8_t>((found->channel - 1) & 0x0F);
    std::array<std::uint8_t, 3> message{};
    std::size_t size = 3;
    if (const auto* change = std::get_if<ControlChange>(&data))
    {
        message = {static_cast<std::uint8_t>(0xB0 | channel), change->controller, change->value};
    }
    else if (const auto* bend = std::get_if<PitchBend>(&data))
    {
        const auto value = static_cast<unsigned>(bend->value - PitchBend::kMin);
        message = {static_cast<std::uint8_t>(0xE0 | channel),
                   static_cast<std::uint8_t>(value & 0x7F),
                   static_cast<std::uint8_t>((value >> 7) & 0x7F)};
    }
    else if (const auto* pressure = std::get_if<ChannelPressure>(&data))
    {
        message = {static_cast<std::uint8_t>(0xD0 | channel), pressure->value, 0};
        size = 2;
    }
    else
    {
        return false;
    }
    const auto destination = lookUp(found->port);
    return destination && engine_.send(*destination, std::span{message.data(), size});
}

std::optional<Player::RecordPhase> Player::recordPhase() const
{
    if (!recording())
    {
        return std::nullopt;
    }
    const auto phase = leadInPhase();
    return phase == LeadInPhase::CountIn   ? RecordPhase::CountIn
           : phase == LeadInPhase::PreRoll ? RecordPhase::PreRoll
                                           : RecordPhase::Recording;
}

std::optional<Player::LeadInPhase> Player::leadInPhase() const
{
    if (!playing_)
        return std::nullopt;
    const auto status = engine_.status();
    if (status.countingIn || (!status.playing && countIn_))
    {
        return LeadInPhase::CountIn;
    }
    const auto at = status.playing ? status.position : std::optional{started_};
    const bool recordingNow = recording();
    const auto entry = recordingNow ? recordPoint_ : playbackEntry_;
    return (recordingNow || started_ < entry) && at && *at < entry
               ? std::optional{LeadInPhase::PreRoll}
               : std::nullopt;
}

Song Player::previewTake(const Song& song)
{
    if (!recorder_)
    {
        return song;
    }
    // Between stopping and the engine saying where, as far as the playhead was last seen.
    if (const auto at = position())
    {
        previewAt_ = std::max(previewAt_, *at);
    }
    if (stopExact_ && !playing_)
    {
        previewAt_ = stoppedAt_;
    }
    if (punchOut_)
        previewAt_ = std::min(previewAt_, *punchOut_);
    return recorder_->finish(song, previewAt_);
}

std::vector<TrackId> Player::recordingTracks() const
{
    return recorder_ ? recorder_->tracks() : std::vector<TrackId>{};
}

std::optional<Player::Take> Player::finishRecording(const Song& song)
{
    if (!recordingStopped())
    {
        return std::nullopt;
    }
    takeInput();
    // Punched out while playing on, or stopped: at the punch's end if it came first.
    const auto stop =
        punchedOut_ ? *punchOut_ : std::min(stoppedAt_, punchOut_.value_or(stoppedAt_));
    Take take{recorder_->finish(song, stop), recorder_->tracks(), stop > recordPoint_};
    recorder_.reset();
    click_.reset();
    punchOut_.reset();
    punchedOut_ = false;
    // Planned again for playing, the tracks recorded heard again and the click as it plays
    // then, even if nothing was recorded and the song is as it was.
    update(take.song);
    return take;
}

bool Player::play(const Song& song)
{
    return playAt(song, marker_, std::nullopt);
}

bool Player::playLoop(const Song& song, Tick from, Tick to)
{
    return !recorder_ && playAt(song, from, PlaybackLoop{from, to, song.repeatPlaybackLeadIn});
}

bool Player::changeLoop(std::optional<std::pair<Tick, Tick>> loop)
{
    if (!playing_)
    {
        return false;
    }
    const auto engineLoop =
        loop ? std::optional{PlaybackLoop{loop->first, loop->second, false}} : std::nullopt;
    if (!engine_.setLoop(engineLoop))
    {
        return false;
    }
    loopAsked_ = loop;
    // Just started, the engine may not have said where it is: still where it was started.
    const auto at = position().value_or(playbackEntry_);
    looping_ = loop && at < loop->second;
    return true;
}

bool Player::playLoop(const Song& song, Tick from, Tick to, Tick entry)
{
    if (entry == from)
        return playLoop(song, from, to);
    return !recorder_ && playAt(song, entry, PlaybackLoop{from, to, false});
}

bool Player::playAt(const Song& song, Tick entry, std::optional<PlaybackLoop> loop)
{
    if (waiting_ && engine_.setPlan(waiting_))
    {
        waiting_.reset();
    }
    const auto click = song.playbackLeadIn.mode == CountInSettings::Mode::CountIn
                           ? lookUp(song.metronome.port)
                           : std::nullopt;
    auto start = planPlaybackStart(song, entry, click);
    const bool countIn = start.countIn.has_value();
    if (!engine_.play(start.playFrom, std::move(start.countIn), loop))
    {
        return false;
    }
    if (playing_)
    {
        noteStopped();
    }
    play_ = engine_.lastPlay();
    playing_ = true;
    looping_ = loop.has_value();
    loopAsked_ = loop ? std::optional{std::pair{loop->from, loop->to}} : std::nullopt;
    started_ = start.playFrom;
    playbackEntry_ = entry;
    countIn_ = countIn;
    return true;
}

bool Player::stop()
{
    if (!playing_)
    {
        return true;
    }
    if (!engine_.stop())
    {
        return false;
    }
    noteStopped();
    return true;
}

bool Player::toggle(const Song& song)
{
    return playing_ ? stop() : play(song);
}

void Player::resetMarkerForSong(Tick tick)
{
    marker_ = tick;
    stopExact_ = true;
}

bool Player::panic()
{
    if (!engine_.panic())
    {
        return false;
    }
    if (playing_)
    {
        noteStopped();
    }
    return true;
}

void Player::noteStopped()
{
    stoppedAt(position().value_or(started_));
    stopExact_ = false;
    playing_ = false;
    looping_ = false;
}

void Player::stoppedAt(Tick tick)
{
    if (!rewind_)
    {
        marker_ = std::max(tick, Tick{0});
    }
}

std::optional<Player::InputMessage> Player::lastInput(std::string_view port) const
{
    const auto id = resolveInput_ ? resolveInput_(port) : std::nullopt;
    if (!id)
        return std::nullopt;
    const auto found = lastInputs_.find(*id);
    if (found == lastInputs_.end())
        return std::nullopt;
    return found->second;
}

std::size_t Player::takeInput()
{
    std::size_t messages = 0;
    while (const auto stamped = engine_.nextReceived())
    {
        messages += stamped->first ? 1 : 0;
        if (recorder_)
        {
            recorder_->receive(*stamped);
        }
        const auto& chunk = stamped->chunk;
        const auto kind = chunk.bytes[0] & 0xF0;
        const bool noteOff =
            kind == 0x80 || (kind == 0x90 && chunk.size == 3 && chunk.bytes[2] == 0);
        const auto size = kind == 0xC0 || kind == 0xD0 ? 2 : 3;
        if (stamped->first && !chunk.more && kind >= 0x80 && kind < 0xF0 && !noteOff &&
            chunk.size == size)
        {
            InputMessage message;
            std::copy_n(chunk.bytes.begin(), size, message.bytes.begin());
            message.size = static_cast<std::uint8_t>(size);
            lastInput_ = message;
            lastInputs_[stamped->input] = message;
        }
    }
    return messages;
}

std::size_t Player::inputLost() const
{
    const auto status = engine_.status();
    return status.inputDropped + status.inputUnreported;
}

bool Player::poll()
{
    if (waiting_ && engine_.setPlan(waiting_))
    {
        waiting_.reset();
    }
    if (!stopExact_)
    {
        if (const auto at = engine_.stoppedAt(play_))
        {
            stoppedAt(*at);
            stoppedAt_ = *at;
            stopExact_ = true;
        }
    }
    // A punch's end passed while playing: recording ends there, the take to be finished, input
    // before it having been stamped by the engine before it moved on.
    if (playing_ && recorder_ && punchOut_ && !punchedOut_)
    {
        if (const auto at = engine_.status().position; at && *at >= *punchOut_)
        {
            punchedOut_ = true;
            return true;
        }
    }
    // Recording carries on past the song's end until stopped.
    if (playing_ && !recorder_ && play_ != 0 && engine_.status().finishedPlay == play_)
    {
        // Resuming from the end would play nothing, so without rewind, back to the start. The
        // engine has finished, so if it can't take the stop now, nothing more sounds anyway.
        static_cast<void>(engine_.stop());
        playing_ = false;
        stopExact_ = true;
        stoppedAt(0);
        return true;
    }
    return false;
}

std::optional<Tick> Player::position() const
{
    if (!playing_)
    {
        return std::nullopt;
    }
    const auto status = engine_.status();
    return status.playing ? status.position : std::nullopt;
}

} // namespace modal::core
