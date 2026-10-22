#pragma once

#include <modal/core/engine.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::core
{

// The control side of playback, for the editor: keeps the engine's plan up to date with the
// song, and plays, stops and resumes it, remembering where playback last started and where
// it stopped. It never waits for the engine thread.
class Player
{
public:
    // The destination for a port's name, or none if there's no such port; an empty name is
    // the default port (see the port setting).
    using PortResolver = std::function<std::optional<DestinationId>(std::string_view port)>;

    Player(Engine& engine, PortResolver resolvePort);

    // Compiles the song and hands the plan to the engine, in playback too. Each port's name is
    // looked up once, until forgetPorts().
    void update(const Song& song);

    // Looks every port up again at the next update, as when ports have come or gone.
    void forgetPorts() { ports_.clear(); }

    // The ports the tracks name that weren't found at the last update, in song order, each
    // once; "" for the default port.
    const std::vector<std::string>& missingPorts() const { return missingPorts_; }

    void play(Tick start);
    void stop();

    // Space: stops if playing; otherwise plays from where playback last started (rewind) or
    // from where it stopped, or from the song's start if it stopped at the end by itself.
    void toggle(bool rewind);

    // Plays from where playback last started.
    void playAgain() { play(started_); }

    // Stops, and sends All Notes Off and Reset All Controllers (see Engine::panic).
    void panic();

    // Called now and then by the editor: notices where the engine stopped, and stops at the
    // song's end. Returns whether playback stopped by itself.
    bool poll();

    bool playing() const { return playing_; }

    // The playhead while playing, once the engine has started.
    std::optional<Tick> position() const;

    Tick started() const { return started_; }
    Tick stopped() const { return stopped_; }

private:
    std::optional<DestinationId> destination(const std::string& port);
    void noteStopped();

    Engine& engine_;
    PortResolver resolvePort_;
    std::map<std::string, std::optional<DestinationId>, std::less<>> ports_;
    std::vector<std::string> missingPorts_;
    bool playing_ = false;
    std::uint64_t play_ = 0; // the engine's id for the last play
    Tick started_ = 0;
    Tick stopped_ = 0;
    bool stopExact_ = true; // whether stopped_ is where the engine says the last play stopped
    bool atEnd_ = false;    // whether the last play stopped by itself at the song's end
};

} // namespace modal::core
