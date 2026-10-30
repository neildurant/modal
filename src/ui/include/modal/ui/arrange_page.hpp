// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/note_fraction.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace modal::ui
{

// The arrangement's tracks and ruler, first sketched in milestone 3's test view and drawn by the
// arrangement view (see ArrangeView). A row for each track, with its number and name in text cells,
// then its bars as blocks on one time scale, so a bar's block is as wide as the bar is long: shaded
// by how busy the bar is, with a gap before each while bars are wide enough, and a faint line for a
// bar with no notes. Section starts and markers are faint lines across every track. The ruler shows
// them by name, and the conductor's meters and bar numbers.
class ArrangePage
{
public:
    // The rows above the tracks: markers' names, the conductor's meters, bar numbers, and the
    // section strip.
    static constexpr int kRulerRows = 4;
    static constexpr int kStripRow = 3;
    static constexpr int kHeaderColumns = 20; // the track's number and name, and a line
    static constexpr int kNameColumns = 14;

    // The length of time in a column of cells at each zoom, from zoomed in to out.
    static constexpr std::array<core::NoteFraction, 7> kZooms{
        core::NoteFraction{1, 16}, core::NoteFraction{1, 8}, core::NoteFraction{1, 4},
        core::NoteFraction{1, 2},  core::NoteFraction{1, 1}, core::NoteFraction{2, 1},
        core::NoteFraction{4, 1}};
    static constexpr std::size_t kDefaultZoom = 2; // a quarter note to a column

    explicit ArrangePage(core::Song song);

    // Shows the song as it is now, after a change, keeping the cursor's track and time as far
    // as they still exist.
    void setSong(core::Song song);

    // Puts the cursor on a track (counting from 0) at a time, on the bar holding it, keeping
    // it in view.
    void setCursor(std::size_t track, core::Tick time);

    // The playhead, drawn as a line through the ruler and tracks, or none; and keeping a time
    // in view, a third of the way across once it's moving through, as the playhead is. The
    // view starts at a column, or smoothly for the playhead, exactly a third of the way back,
    // so the view glides under it rather than jumping a column at a time, which made the
    // playhead jump back with it.
    void setPlayhead(std::optional<core::Tick> playhead) { playhead_ = playhead; }
    void showTime(core::Tick time, bool smooth = false);

    // The view's state, for going back to where a change was made: the top track, the time at
    // the left and the zoom.
    void setView(std::size_t topTrack, core::Tick leftTick, std::size_t zoom);

    // The cursor on the section strip, on a section (counting from 0) rather than a bar of a
    // track; or none, back on the tracks. The view keeps the section in view.
    void setStripCursor(std::optional<std::size_t> section);
    std::optional<std::size_t> stripCursor() const { return stripCursor_; }

    // Fits the page to the main area, scrolling if the cursor would be out of view.
    void resize(Rect main, CellSize cell);

    // The cursor moves by the current track's bars, and between tracks keeping its time, so
    // it lands on the bar there that holds it. Each of these keeps the cursor in view.
    void moveBars(std::int64_t by);
    void moveTracks(std::int64_t by);
    void goToBar(std::int64_t bar); // on the current track, counting from 1
    void goToLastBar();

    // Scrolls the tracks by some rows, down if positive, and moves the cursor as far, as
    // Vim's Ctrl-D does.
    void scrollTracks(std::int64_t by);

    // Moves the cursor by some of the current track's bars, right if positive, and scrolls
    // the view by as long in time, so the cursor stays exactly where it is on screen; at the
    // start or end of the song the view stops and the cursor moves on. The view can then
    // start anywhere, not only at a column.
    void scrollBars(std::int64_t by);
    // How many of the current track's bars are fully in view, at least one: how far a page
    // scrolls.
    std::int64_t barsInView() const;

    // Zooms out by some steps, or in if negative, as far as the zooms go; or back to the
    // default.
    void zoomBy(int steps);
    void resetZoom();

    // Scrolls to put the cursor's bar and track in the middle of the view, as far as the
    // song allows.
    void centre();

    // How many tracks' rows are fully in view.
    int viewTracks() const;

    // The tracks and ruler, with the play-from marker in the ruler if there's one.
    void addRegions(Frame& frame, std::optional<core::Tick> playFrom = std::nullopt) const;

    // The cursor's track, bar (of the track's last) and meter, and the length of time in a
    // column, for the status line.
    std::string status() const;

    std::size_t track() const { return track_; }
    std::int64_t bar() const; // on the current track, counting from 1
    core::Tick time() const { return time_; }
    std::size_t topTrack() const { return topTrack_; }
    core::Tick leftTick() const { return leftTick_; }
    std::size_t zoom() const { return zoom_; }
    core::Tick end() const { return end_; }

    // How busy a stretch of a track is, from the notes starting in it: 0 for none, then 1 to
    // 4 for under 3/4 of a note to a quarter note, under 1.5, under 3, and more.
    static int density(std::size_t notes, core::Tick length, core::Ppqn ppqn);

private:
    const core::Track& currentTrack() const;
    const core::MeterMap& meterOf(const core::Track& track) const;
    std::int64_t lastBar(const core::Track& track) const;
    void follow();
    core::Tick columnTicks() const;
    int viewColumns() const;

    core::Song song_;
    std::optional<core::Tick> playhead_;
    std::optional<std::size_t> stripCursor_;
    core::Tick end_ = 0;
    std::size_t track_ = 0;
    core::Tick time_ = 0; // the cursor's place in time, kept as it moves between tracks
    std::size_t topTrack_ = 0;
    core::Tick leftTick_ = 0;
    std::size_t zoom_ = kDefaultZoom;
    Rect main_;
    CellSize cell_{1, 1};
};

} // namespace modal::ui
