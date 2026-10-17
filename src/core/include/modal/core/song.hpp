#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/metronome.hpp>
#include <modal/core/tempo_map.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

struct Marker
{
    Tick tick;
    std::string name;

    friend bool operator==(const Marker&, const Marker&) = default;
};

// A song: a value that is cheap to copy, since copies share their tracks. Each edit changes a
// copy, and the tracks it touches are copied on write, so the engine can keep playing a
// previous snapshot while the editor works on the next.
class Song
{
public:
    explicit Song(Ppqn ppqn = kPpqn);

    Ppqn ppqn() const { return tempo_.ppqn(); }

    std::string title;
    MetronomeSettings metronome;

    const TempoMap& tempo() const { return tempo_; }
    TempoMap& tempo() { return tempo_; }

    // Markers in tick order. Adding one at a tick that already has one replaces its name.
    std::span<const Marker> markers() const { return markers_; }
    void setMarker(Tick tick, std::string name);
    bool removeMarker(Tick tick);

    // Tracks in display order; track 1 is tracks()[0].
    std::span<const std::shared_ptr<const Track>> tracks() const { return tracks_; }

    // The track with the given ID, or null if there isn't one.
    const Track* track(TrackId id) const;

    // Adds an empty track at the end (or before the given position) and returns its ID.
    TrackId addTrack(std::string name = {});
    TrackId addTrack(std::size_t position, std::string name);

    // Copies the track, lets `edit` change the copy, and replaces the track with it. Other
    // songs sharing the old track are unaffected. Returns false if there's no such track.
    bool updateTrack(TrackId id, const std::function<void(Track&)>& edit);

    bool removeTrack(TrackId id);

    // The track whose meter markers, the loop range and the main position display use:
    // the chosen one, or track 1 if none is chosen or the chosen one was removed. Empty only
    // when the song has no tracks.
    std::optional<TrackId> referenceTrack() const;
    void setReferenceTrack(std::optional<TrackId> id);

    // The reference track's meter, or the default meter when there are no tracks.
    const MeterMap& referenceMeter() const;

private:
    std::size_t indexOf(TrackId id) const;

    TempoMap tempo_;
    MeterMap defaultMeter_;
    std::vector<Marker> markers_;
    std::vector<std::shared_ptr<const Track>> tracks_;
    std::optional<TrackId> referenceTrack_;
    std::uint64_t nextTrackId_ = 1;
};

} // namespace modal::core
