// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/playback_transform.hpp>
#include <modal/core/player.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

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

bool Player::record(const Song& song)
{
    if (playing_ || recorder_)
    {
        return false;
    }
    const auto click = lookUp(song.metronome.port);
    auto start = planRecordStart(song, marker_, click);
    recorder_.emplace(song, 0, marker_, song.recordMode, resolveInput_);
    click_ = click;
    recordPoint_ = marker_;
    replacing_ = song.recordMode == RecordMode::Replace;
    auto plan = compile(song);
    const bool countIn = start.countIn.has_value();
    if (!engine_.setPlan(plan) || !engine_.play(start.playFrom, std::move(start.countIn)))
    {
        // The plan, if taken, records nothing without a play; the next update replaces it.
        recorder_.reset();
        click_.reset();
        waiting_ = std::move(plan);
        return false;
    }
    waiting_.reset();
    play_ = engine_.lastPlay();
    previewAt_ = marker_;
    countIn_ = countIn;
    recorder_.emplace(song, play_, marker_, song.recordMode, resolveInput_);
    playing_ = true;
    stopExact_ = true;
    started_ = start.playFrom;
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

std::optional<Player::RecordPhase> Player::recordPhase() const
{
    if (!recording())
    {
        return std::nullopt;
    }
    const auto status = engine_.status();
    // Until the engine has started, as it will start.
    if (status.countingIn || (!status.playing && countIn_))
    {
        return RecordPhase::CountIn;
    }
    const auto at = status.playing ? status.position : std::optional{started_};
    return at && *at < recordPoint_ ? RecordPhase::PreRoll : RecordPhase::Recording;
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
    Take take{recorder_->finish(song, stoppedAt_), recorder_->tracks()};
    recorder_.reset();
    click_.reset();
    // Planned again for playing, the tracks recorded heard again and the click as it plays
    // then, even if nothing was recorded and the song is as it was.
    update(take.song);
    return take;
}

bool Player::play()
{
    if (waiting_ && engine_.setPlan(waiting_))
    {
        waiting_.reset();
    }
    if (!engine_.play(marker_))
    {
        return false;
    }
    if (playing_)
    {
        noteStopped();
    }
    play_ = engine_.lastPlay();
    playing_ = true;
    started_ = marker_;
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

bool Player::toggle()
{
    return playing_ ? stop() : play();
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
}

void Player::stoppedAt(Tick tick)
{
    if (!rewind_)
    {
        marker_ = std::max(tick, Tick{0});
    }
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
    // Recording carries on past the song's end until stopped.
    if (playing_ && !recorder_ && engine_.status().finished)
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
