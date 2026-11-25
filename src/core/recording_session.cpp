// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/recording_session.hpp>

#include <algorithm>
#include <utility>

namespace modal::core
{

RecordingSession::RecordingSession(Document& document)
    : document_(document)
{
}

RecordingSession::~RecordingSession()
{
    // No document callback may retain this session after its owner goes away. Transport
    // shutdown belongs to the Player/Engine owner; destruction never waits for the engine.
    if (pending())
    {
        clearHooks();
    }
}

RecordingSession::State RecordingSession::state() const
{
    return !take_ ? State::Idle : take_->player->recording() ? State::Recording : State::Finishing;
}

std::span<const TrackId> RecordingSession::tracks() const
{
    return take_ ? std::span<const TrackId>{take_->tracks} : std::span<const TrackId>{};
}

bool RecordingSession::start(Player& player, std::optional<ArmedSpan> punch,
                             std::optional<ArmedSpan> loop)
{
    // Playing, only a punch can start, on the fly.
    if (pending() || player.stopping() || (player.playing() && (!punch || loop)) ||
        !(player.playing() ? player.punchIn(document_.underlying(), *punch)
                           : player.record(document_.underlying(), punch, loop)))
    {
        return false;
    }
    take_ = Take{&player, player.recordingTracks(), player.inputLost()};
    document_.setPreview([this](const Song& song) { return take_->player->previewTake(song); });
    setGuard();
    return true;
}

void RecordingSession::setGuard()
{
    document_.setGuard([this](const Song& before, const Song& after)
                       { return guard(before, after); });
}

std::optional<RecordingSession::Completion>
RecordingSession::finishPass(const EditPlace& place,
                             const std::function<std::string(std::uint32_t)>& title)
{
    if (!take_)
        return std::nullopt;
    auto pass = take_->player->finishPass(document_.underlying());
    if (!pass)
        return std::nullopt;
    Completion completion{false, std::move(pass->tracks), 0, true, pass->pass};
    // The guard keeps other edits off the tracks being recorded; the pass is the take itself.
    document_.setGuard({});
    completion.changed =
        document_.edit(title(pass->pass), place, [&](Song& song) { song = std::move(pass->song); });
    setGuard();
    return completion;
}

bool RecordingSession::stop()
{
    return !take_ || take_->player->stop();
}

bool RecordingSession::refreshPreview()
{
    if (!take_ || take_->player->recordPhase() != Player::RecordPhase::Recording)
    {
        return false;
    }
    document_.refreshPreview();
    return true;
}

bool RecordingSession::inputLost() const
{
    return take_ && take_->player->inputLost() > take_->inputLost;
}

std::optional<RecordingSession::Completion> RecordingSession::finish(const EditPlace& place,
                                                                     std::string title)
{
    // Stopped, or punched out with playback carrying on.
    if (!take_ || (take_->player->playing() && !take_->player->punchedOut()))
    {
        return std::nullopt;
    }
    take_->player->poll();
    auto finished = take_->player->finishRecording(document_.underlying());
    if (!finished)
    {
        return std::nullopt;
    }
    Completion completion{false, std::move(finished->tracks),
                          take_->player->inputLost() - take_->inputLost, finished->reached,
                          finished->pass};
    clearHooks();
    // Keep the session pending through the commit, including document listener callbacks.
    completion.changed = document_.edit(std::move(title), place,
                                        [&](Song& song) { song = std::move(finished->song); });
    take_.reset();
    return completion;
}

std::optional<std::string> RecordingSession::fileOperationRefusal() const
{
    // Once stopped, the take is committed within a frame or two; saying so rather than asking
    // to stop what has stopped (decided 16 November 2026, after a review).
    switch (state())
    {
    case State::Recording:
        return "Stop recording first";
    case State::Finishing:
        return "Finishing the take: try again in a moment";
    case State::Idle:
        break;
    }
    return std::nullopt;
}

std::optional<std::string> RecordingSession::eventEditRefusal(TrackId id) const
{
    if (!take_ || std::ranges::find(take_->tracks, id) == take_->tracks.end())
    {
        return std::nullopt;
    }
    const auto& song = document_.underlying();
    const auto* track = song.track(id);
    if (track == nullptr)
    {
        return std::nullopt;
    }
    const auto index =
        std::ranges::find_if(song.tracks(), [&](const auto& t) { return t->id() == id; }) -
        song.tracks().begin();
    const auto name = track->name.empty() ? "track " + std::to_string(index + 1) : track->name;
    return name + " is being recorded: change it once recording stops";
}

void RecordingSession::clearHooks()
{
    document_.setPreview({});
    document_.setGuard({});
}

std::optional<std::string> RecordingSession::guard(const Song& before, const Song& after) const
{
    for (const auto id : take_->tracks)
    {
        const auto* track = before.track(id);
        if (track == nullptr)
        {
            continue;
        }
        if (after.track(id) == nullptr || !(after.events(id) == before.events(id)))
        {
            return eventEditRefusal(id);
        }
    }
    return std::nullopt;
}

} // namespace modal::core
