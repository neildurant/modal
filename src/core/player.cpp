// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/player.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace modal::core
{

Player::Player(Engine& engine, PortResolver resolvePort)
    : engine_(engine)
    , resolvePort_(std::move(resolvePort))
{
}

std::optional<DestinationId> Player::destination(const std::string& port)
{
    auto it = ports_.find(port);
    if (it == ports_.end())
    {
        it = ports_.emplace(port, resolvePort_(port)).first;
    }
    if (!it->second && std::ranges::find(missingPorts_, port) == missingPorts_.end())
    {
        missingPorts_.push_back(port);
    }
    return it->second;
}

void Player::update(const Song& song)
{
    missingPorts_.clear();
    auto plan = std::make_shared<const PlaybackPlan>(
        compilePlan(song, [&](const Track& track) { return destination(track.port); },
                    {.bypassQuantize = bypassQuantize_}));
    quantizeWarnings_ = plan->quantizeWarnings;
    waiting_ = engine_.setPlan(plan) ? nullptr : std::move(plan);
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
            stopExact_ = true;
        }
    }
    if (playing_ && engine_.status().finished)
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
