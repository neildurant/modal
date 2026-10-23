// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/event_list.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/view.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The event list (decided in milestone 4): one track's events at a time, with the
// conductor's section starts, meter and tempo changes and markers among them, dimmed, and
// the end of the song last; or the conductor's own list, track 0. A row for each event,
// with its fields in columns under fixed headings; the cursor is on one field of one row.
// A bar at the top names the track and the section the cursor is in, and the inspector below
// it shows the cursor's row in full.
class EventListView final : public View
{
public:
    // The view's own commands.
    static std::span<const CommandInfo> commands();

    // Shows the document's song, from track 1 (or the conductor's list, if there are no
    // tracks). The document and the host outlive the view.
    EventListView(core::Document& document, ViewHost& host);
    EventListView(const EventListView&) = delete;
    EventListView& operator=(const EventListView&) = delete;

    std::string_view name() const override { return "the event list"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override;
    const core::MeterMap& cursorMeter() const override;
    void setCursor(const SongCursor& cursor) override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, Follow follow) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;
    bool typing() const override { return field_.has_value(); }
    void type(char32_t character) override;
    std::optional<bool> runTyping(std::string_view command) override;

    // How far apart events can start and still be one chord, for ( and ) (chordwindow).
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }

    // The track shown: 0 for the conductor, then 1 for the first track.
    std::size_t track() const { return track_; }
    const EventList& list() const { return list_; }
    std::size_t row() const { return row_; }
    Column field() const; // the cursor's, on its row
    std::size_t top() const { return top_; }
    int viewRows() const; // rows of events fully shown

    // The row playing, while playing: the last at or before the playhead.
    std::optional<std::size_t> playheadRow() const { return playheadRow_; }

    // Whether a field is being edited in place (after Enter), and what's typed there.
    bool editingField() const { return field_.has_value(); }
    const CommandLine& fieldLine() const { return fieldLine_; }

private:
    void setMessage(std::string message, MessageKind kind)
    {
        host_.setMessage(std::move(message), kind);
    }

    // Shows a track's list, the cursor at or after a tick.
    void showTrack(std::size_t track, core::Tick tick);
    EventList listOf(std::size_t track) const;
    void moveRows(std::int64_t by);

    // Moves to the first row at or after a time a motion reached. Going back, if that row
    // isn't earlier than the cursor's, the motion is taken again from there until it is, or it
    // can go no further, so it never sticks.
    void land(core::Tick target, bool forward, const std::function<core::Tick(core::Tick)>& again);

    // The rows starting chords: events starting together, within the chord window of the
    // first of them; in the conductor's list, its rows.
    std::vector<std::size_t> chordStarts() const;

    // Deletes or yanks the track's own events among some rows, which may be in either order.
    bool operateRows(std::string_view op, std::size_t from, std::size_t to);
    bool put();

    // Starts editing the field under the cursor in place, with its value in it; then sets it
    // from what's typed, or steps it.
    bool openField();
    bool addNote(); // o: a note at the cursor's time, editing its pitch first
    bool finishField();
    bool stepFieldBy(int steps);

    // The conductor's list: sets a row's value (a section's or marker's name, a tempo, a
    // meter) from text or steps it, or deletes the tempo and meter changes and markers among
    // some rows, but not a section's own tempo and meter at its start, nor a section.
    std::optional<std::string> setConductor(std::size_t row, std::string_view text);
    std::optional<std::string> stepConductor(std::size_t row, int steps);
    bool deleteConductor(std::size_t from, std::size_t to);

    // Replaces the event of a row with a changed one, if it doesn't overlap a note of its
    // pitch, and puts the cursor on it. Returns why not, or nothing.
    std::optional<std::string> replaceEvent(std::size_t row, const core::Event& changed,
                                            Column field);

    // Changes the song as one undo step, made where the cursor is, and shows the change.
    bool edit(std::string title, const std::function<void(core::Song&)>& change);
    std::string trackName() const;
    void moveFields(int by);
    void follow();
    std::size_t rowAtOrBefore(core::Tick tick) const;

    Region listRegion() const;
    Region inspectorRegion() const;
    int inspectorPadding() const { return cell_.height / 4; }
    Region barRegion() const;

    core::Document& document_;
    ViewHost& host_;
    std::size_t track_ = 0;
    EventList list_;
    std::size_t row_ = 0;
    Column wanted_ = Column::Position;    // the field the cursor keeps to, as rows change
    std::optional<core::Tick> barOffset_; // where in the bar { and } keep to, while repeated
    std::chrono::milliseconds chordWindow_{30};
    std::size_t top_ = 0;
    struct FieldPrompt
    {
        std::size_t row;
        Column column;
        bool fresh;                        // typing replaces the value; keys that edit it keep it
        std::optional<core::Event> adding; // a note being added by o, shown after the row
    };
    std::optional<FieldPrompt> field_;
    CommandLine fieldLine_;
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
    std::optional<std::size_t> playheadRow_;
};

} // namespace modal::ui
