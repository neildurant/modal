// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/pedalling.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/choice_list.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

// The piano roll (decided October 2026): one track's notes, pitch rows against time, a page of
// whole bars at a time stretched to the width, with the keyboard down the left and the
// velocity lane below. The cursor is a crosshair, a time and a pitch; the note it's on, from
// its start to its end, is the current note, which the inspector shows.
class PianoRollView final : public View
{
public:
    // The commands only the piano roll has; it shares the others' names with the other views.
    static std::span<const CommandInfo> commands();

    // Bars to a page, stepped through by zi and zo, and the one it starts at.
    static constexpr std::array<std::int64_t, 5> kPageSizes{1, 2, 4, 8, 16};
    static constexpr std::size_t kDefaultPageSize = 2; // four bars

    static constexpr int kKeyColumns = 5; // the keyboard's note names, and a space
    static constexpr int kRulerRows = 3;  // the sections and labels, marks, and bar numbers, above
    static constexpr int kLaneRows = 3;   // the lane, at its usual height

    // The document and the host outlive the view.
    PianoRollView(core::Document& document, ViewHost& host);

    // How far apart notes can start and still be one chord, for ( and ) (chordwindow).
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }
    // How far Alt-H and Alt-L nudge a note, and + and _ lengthen and shorten it (nudge).
    void setNudge(std::chrono::milliseconds nudge) { nudge_ = nudge; }
    // Whether O moves the cursor on by the grid step rather than the note's length
    // (gridadvance).
    void setGridAdvance(bool grid) { gridAdvance_ = grid; }
    bool gridAdvance() const { return gridAdvance_; }

    std::string_view name() const override { return "the piano roll"; }
    std::optional<std::string> selectionStatus() const override;
    std::optional<std::pair<core::Tick, core::Tick>> selectionSpan() const override;
    std::optional<std::pair<core::Tick, core::Tick>> rowsSpan() const override;
    std::optional<std::pair<int, int>> boxValues() const override;
    std::optional<CutRange> cutRange(std::string_view motion, int count) override;
    void selectObject(std::string_view object, int count, Selection& selection) override;
    std::optional<std::pair<core::Tick, core::Tick>> objectSpan(std::string_view object,
                                                                int count) override;
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    std::optional<CommandResult> runLine(std::string_view command,
                                         std::string_view arguments) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    std::optional<OperatorTarget> target(std::string_view motion, int count) override;
    std::optional<std::string> describeRange(std::string_view op,
                                             std::string_view motion) const override;
    void addRegions(Frame& frame) const override;
    std::optional<core::EventQuery> queryHere() const override;
    std::string matchStatus(const core::EventQuery& query) const override;
    std::optional<OperatorTarget::Part> matchesHere(const core::EventQuery& query) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override;
    void setCursor(const SongCursor& cursor) override;
    void open(const SongCursor& cursor) override;
    const core::MeterMap& cursorMeter() const override { return meter(); }
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, Follow follow) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;
    // Editing the inspector's fields, with e; or choosing the lane's controller from its list.
    bool typing() const override { return editor_.active() || laneChooser_.has_value(); }
    std::optional<std::string> typingHint() const override
    {
        if (rampDraft_)
            return std::string{kPanelHint} + "; Space plays";
        // On a ramp in the lane, how to change it (asked for 17 November 2026, after use).
        if (!typing() && !host_.selection() && currentRamp())
            return "A ramp: e or Enter changes it, , c R makes it events";
        if (laneChooser_)
            return "Enter chooses, Escape leaves it; a number or a name narrows the list";
        if (!typing())
            return std::nullopt;
        return std::string{kPanelHint};
    }
    const InspectorEditor& inspectorEditor() const { return editor_; }
    void type(char32_t character) override
    {
        // While a ramp's draft holds the keys, Space still plays and stops, to hear it (decided
        // 17 November 2026); none of its fields takes a space.
        if (rampDraft_ && character == U' ')
            host_.togglePlayback();
        else if (laneChooser_)
            laneChooser_->type(character);
        else
            editor_.type(character);
    }
    std::optional<bool> runTyping(std::string_view command) override;

    // What's in view (see Piano roll pages in the design): where it starts and ends in time,
    // a whole number of bars from wherever it starts; the first and last bars it shows, in the
    // track's bars, partial ones included; and the section the cursor is in.
    struct Page
    {
        std::int64_t firstBar = 1;
        std::int64_t lastBar = 1;
        core::Tick start = 0;
        core::Tick end = 0;
        std::size_t section = 0;

        friend bool operator==(const Page&, const Page&) = default;
    };
    Page page() const;

    std::size_t track() const { return track_; } // counting from 0
    core::Tick time() const { return time_; }
    std::uint8_t pitch() const { return pitch_; }
    std::uint8_t topPitch() const { return topPitch_; }
    std::int64_t pageBars() const { return openedBars_.value_or(kPageSizes[pageSize_]); }

    // Opens a selection of bars as the page (Enter on a selection in the arrangement): from a
    // time to another, as many bars as there, up to the most a page holds; turning then moves
    // on by as many, until zooming brings pages back into line.
    void openBars(const SongCursor& cursor, core::Tick from, core::Tick to);
    core::NoteFraction grid() const; // the track's
    void addNewNote(Region& inspector, const core::Track& track) const;
    int noteRows() const; // the pitch rows fully in view
    // The track's lane height, , c h, and the lane's height in pixels from it.
    core::LaneHeight laneSize() const;
    int laneHeight() const;

    // The note the cursor is on, at its pitch from its start to before its end, as an index
    // into the track's events; or none. Of two, the one starting later.
    std::optional<std::size_t> currentNote() const;
    // The same at another time, at the cursor's pitch.
    std::optional<std::size_t> noteAt(core::Tick tick) const;
    // Where h and l go from a time: a grid step, or off a note there in one.
    core::Tick stepLeft(core::Tick tick) const;
    core::Tick stepRight(core::Tick tick) const;

    // Whether the cursor is in the controller lane below the notes (gl), where it moves in time
    // and changes values (see Editing in the lane in the design); and going in and out.
    bool inLane() const { return inLane_; }
    void enterLane();
    void leaveLane() { inLane_ = false; }
    // What the lane shows, the track's own (velocity, a controller, pitch bend or pressure),
    // whether it's the notes' velocities, and its name on the left: vel, cc1, bend or pres.
    core::Lane lane() const;
    bool velocityLane() const;
    // Whether the lane is a pedal's, sustain and the like; and drawn as its regions, not its
    // values (, c v).
    bool isPedalLane() const;
    bool isSwitchLane() const;
    // Where a pedal is down on the track, each from its press to its lift, with the press's
    // index into the track's events.
    std::vector<core::PedalRegion> pedalRegions(std::uint8_t controller) const;
    // A region's events, by index: its press, any values within it, and its lift.
    std::vector<std::size_t> regionEvents(const core::PedalRegion& region) const;
    // Adding, deleting, moving, lengthening and shortening a pedal's regions, as notes are.
    std::optional<bool> editPedal(std::string_view command, int n);
    std::string laneLabel() const;
    // Shows a lane on the track, as , c c's list and :lane choose it, remembering it as used
    // when chosen, and not when ]c and [c step to it.
    void showLane(const core::Lane& lane, bool chosen = true);
    // The list , c c opens, while it's open: the choices as shown, and the lanes they are.
    const ChoiceList* laneChooser() const { return laneChooser_ ? &*laneChooser_ : nullptr; }
    // The note whose velocity the lane's cursor is on: at the cursor's pitch, starting in its
    // grid step; or none.
    std::optional<std::size_t> laneNote() const;
    // The notes a Visual selection in the lane takes: those starting in its time, and in a
    // Ctrl-v box only those whose velocities are between its values.
    std::vector<std::size_t> laneTaken() const;

    // The time the cursor covers, from its start to its end (decided October 2026): the
    // current note's, or on an empty spot the default note's from the cursor, the note o would
    // add, so it shows what you'd get.
    std::pair<core::Tick, core::Tick> cursorSpan() const;

    // Controller ramps in the lane (designed 17 November 2026). The ramp of the lane's
    // controller the cursor is in, from its start to its end, by its index in the track's
    // ramps; and whether a ramp's draft is open in the inspector.
    std::optional<std::size_t> currentRamp() const;
    // What the inspector shows: the current note, the lane's event or ramp, or an open draft.
    Inspection inspection() const;
    bool draftingRamp() const { return rampDraft_.has_value(); }
    // Cancels what's being edited in the inspector, as closing the window does first.
    void cancelEditing() override;

private:
    const core::MeterMap& meter() const;
    std::optional<core::TrackId> trackId() const;
    // Where the track's events a query matches are, in order: a note at its time and pitch,
    // and others at their time and the cursor's pitch, which going to one keeps.
    std::vector<std::pair<core::Tick, std::uint8_t>>
    matchPlaces(const core::EventQuery& query) const;
    // Where the view would end, starting at a tick: as many bars on, at the same place in the
    // bar, or while it's scrolled, its length on. And where the section's page of whole bars
    // holding a tick starts, pages restarting at each section.
    core::Tick viewEnd(core::Tick start) const;
    core::Tick pageStart(core::Tick tick) const;
    // Turns the view by its own width until a time, the cursor's or the playhead's, is in it,
    // keeping its place in the bar, as many bars wide again.
    void turn(core::Tick tick);
    // Shows the section's page of whole bars holding the cursor, as when a track opens.
    void align();
    // Zooms to a page size, keeping the cursor's place on screen.
    void zoomTo(std::size_t size);
    core::Tick gridTicks() const;
    // The grid steps, counted from each bar line: the next after a tick, and the last before.
    core::Tick gridAfter(core::Tick tick) const;
    core::Tick gridBefore(core::Tick tick) const;
    core::Tick lastTick() const; // the song's last, where the cursor can go

    // Moves the cursor in time, or to a pitch, or to another track, keeping it in view.
    void moveTo(core::Tick tick);
    void moveToPitch(int pitch);
    void showTrack(std::size_t track, std::optional<std::uint8_t> pitch);

    // Keeps the cursor's pitch in view, scrolling no further than it must; or puts it in the
    // middle of the view.
    void follow();
    void centre();
    // The middle of the track's notes' range, or middle C if it has none.
    std::uint8_t middlePitch() const;
    // Where ( and ) go: the start of each chord of the track's notes.
    std::vector<core::Tick> chordStarts() const;
    // The ruler above the notes: the sections, labels and play-from marker, then the bars.
    void addRuler(Region& ruler, const TimeScale& scale, const Page& page) const;
    // What drawing a lane's contents shares: the lane's region and size, the page across it,
    // its scale of values, from the lowest a little above its bottom to the highest at its
    // top, and the events a selection takes.
    struct LaneDrawing
    {
        Region& lane;
        const Page& page;
        const TimeScale& scale;
        int width;
        int height;
        int lineWidth;
        const core::Lane& shown;
        int low;
        int high;
        int top;
        int floor;
        const std::vector<std::size_t>& taken;

        int y(int value) const { return floor - (floor - top) * (value - low) / (high - low); }
    };
    void addPedalRegions(const LaneDrawing& d, core::TrackId track) const;
    void addControllerValues(const LaneDrawing& d, core::TrackId track) const;
    // The view's bar and the inspector, above the ruler.
    void addBars(Frame& frame, const Page& page) const;

    // The track's name, or its number if it has none, for messages.
    std::string trackName() const;
    // Changes the track's settings as an undo step, merged with the step before if it has the
    // same merge key (see core::Document::edit).
    void changeTrack(std::string title, std::string_view mergeKey,
                     const std::function<void(core::Track&)>& change);
    void setGrid(core::NoteFraction grid);
    // Changes the track's events as an undo step, merged as changeTrack's are.
    void editEvents(std::string title, const std::function<void(core::EventList&)>& change,
                    std::string_view mergeKey = {});
    // The note the cursor is on, or a message saying there's none to do something to.
    std::optional<std::size_t> noteHere(std::string_view what) const;
    // The notes of the chord holding the note under the cursor and as many chords after it as
    // the count says less one (ic), or none, saying so, if there's no note there.
    std::vector<std::size_t> chordHere(int count) const;
    // The notes of the chord holding the current note, by index, or none; and the inspector's
    // Chord block on one, its name, size and velocities.
    std::vector<std::size_t> chordOfCurrent() const;
    void addChord(Region& inspector) const;

    // Adds the default note at the cursor, or with advancing (O) as many as the count says one
    // after another, the cursor moving on past each; deletes or yanks notes, as indices into
    // the track's events, into the register with their ticks from a time; and puts the
    // register at the cursor's time.
    bool addNote(int count, bool advance);
    bool operateOn(std::string_view op, const std::vector<std::size_t>& notes, core::Tick from,
                   core::Tick span);
    bool put(bool insert, bool ending = false, bool atPitch = false);
    // Makes the current note what an inspector field set it to, or says why it can't be.
    std::optional<std::string> setNote(const std::string& heading, const core::Event& changed);
    // The track's notes starting in a span of time, by index.
    std::vector<std::size_t> notesIn(core::Tick from, core::Tick to) const;
    // The Visual selection's span: from where it started to the cursor, the grid step at the
    // later end in, at every pitch; or nothing if there's none here.
    std::optional<std::pair<core::Tick, core::Tick>> selected() const;
    // With V, the pitches it takes, lowest and highest; with v, every pitch, so nothing.
    std::optional<std::pair<std::uint8_t, std::uint8_t>> selectedPitches() const;
    // Whether a note at a time and pitch is one the selection takes.
    bool takes(core::Tick tick, std::uint8_t pitch) const;
    bool selecting() const; // whether there's a Visual selection here
    // Where a range goes in time from the cursor, or nothing if it goes nowhere; and whether a
    // motion goes across pitches, which isn't a range yet, saying so if it does.
    std::optional<std::pair<core::Tick, core::Tick>> timeSpan(std::string_view motion, int count);
    bool acrossPitches(std::string_view motion) const;
    // Deletes or yanks the cursor's bar and those after it, its notes or as time (ab).
    bool operateOnBars(std::string_view op, int count, bool time);
    // The lane's Visual selection: its time, and with Ctrl-v the values it takes between,
    // lowest and highest; or none if it isn't in the lane.
    bool laneSelecting() const;
    std::optional<std::pair<core::Tick, core::Tick>> laneSpan() const;
    std::optional<std::pair<int, int>> laneValues() const;
    // The track's notes in the order Tab takes them in the lane: by chord, then low to high,
    // as their bars fan out, each with its own start and pitch.
    std::vector<std::pair<core::Tick, std::uint8_t>> laneOrder() const;
    // The same, each with its index into the track's events.
    struct LaneEntry
    {
        core::Tick tick = 0;
        std::uint8_t pitch = 0;
        std::size_t index = 0;
    };
    std::vector<LaneEntry> laneEntries() const;
    // The lane's v or Ctrl-v selection, as places in that order, from the first note it takes
    // to the last; or none, for no selection or one an object made, which takes time.
    std::optional<std::pair<std::size_t, std::size_t>> laneRange() const;
    // Where each note's bar is across the lane, by its index into the track's events, a
    // chord's fanned out side by side.
    std::vector<int> laneBarXs(const TimeScale& scale) const;
    // After a move in the lane, onto a note starting in the cursor's grid step if none there
    // is at its pitch, the lowest.
    void lanePick();
    // Asks for a value, or a change to it, for the lane's event or selection (Enter alone).
    bool askValue();
    // Where a selection keeps what it took: a note at its pitch, a controller's event at 0.
    std::uint8_t lanePitch(const core::Event& event) const;
    // For messages: what the lane's values are (velocity, value, bend, pressure), so many of
    // what it takes (3 notes, 1 event), and that the cursor's on none.
    std::string laneValueName() const;
    std::string laneTaking(std::size_t count) const;
    std::string laneNothing() const;
    // The value the lane holds at a time, from its last event there; adding an event with a
    // value; moving the current event or a selection's (Alt-h, Alt-l, Alt-H, Alt-L); and d
    // and y on a controller's events.
    int heldValue(core::Tick tick) const;
    std::optional<int> priorValue(core::Tick tick) const;
    bool addLaneEvent(core::Tick tick, int value);
    bool moveLaneEvents(std::string_view command, int n);
    bool laneOperate(std::string_view op, std::string_view motion, int count);
    // , c c: opens the list of lanes to choose from; ]c and [c: the track's next or previous.
    bool chooseLane();
    bool stepLane(int by);
    // The lanes offered, in the list's order, with how each is shown.
    std::vector<std::pair<core::Lane, std::string>> laneChoices() const;
    // The lane's own commands: k, j, K, J, Enter and Tab there.
    std::optional<bool> runLane(std::string_view command, int count);
    // Sets each of some events' values in the lane to what a change makes of it, as an undo
    // step merged with the next if it has the same merge key, saying if none changed.
    bool setLaneValues(const std::vector<std::size_t>& indices,
                       const std::function<int(int)>& change, std::string title,
                       std::string_view mergeKey);
    // Controller ramps. A draft is a ramp being made or changed in the inspector: its changes
    // are one undo step, merged by its key, kept by Enter and taken back by Escape.
    struct RampDraft
    {
        core::TrackId track;
        std::size_t index; // in the track's ramps
        bool created;      // made by , c r, so cancelling it makes none
        core::Lane lane;
    };
    std::optional<core::ControllerRamp> draftRamp() const;
    Inspection rampInspection(const core::ControllerRamp& ramp) const;
    // Whether a lane selection takes a ramp's events, which can't be changed one by one.
    bool rampTaken() const;
    // , c r: a ramp across the selection or the cursor's bar, opened as a draft; and opening
    // the cursor's ramp as one, with e or Enter.
    bool makeRamp();
    bool openRamp(std::size_t index, bool created);
    // Sets or steps a draft's field, saying why not if it can't be.
    std::optional<std::string> setRampField(const std::string& heading, const std::string& text);
    std::optional<std::string> stepRampField(const std::string& heading, int steps);
    std::optional<std::string> applyRamp(const core::ControllerRamp& ramp);
    std::string rampRefusal(const core::Song::RampEdit& edit, const core::Song& song) const;
    // Once a draft is kept or cancelled: says which, and on cancelling while playing sends
    // the value at the playhead again.
    void rampFinished(bool kept);
    // , c R: makes the cursor's ramp ordinary events.
    bool convertRamp();
    void addRamps(const LaneDrawing& d, core::TrackId track) const;

    // The commands run in the notes area, and in the lane, moves in time.
    std::optional<bool> runNotes(std::string_view command, int count);
    // Its parts, each returning nothing for a command that isn't its own.
    std::optional<bool> runNoteMove(std::string_view command, int count, bool keepsBarOffset);
    std::optional<bool> runNoteJump(std::string_view command, int count);
    std::optional<bool> runNoteEdit(std::string_view command, int count);

    // Changes the current note, the cursor going with it, unless it would go before the song's
    // start or past its last tick, or run into another note of its pitch. The change says
    // whether it can be made, giving its own message if not.
    bool changeNote(std::string_view verb, const std::function<bool(core::Event&)>& change);
    // The same change to every note a selection takes, refused whole if it can't be made to
    // one, the selection going with them; moves one after another undo together.
    bool changeSelected(std::string_view verb, const std::function<bool(core::Event&)>& change);
    // A time a number of nudges on, or back if negative: at least a tick each way, however
    // short the nudge is at the tempo there.
    core::Tick nudged(core::Tick tick, int nudges) const;

    core::Document& document_;
    ViewHost& host_;
    std::size_t track_ = 0;
    core::Tick time_ = 0;
    core::Tick viewStart_ = 0;
    // While scrolled in time, the view's length, kept so its scale stays put over bars of
    // another length; until it turns or zooms, or a track opens, when it's whole bars again.
    std::optional<core::Tick> viewLength_;
    std::uint8_t pitch_ = 60;
    std::uint8_t topPitch_ = 72;
    std::optional<core::Tick> barOffset_; // where in the bar { and } keep to, while repeated
    std::size_t pageSize_ = kDefaultPageSize;
    std::optional<std::int64_t> openedBars_; // the bars of a selection opened as the page
    std::optional<core::Tick> playhead_;
    bool opened_ = false;                            // on a track yet, since the song was shown
    std::map<core::TrackId, std::uint8_t> pitches_;  // the pitch last used on each track
    std::map<core::TrackId, std::size_t> pageSizes_; // and the page size
    std::chrono::milliseconds chordWindow_{30};
    std::chrono::milliseconds nudge_{5};
    bool gridAdvance_ = false;
    bool inLane_ = false;
    bool pedalValues_ = false; // a pedal's lane showing its values, not its regions (, c v)
    bool zeroTyped_ = false;   // 0 typed in a controller's lane, for Enter to set 0
    std::optional<ChoiceList> laneChooser_;
    std::optional<RampDraft> rampDraft_;
    std::vector<core::Lane> laneChoices_; // what laneChooser_'s choices are
    InspectorEditor editor_;
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
