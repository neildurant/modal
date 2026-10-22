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

    Player(Engine& engine, PortResolver resolvePort);

    // Compiles the song and hands the plan to the engine, in playback too. Each port's name is
    // looked up once, until forgetPorts().
    void update(const Song& song);

    // Looks every port up again at the next update, as when ports have come or gone.
    void forgetPorts() { ports_.clear(); }

    // The ports the tracks name that weren't found at the last update, in song order, each
    // once; "" for the default port.
    const std::vector<std::string>& missingPorts() const { return missingPorts_; }

    // Where playback starts from.
    Tick marker() const { return marker_; }
    void setMarker(Tick tick) { marker_ = tick; }

    // Whether the marker stays put when playback stops (on to begin with).
    void setRewind(bool rewind) { rewind_ = rewind; }
    bool rewind() const { return rewind_; }

    // Plays from the marker, starting again there if already playing.
    void play();
    void stop();

    // Space: stops if playing, and otherwise plays.
    void toggle();

    // Stops, and sends All Notes Off and Reset All Controllers (see Engine::panic).
    void panic();

    // Called now and then by the editor: notices where the engine stopped, and stops at the
    // song's end. Returns whether playback stopped by itself.
    bool poll();

    bool playing() const { return playing_; }

    // Whether playback has stopped but the engine hasn't yet said where, after which the
    // marker may move there (without rewind); poll() until it has.
    bool stopping() const { return !playing_ && !stopExact_; }

    // The playhead while playing, once the engine has started.
    std::optional<Tick> position() const;

private:
    std::optional<DestinationId> destination(const std::string& port);

    // Notes that playback stopped: where the engine was last seen, until it says where.
    void noteStopped();
    void stoppedAt(Tick tick);

    Engine& engine_;
    PortResolver resolvePort_;
    std::map<std::string, std::optional<DestinationId>, std::less<>> ports_;
    std::vector<std::string> missingPorts_;
    Tick marker_ = 0;
    bool rewind_ = true;
    bool playing_ = false;
    std::uint64_t play_ = 0; // the engine's id for the last play
    Tick started_ = 0;       // where the last play started
    bool stopExact_ = true;  // whether the engine has said where the last play stopped
};

} // namespace modal::core
