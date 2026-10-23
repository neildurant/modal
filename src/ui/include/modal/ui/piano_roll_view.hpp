// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/inspector.hpp>
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
    static constexpr int kLaneRows = 3;   // the velocity lane

    // The document and the host outlive the view.
    PianoRollView(core::Document& document, ViewHost& host);

    // How far apart notes can start and still be one chord, for ( and ) (chordwindow).
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }

    std::string_view name() const override { return "the piano roll"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    std::optional<CommandResult> runLine(std::string_view command,
                                         std::string_view arguments) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override;
    void setCursor(const SongCursor& cursor) override;
    const core::MeterMap& cursorMeter() const override { return meter(); }
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, bool cursorFollows) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;

    // A page of bars: its first and last, in the track's bars, where it starts and ends in
    // time, and which of its section's pages it is. Pages start again at each section
    // (decided October 2026), so the last of a section's may be shorter.
    struct Page
    {
        std::int64_t firstBar = 1;
        std::int64_t lastBar = 1;
        core::Tick start = 0;
        core::Tick end = 0;
        std::size_t section = 0;
        std::int64_t number = 1; // within the section, counting from 1
        std::int64_t count = 1;  // in the section

        friend bool operator==(const Page&, const Page&) = default;
    };
    Page page() const; // the one shown

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

private:
    const core::MeterMap& meter() const;
    std::optional<core::TrackId> trackId() const;
    Page pageAt(core::Tick tick) const;
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

    // The track's name, or its number if it has none, for messages.
    std::string trackName() const;
    // Changes the track's settings as an undo step, merged with the step before if it has the
    // same merge key (see core::Document::edit).
    void changeTrack(std::string title, std::string_view mergeKey,
                     const std::function<void(core::Track&)>& change);
    void setGrid(core::NoteFraction grid);

    core::Document& document_;
    ViewHost& host_;
    std::size_t track_ = 0;
    core::Tick time_ = 0;
    std::uint8_t pitch_ = 60;
    std::uint8_t topPitch_ = 72;
    std::optional<core::Tick> barOffset_; // where in the bar { and } keep to, while repeated
    std::size_t pageSize_ = kDefaultPageSize;
    std::optional<core::Tick> playhead_;
    bool opened_ = false;                            // on a track yet, since the song was shown
    std::map<core::TrackId, std::uint8_t> pitches_;  // the pitch last used on each track
    std::map<core::TrackId, std::size_t> pageSizes_; // and the page size
    std::chrono::milliseconds chordWindow_{30};
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
