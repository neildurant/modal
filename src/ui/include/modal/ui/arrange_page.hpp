#pragma once

#include <modal/core/note_fraction.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace modal::ui
{

// The test view's arrangement page: a sketch of the arrangement (milestone 6), to try out
// how it's drawn. A row for each track, with its number, name and the meter at the cursor in
// text cells, then its bars as blocks on one time scale, so a bar's block is as wide as the
// bar is long: shaded by how busy the bar is, with a gap before each while bars are wide
// enough, and a thin line for a bar with no notes. Markers are lines across every track.
// The ruler shows the markers by name, and the reference track's meters and bar numbers.
class ArrangePage
{
public:
    static constexpr int kRulerRows = 3;
    static constexpr int kHeaderColumns = 26; // the track's number, name and meter, and a line
    static constexpr int kNameColumns = 14;

    // The length of time in a column of cells at each zoom, from zoomed in to out.
    static constexpr std::array<core::NoteFraction, 7> kZooms{
        core::NoteFraction{1, 16}, core::NoteFraction{1, 8}, core::NoteFraction{1, 4},
        core::NoteFraction{1, 2},  core::NoteFraction{1, 1}, core::NoteFraction{2, 1},
        core::NoteFraction{4, 1}};
    static constexpr std::size_t kDefaultZoom = 3;

    explicit ArrangePage(core::Song song);

    // Fits the page to the main area, scrolling if the cursor would be out of view.
    void resize(Rect main, CellSize cell);

    // Handles a key with a count (0 for none): h and l (or the arrow keys) by the current
    // track's bars, j and k between tracks at the same time, 0 and $ to the first and last
    // bar, G to the last bar or {count}G to a bar of the current track, Ctrl-D, Ctrl-U,
    // Ctrl-F and Ctrl-B through the tracks, and + and - to zoom. Returns whether it was one
    // of these.
    bool handle(const Key& key, int count);

    void addRegions(Frame& frame) const;

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
    std::int64_t lastBar(const core::Track& track) const;
    void goToBar(std::int64_t bar);
    void moveTracks(std::int64_t by);
    void follow();
    core::Tick columnTicks() const;
    int viewTracks() const;
    int viewColumns() const;

    core::Song song_;
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
