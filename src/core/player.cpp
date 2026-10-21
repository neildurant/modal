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
    engine_.setPlan(std::make_shared<const PlaybackPlan>(
        compilePlan(song, [&](const Track& track) { return destination(track.port); })));
}

void Player::play(Tick start)
{
    if (playing_)
    {
        noteStopped();
    }
    engine_.play(start);
    play_ = engine_.lastPlay();
    playing_ = true;
    started_ = start;
}

void Player::stop()
{
    if (playing_)
    {
        engine_.stop();
        noteStopped();
    }
}

void Player::toggle(bool rewind)
{
    if (playing_)
    {
        stop();
    }
    else
    {
        play(rewind ? started_ : stopped_);
    }
}

void Player::panic()
{
    engine_.panic();
    if (playing_)
    {
        noteStopped();
    }
}

void Player::noteStopped()
{
    // Where the engine was last seen, until it says where it stopped.
    stopped_ = position().value_or(started_);
    stopExact_ = false;
    playing_ = false;
}

bool Player::poll()
{
    if (!stopExact_)
    {
        if (const auto at = engine_.stoppedAt(play_))
        {
            stopped_ = std::max(*at, Tick{0});
            stopExact_ = true;
        }
    }
    if (playing_ && engine_.status().finished)
    {
        stop();
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
