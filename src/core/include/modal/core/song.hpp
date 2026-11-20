// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/metronome.hpp>
#include <modal/core/ramp.hpp>
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

// What recording does to what's already in the recorded range of the armed tracks.
enum class RecordMode : std::uint8_t
{
    Replace, // what was in the recorded range goes
    Overdub, // the take is merged with it
};

// A bookmark: a named position with no structural meaning.
struct Label
{
    Tick tick;
    std::string name;

    friend bool operator==(const Label&, const Label&) = default;
};

// A mark (ma, decided October 2026): a place named by a letter, a to z, saved with the song
// and kept in the section it's in, so it goes where the section goes: a track (none for the
// conductor), a time and, for one set in the piano roll, a pitch (kNoPitch otherwise, rather
// than an optional, which GCC's release build takes for uninitialised).
struct Mark
{
    static constexpr int kNoPitch = -1;

    char letter = 'a';
    Tick tick = 0;
    std::optional<TrackId> track;
    int pitch = kNoPitch;

    friend bool operator==(const Mark&, const Mark&) = default;
};

// One track's part of a section, positioned from the section's start.
struct TrackContent
{
    // The track's own meter here, or none to follow the conductor's. The section's end cuts
    // its last bar short if the bar would run past it.
    std::optional<MeterMap> meter;
    // Its own events, including any its ramps hide.
    EventList events;
    // Its controller ramps, in order of start, each ending before the section does.
    std::vector<ControllerRamp> ramps;

    bool empty() const { return !meter && events.empty() && ramps.empty(); }

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
    std::span<const Label> labels() const { return labels_; }

    // The marks in it, by letter.
    std::span<const Mark> marks() const { return marks_; }

    // A track's content here, or null if it has no events and follows the conductor.
    const TrackContent* content(TrackId id) const;

    // For building a section to make a song from, as reading a file does. A section in a
    // song can't be changed this way, since the song gives out only const sections.
    void setColour(std::optional<std::string> colour) { colour_ = std::move(colour); }
    void addTempo(TempoChange change); // after the others; at 0, replaces the first
    void addLabel(Label label);        // after the others
    void addMark(Mark mark);           // of a letter not there yet
    void setContent(TrackId id, TrackContent content);

    friend bool operator==(const Section& a, const Section& b);

private:
    friend class Song;

    std::string name_;
    std::optional<std::string> colour_;
    Tick length_;
    std::vector<TempoChange> tempo_;
    MeterMap meter_;
    std::vector<Label> labels_;
    std::vector<Mark> marks_;
    std::map<TrackId, std::shared_ptr<const TrackContent>> content_;
};

// A whole track in a register, independent of later edits to its source. Each part keeps its
// section's name, length and conductor's meter, so a put can tell whether the destination's
// sections are the same, and keep the track's bars where they aren't.
struct TrackCopy
{
    Track settings;
    Ppqn ppqn;
    struct Part
    {
        std::string section;
        Tick length;
        TrackContent content;
        MeterMap conductor; // the section's, which the track followed where it had no meter
        friend bool operator==(const Part&, const Part&) = default;
    };
    std::vector<Part> parts;

    friend bool operator==(const TrackCopy&, const TrackCopy&) = default;
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

    // A song of one section, "Section 1", eight bars of 4/4 at 120 BPM.
    explicit Song(Ppqn ppqn = kPpqn);

    // A song made from its parts, as reading a file does: its tracks in display order, and at
    // least one section, whose content is for those tracks, in order. The parts must fit
    // together: each section's conductor meter a whole number of bars, and its tempo changes,
    // labels and events inside it.
    Song(Ppqn ppqn, std::vector<Track> tracks, std::vector<Section> sections);

    Ppqn ppqn() const { return tempo_.ppqn(); }

    // Which song this is: new for each song made or read, and kept through copies, edits and
    // undo, so that playback can tell an edited song from a different one.
    std::uint64_t identity() const { return identity_; }

    std::string title;
    MetronomeSettings metronome;
    CountInSettings countIn;
    RecordMode recordMode = RecordMode::Replace;

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

    // Moves some adjoining tracks, from first on, so the first of them is at the position `to`
    // of the tracks as they'll be. A track's settings, events, meters and marks go with it, being
    // held by its ID. Returns false if they don't all fit there, leaving the song as it was.
    bool moveTracks(std::size_t first, std::size_t count, std::size_t to);

    std::optional<TrackCopy> copyTrack(TrackId id) const;
    // Whether a copy can go in: one at the song's PPQN.
    bool canInsertTrack(const TrackCopy& copy) const;
    // New ID and a unique name; settings survive. Into the same sections (names and lengths),
    // each part goes to its section with its own or following meter. Into different ones
    // (decided 20 November 2026), the parts are laid along time from the song's start and cut
    // at its sections, a section of whole bars being added at the end for anything past it;
    // where the copy's bars differ from the song's, the track keeps them as its own meter, and
    // a ramp that would cross a section becomes its events. Refuses a different PPQN, leaving
    // the song unchanged.
    std::optional<TrackId> insertTrack(std::size_t position, const TrackCopy& copy);

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
    // the earlier section, and the new section starts a new bar in the same meter; so its end
    // too may become a remainder bar, or if it's the last section, grow to the next bar line.
    // The name is numbered if another section has it. Returns nothing if the tick isn't
    // inside a section.
    std::optional<Split> splitSection(Tick tick, std::string name);

    // Renames a section, numbering the name if another section has it. Returns false if
    // there's no such section.
    bool renameSection(std::size_t index, std::string name);

    // A name for a new section: "Section" and one more than the highest number of a section
    // already named that way, so new sections count up in the order they're made.
    std::string newSectionName() const;

    // Whole sections, as the section strip works on them (decided October 2026). Each keeps
    // what's in it; the sections after follow on, so later material closes up or moves along.

    // Removes sections from an index; at least one section always stays. Returns whether any
    // went.
    bool removeSections(std::size_t index, std::size_t count);

    // Inserts copies of sections before an index (or after the last, at the number of
    // sections), each numbered if another section has its name. Content for tracks the song
    // no longer has is left out. Returns whether it inserted any.
    bool insertSections(std::size_t index,
                        std::span<const std::shared_ptr<const Section>> sections);

    // Inserts copies of sections taken from another song, whose tracks are given, as
    // insertSections does. Each of its tracks' content goes to this song's track of the same
    // name, each used once; a track with content and no match here is added after the others,
    // with its settings (decided 20 November 2026). Returns the names of the tracks added.
    std::vector<std::string>
    insertSectionsFrom(std::size_t index, std::span<const std::shared_ptr<const Section>> sections,
                       std::span<const std::shared_ptr<const Track>> from);

    // Inserts an empty section of whole bars before an index (or after the last), in the meter
    // and at the tempo where it goes, numbered if another section has its name. Returns
    // whether it could.
    bool insertEmptySection(std::size_t index, std::string name, std::int64_t bars);

    // Moves a section by places, earlier if negative, as far as the ends go, past its
    // neighbours. Returns where it is now.
    std::size_t moveSection(std::size_t index, std::ptrdiff_t by);

    // Lengthens a section by bars at its end, in the meter there, or shortens it, removing its
    // last bars and everything starting in them, never to less than a bar. Returns whether it
    // changed.
    bool resizeSection(std::size_t index, std::int64_t bars);

    // Joins a section with the one after it, which becomes its later part: its tempo and meter
    // changes, labels and each track's events and meter, positioned from the first section's
    // start. A track with a meter of its own in either keeps it, following the conductor's
    // in the other part. Returns whether there was one after to join.
    bool joinSections(std::size_t index);

    // Bars on every track (decided October 2026), from a bar line of the conductor's, as
    // :bar-delete and :bar-insert make them, each section staying one section. What an edit
    // did, or why it couldn't, leaving the song as it was.
    struct BarsEdit
    {
        bool done = false;
        std::optional<TrackId> misaligned; // a track with a meter of its own whose bars it cuts
        std::optional<TrackId> ramp;       // a track with a ramp it would cut
        std::vector<std::shared_ptr<const Section>> removed; // what removeBars took out
    };

    // Removes bars of the conductor's, as far as the song's end, with everything starting in
    // them: tempo and meter changes, labels, and every track's events and meter, what's after
    // closing up. What went comes back as sections, named "From" and those it was taken from. Not
    // done if it would leave no bars, the tick isn't a bar line, or a track's own bars don't line
    // up with them.
    BarsEdit removeBars(Tick from, std::int64_t count);

    // Inserts empty bars before a bar line of the conductor's, in the section there, in a meter
    // given or the one there and at the tempo there, the meter after them going on as before.
    // A track with a meter of its own keeps it through them, so they must be whole bars of it.
    BarsEdit insertBars(Tick at, std::int64_t count,
                        std::optional<TimeSignature> signature = std::nullopt);

    // A ramp a tick would cut, if a section started there: one starting before it and ending
    // at it or after. As a track and the ramp's index in ramps(track).
    struct RampAt
    {
        TrackId track;
        std::size_t index;
    };
    std::optional<RampAt> rampAcross(Tick tick) const;

    // Sets the last section to the fewest whole bars, at least one, that hold everything in
    // it, including the ends of notes.
    void fitToContent();

    // The flattened timeline.
    const TempoMap& tempo() const { return tempo_; }
    const MeterMap& meter() const { return meter_; } // the conductor's
    std::span<const Label> labels() const { return labels_; }
    // The marks, by letter, at ticks from the song's start; and one by its letter, if set.
    std::span<const Mark> marks() const { return marks_; }
    const Mark* mark(char letter) const;

    // A track's meter (its own where it has one, the conductor's elsewhere) and events. Not
    // to be asked of a track the song doesn't have.
    const MeterMap& meter(TrackId id) const;
    // What plays: the track's events with its ramps' in place of those they hide.
    const EventList& events(TrackId id) const;
    // The track's own events, including those its ramps hide.
    const EventList& sourceEvents(TrackId id) const;
    // Its ramps, at ticks from the song's start, in order of start.
    std::span<const ControllerRamp> ramps(TrackId id) const;

    // Controller ramps (designed 17 November 2026). An edit is refused, changing nothing, for
    // no such track or ramp, settings that don't make sense, a ramp reaching the song's end or
    // the next section, or one touching or overlapping another of its controller on the track.
    enum class RampRefusal : std::uint8_t
    {
        None,
        NoTrack,
        NoRamp,
        Invalid,
        PastEnd,
        CrossesSection,
        Collides,
    };
    struct RampEdit
    {
        RampRefusal refusal = RampRefusal::None;
        std::size_t index = 0; // the ramp's index afterwards, or the one it collides with

        bool done() const { return refusal == RampRefusal::None; }
    };
    RampEdit addRamp(TrackId id, ControllerRamp ramp);
    // Replaces a ramp, by its index in ramps(id), as moving, resizing or reshaping it does.
    RampEdit replaceRamp(TrackId id, std::size_t index, ControllerRamp ramp);
    // Removes a ramp, showing the events it hid again.
    bool removeRamp(TrackId id, std::size_t index);
    // Makes a ramp's events ordinary ones, in place of those it hid, and removes it.
    bool convertRamp(TrackId id, std::size_t index);
    // Edits a track's own events, those its ramps hide included, and its ramps together, both
    // at ticks from the song's start, as moving time or putting a register does, so a ramp and
    // what it hides stay together. Refused, changing nothing, if a ramp it leaves wouldn't be
    // allowed where it is (see addRamp), with the index of the one at fault among those left.
    RampEdit updateTrackContent(
        TrackId id,
        const std::function<void(EventList& source, std::vector<ControllerRamp>& ramps)>& edit);

    // Edits the tempo, the conductor's meter, a track's meter or a track's events in ticks
    // from the song's start, then shares the result out to the sections. A track's events are
    // edited as they play; its ramps' events can't be changed this way, being made from the
    // ramps, so whatever the edit leaves of a ramp's controller in its span is set aside and
    // the events the ramp hid kept. A section keeps its
    // length when the conductor's meter changes, ending in a remainder bar if need be, except
    // the last, which grows to the next bar line instead. Editing a track's meter gives it a
    // meter of its own in every section. Return false if there's no such track.
    void updateTempo(const std::function<void(TempoMap&)>& edit);
    void updateMeter(const std::function<void(MeterMap&)>& edit);
    bool updateMeter(TrackId id, const std::function<void(MeterMap&)>& edit);
    bool updateEvents(TrackId id, const std::function<void(EventList&)>& edit);

    // Sets the meter over a span of time, never moving an event: the conductor's (and so the
    // tracks following it), or one track's, which then has a meter of its own in each section
    // the span touches, a copy of the conductor's outside the span. In each such section, the
    // meter there before resumes at the span's end, and changes inside the span go. A span
    // starting inside a bar cuts it to a remainder bar first; one ending inside a bar of the
    // meter before ends with a remainder bar of the new, and if that end isn't on a bar line of
    // the meter before, a remainder bar after it brings the bars back onto their old lines, so
    // the bars after the span stay where they were. Sections keep their places and lengths.
    // Returns false, changing nothing, for no such track, an empty span or one outside the
    // song, or a signature whose beat isn't a whole number of ticks.
    bool setMeter(std::optional<TrackId> track, Tick from, Tick to, TimeSignature signature);

    // Sets the track following the conductor's meter in every section, or in those a span of
    // time touches. Returns false if there's no such track.
    bool followConductor(TrackId id);
    bool followConductor(TrackId id, Tick from, Tick to);

    // Adds a bookmark, or renames the one at that tick. The name is numbered if another
    // bookmark in the same section has it.
    void setLabel(Tick tick, std::string name);
    bool removeLabel(Tick tick);

    // Sets a mark, at a tick from the song's start, moving it if it's set already; and
    // removes one. Marks go with their sections; those in sections taken out go with them, and
    // sections put in bring none, so a letter marks one place. Removing a track removes its
    // marks.
    void setMark(Mark mark);
    bool removeMark(char letter);

    // Whether two songs have the same settings, tracks and sections.
    friend bool operator==(const Song& a, const Song& b);

    // A song without its flattened timeline, which can be rebuilt from the rest: for keeping
    // past versions of a song, as undo does, without the timeline's copy of every event.
    // The tracks and sections are shared with the song's.
    struct Parts
    {
        Ppqn ppqn;
        std::string title;
        MetronomeSettings metronome;
        CountInSettings countIn;
        RecordMode recordMode;
        std::vector<std::shared_ptr<const Track>> tracks;
        std::vector<std::shared_ptr<const Section>> sections;
        std::uint64_t nextTrackId;
        std::uint64_t identity;
    };

    Parts parts() const;

    // The song again from its parts, rebuilding the timeline.
    explicit Song(Parts parts);

private:
    static std::uint64_t newIdentity();

    struct Flattened
    {
        MeterMap meter;
        std::shared_ptr<const EventList> events; // what plays; shared, so copying is cheap
        std::shared_ptr<const EventList> source; // the same list when there are no ramps
        std::vector<ControllerRamp> ramps;
    };

    std::size_t indexOf(TrackId id) const;

    // Shares a track's own events out to the sections, and rebuilds what plays.
    void setSource(std::size_t trackIndex, EventList source);
    // Shares a track's ramps out to the sections, and rebuilds what plays.
    void setRamps(std::size_t trackIndex, std::vector<ControllerRamp> ramps);
    RampEdit checkRamp(std::size_t trackIndex, const ControllerRamp& ramp,
                       std::optional<std::size_t> except) const;

    // Grows the last section to the bar line at or after the tick.
    void growTo(Tick end);

    // Replaces a section, if it changed, keeping the old one's sharing otherwise.
    void replaceSection(std::size_t index, std::shared_ptr<Section> section);

    // Lets `edit` change a track's content in a section (empty if it has none), and keeps the
    // result if it changed, dropping it if it ends with no events and no meter of its own.
    void updateContent(std::size_t index, TrackId id,
                       const std::function<void(TrackContent&)>& edit);

    // Rebuilds the flattened timeline: the conductor's (with the section starts and labels),
    // the tracks' meters, one track's events, or all of it.
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
    std::uint64_t identity_ = newIdentity();

    // Flattened.
    TempoMap tempo_;
    MeterMap meter_;
    std::vector<Label> labels_;
    std::vector<Mark> marks_;
    std::vector<Tick> starts_;         // each section's start, then the song's end
    std::vector<Flattened> flattened_; // parallel to tracks_
};

} // namespace modal::core
