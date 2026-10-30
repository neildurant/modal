// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/bar_lines.hpp>
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
    static constexpr int kRulerRows = 2;  // the sections, markers and bar numbers, above
    static constexpr int kLaneRows = 3;   // the velocity lane

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
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    std::optional<CommandResult> runLine(std::string_view command,
                                         std::string_view arguments) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    std::optional<OperatorTarget> target(std::string_view motion, int count) override;
    std::optional<std::string> describeRange(std::string_view op,
                                             std::string_view motion) const override;
    std::optional<std::pair<std::string, std::string>>
    relabel(std::string_view command) const override;
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
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
    // Editing the inspector's fields, with e.
    bool typing() const override { return editor_.active(); }
    const InspectorEditor& inspectorEditor() const { return editor_; }
    void type(char32_t character) override { editor_.type(character); }
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
    std::int64_t pageBars() const { return kPageSizes[pageSize_]; }
    core::NoteFraction grid() const; // the track's
    int noteRows() const;            // the pitch rows fully in view

    // The note the cursor is on, at its pitch from its start to before its end, as an index
    // into the track's events; or none. Of two, the one starting later.
    std::optional<std::size_t> currentNote() const;

    // The time the cursor covers, from its start to its end (decided October 2026): the
    // current note's, or on an empty spot the default note's from the cursor, the note o would
    // add, so it shows what you'd get.
    std::pair<core::Tick, core::Tick> cursorSpan() const;

private:
    const core::MeterMap& meter() const;
    std::optional<core::TrackId> trackId() const;
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
    Inspection inspection() const;
    // The ruler above the notes: the sections, markers and play-from marker, then the bars.
    void addRuler(Region& ruler, const TimeScale& scale, const Page& page) const;

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

    // Adds the default note at the cursor, or with advancing (O) as many as the count says one
    // after another, the cursor moving on past each; deletes or yanks notes, as indices into
    // the track's events, into the register with their ticks from a time; and puts the
    // register at the cursor's time.
    bool addNote(int count, bool advance);
    bool operateOn(std::string_view op, const std::vector<std::size_t>& notes, core::Tick from,
                   core::Tick span);
    bool put(bool insert);
    // Makes the current note what an inspector field set it to, or says why it can't be.
    std::optional<std::string> setNote(const std::string& heading, const core::Event& changed);
    // The track's notes starting in a span of time, by index.
    std::vector<std::size_t> notesIn(core::Tick from, core::Tick to) const;
    // Where a range goes in time from the cursor, or nothing if it goes nowhere; and whether a
    // motion goes across pitches, which isn't a range yet, saying so if it does.
    std::optional<std::pair<core::Tick, core::Tick>> timeSpan(std::string_view motion, int count);
    bool acrossPitches(std::string_view motion) const;
    // Deletes or yanks the cursor's bar and those after it, its notes or as time (ab).
    bool operateOnBars(std::string_view op, int count, bool time);
    // Changes the current note, the cursor going with it, unless it would go before the song's
    // start or past its last tick, or run into another note of its pitch. The change says
    // whether it can be made, giving its own message if not.
    bool changeNote(std::string_view verb, const std::function<bool(core::Event&)>& change);
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
    std::optional<core::Tick> playhead_;
    bool opened_ = false;                            // on a track yet, since the song was shown
    std::map<core::TrackId, std::uint8_t> pitches_;  // the pitch last used on each track
    std::map<core::TrackId, std::size_t> pageSizes_; // and the page size
    std::chrono::milliseconds chordWindow_{30};
    std::chrono::milliseconds nudge_{5};
    bool gridAdvance_ = false;
    InspectorEditor editor_;
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
