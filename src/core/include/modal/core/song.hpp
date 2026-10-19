#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/metronome.hpp>
#include <modal/core/tempo_map.hpp>
#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

// A bookmark: a named position with no structural meaning.
struct Marker
{
    Tick tick;
    std::string name;

    friend bool operator==(const Marker&, const Marker&) = default;
};

// One track's part of a section, positioned from the section's start.
struct TrackContent
{
    // The track's own meter here, or none to follow the conductor's. The section's end cuts
    // its last bar short if the bar would run past it.
    std::optional<MeterMap> meter;
    EventList events;

    friend bool operator==(const TrackContent&, const TrackContent&) = default;
};

// A section of the song, spanning every track. Everything in it is positioned from its start,
// so moving or copying a section changes nothing inside it. Only the song changes sections.
class Section
{
public:
    Section(std::string name, Tick length, MeterMap meter, Tempo tempo);

    const std::string& name() const { return name_; }
    const std::optional<std::string>& colour() const { return colour_; }
    Tick length() const { return length_; }

    // The conductor's tempo changes, in tick order, the first at 0.
    std::span<const TempoChange> tempo() const { return tempo_; }

    // The conductor's meter. The section is a whole number of its bars.
    const MeterMap& meter() const { return meter_; }

    // Bookmarks in tick order, each with a name of its own within the section.
    std::span<const Marker> markers() const { return markers_; }

    // A track's content here, or null if it has no events and follows the conductor.
    const TrackContent* content(TrackId id) const;

    friend bool operator==(const Section& a, const Section& b);

private:
    friend class Song;

    std::string name_;
    std::optional<std::string> colour_;
    Tick length_;
    std::vector<TempoChange> tempo_;
    MeterMap meter_;
    std::vector<Marker> markers_;
    std::map<TrackId, std::shared_ptr<const TrackContent>> content_;
};

// A song: its tracks, and the sections that hold their content, in order, covering the song
// from tick 0 with no gaps. A value that is cheap to copy, since copies share their tracks
// and sections. Each edit changes a copy, and what it touches is copied on write, so the
// engine can keep playing a previous snapshot while the editor works on the next.
//
// The song also keeps its flattened timeline, rebuilt after each edit: the tempo, the
// conductor's meter, and each track's meter and events, at ticks from the song's start.
// Playback, export and the views read these. Edits given in those ticks are shared out to
// the sections, and anything put past the end grows the last section by whole bars.
class Song
{
public:
    static constexpr std::int64_t kNewSongBars = 8;

    // A song of one section, "Song", eight bars of 4/4 at 120 BPM.
    explicit Song(Ppqn ppqn = kPpqn);

    Ppqn ppqn() const { return tempo_.ppqn(); }

    std::string title;
    MetronomeSettings metronome;
    CountInSettings countIn;

    // Tracks in display order; track 1 is tracks()[0].
    std::span<const std::shared_ptr<const Track>> tracks() const { return tracks_; }

    // The track with the given ID, or null if there isn't one.
    const Track* track(TrackId id) const;

    // Adds an empty track at the end (or before the given position) and returns its ID.
    TrackId addTrack(std::string name = {});
    TrackId addTrack(std::size_t position, std::string name);

    // Copies the track's settings, lets `edit` change the copy, and replaces the track with
    // it. Other songs sharing the old track are unaffected. Returns false if there's no such
    // track.
    bool updateTrack(TrackId id, const std::function<void(Track&)>& edit);

    bool removeTrack(TrackId id);

    // Sections in order.
    std::span<const std::shared_ptr<const Section>> sections() const { return sections_; }

    // Where a section starts, and (for the section count) where the song ends.
    Tick sectionStart(std::size_t index) const { return starts_[index]; }
    Tick end() const { return starts_.back(); }

    // The section holding a tick; the last section for ticks past the end.
    std::size_t sectionAt(Tick tick) const;

    // What splitting a section cut short: the conductor's bar, and the bars of tracks with
    // meters of their own. Tracks following the conductor are cut with it.
    struct Split
    {
        std::size_t index; // of the new section
        bool conductor;
        std::vector<TrackId> tracks;
    };

    // Starts a new section at the tick, which must be inside a section and not at its start,
    // taking the rest of that section. A bar the tick falls inside becomes a remainder bar in
    // the earlier section, and the new section starts a new bar in the same meter. The name is
    // numbered if another section has it. Returns nothing if the tick isn't inside a section.
    std::optional<Split> splitSection(Tick tick, std::string name);

    // Renames a section, numbering the name if another section has it. Returns false if
    // there's no such section.
    bool renameSection(std::size_t index, std::string name);

    // Sets the last section to the fewest whole bars, at least one, that hold everything in
    // it, including the ends of notes.
    void fitToContent();

    // The flattened timeline.
    const TempoMap& tempo() const { return tempo_; }
    const MeterMap& meter() const { return meter_; } // the conductor's
    std::span<const Marker> markers() const { return markers_; }

    // A track's meter (its own where it has one, the conductor's elsewhere) and events. Not
    // to be asked of a track the song doesn't have.
    const MeterMap& meter(TrackId id) const;
    const EventList& events(TrackId id) const;

    // Edits the tempo, the conductor's meter, a track's meter or a track's events in ticks
    // from the song's start, then shares the result out to the sections. A section keeps its
    // length when the conductor's meter changes, ending in a remainder bar if need be, except
    // the last, which grows to the next bar line instead. Editing a track's meter gives it a
    // meter of its own in every section. Return false if there's no such track.
    void updateTempo(const std::function<void(TempoMap&)>& edit);
    void updateMeter(const std::function<void(MeterMap&)>& edit);
    bool updateMeter(TrackId id, const std::function<void(MeterMap&)>& edit);
    bool updateEvents(TrackId id, const std::function<void(EventList&)>& edit);

    // Sets the track following the conductor's meter in every section.
    bool followConductor(TrackId id);

    // Adds a bookmark, or renames the one at that tick. The name is numbered if another
    // bookmark in the same section has it.
    void setMarker(Tick tick, std::string name);
    bool removeMarker(Tick tick);

private:
    struct Flattened
    {
        MeterMap meter;
        std::shared_ptr<const EventList> events; // shared, so copying a song is cheap
    };

    std::size_t indexOf(TrackId id) const;

    // Grows the last section to the bar line at or after the tick.
    void growTo(Tick end);

    // Replaces a section, if it changed, keeping the old one's sharing otherwise.
    void replaceSection(std::size_t index, std::shared_ptr<Section> section);

    // Lets `edit` change a track's content in a section (empty if it has none), and keeps the
    // result if it changed, dropping it if it ends with no events and no meter of its own.
    void updateContent(std::size_t index, TrackId id,
                       const std::function<void(TrackContent&)>& edit);

    // Rebuilds the flattened timeline: the conductor's (with the section starts and markers)
    // and the tracks' meters, and the events of one track or of all.
    void rebuildConductor();
    void rebuildMeters();
    void rebuildEvents(std::size_t trackIndex);
    void rebuildAll();

    // The name, numbered if a section other than the one given has it.
    std::string uniqueSectionName(std::string name,
                                  std::optional<std::size_t> except = std::nullopt) const;

    std::vector<std::shared_ptr<const Track>> tracks_;
    std::vector<std::shared_ptr<const Section>> sections_;
    std::uint64_t nextTrackId_ = 1;

    // Flattened.
    TempoMap tempo_;
    MeterMap meter_;
    std::vector<Marker> markers_;
    std::vector<Tick> starts_;         // each section's start, then the song's end
    std::vector<Flattened> flattened_; // parallel to tracks_
};

} // namespace modal::core
