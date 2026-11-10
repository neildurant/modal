// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/player.hpp>

#include <algorithm>
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
    missingInputs_.clear();
    // Resolve on every update: input IDs are unavailable during an asynchronous replacement.
    std::map<std::string, std::optional<InputId>, std::less<>> inputs;
    for (const auto& track : song.tracks())
    {
        if (!track->input.empty() && !inputs.contains(track->input))
        {
            const auto id = resolveInput_ ? resolveInput_(track->input) : std::nullopt;
            inputs.emplace(track->input, id);
            if (!id)
                missingInputs_.push_back(track->input);
        }
    }
    PlanOptions options;
    options.resolveInput = [&](std::string_view name) { return inputs.find(name)->second; };
    options.bypassQuantize = bypassQuantize_;
    auto plan = std::make_shared<const PlaybackPlan>(
        compilePlan(song, [&](const Track& track) { return destination(track.port); }, options));
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

std::size_t Player::takeInput()
{
    std::size_t messages = 0;
    while (const auto stamped = engine_.nextReceived())
    {
        messages += stamped->first ? 1 : 0;
        const auto& chunk = stamped->chunk;
        if (stamped->first && !chunk.more && chunk.size == 3 && (chunk.bytes[0] & 0xF0) == 0x90 &&
            chunk.bytes[2] > 0)
        {
            lastInputNote_ = InputNote{chunk.bytes[1], chunk.bytes[2]};
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
