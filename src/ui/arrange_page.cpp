// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr Style kDim{Role::Dim, Role::Background, kPlain};
constexpr std::uint8_t kFaint = 96; // the opacity of marker lines

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

// Text placed at pixel positions along a row, left to right, leaving out any that would
// overlap the one before.
class Labels
{
public:
    Labels(Region& region, int y, CellSize cell)
        : region_(region)
        , y_(y)
        , cell_(cell)
    {
    }

    void add(int x, const std::string& text, Style style)
    {
        if (x < next_)
        {
            return;
        }
        auto characters = decodeUtf8(text);
        next_ = x + static_cast<int>(characters.size() + 1) * cell_.width;
        region_.text.push_back({x, y_, std::move(characters), style});
    }

private:
    Region& region_;
    int y_;
    CellSize cell_;
    int next_ = -1'000'000;
};

} // namespace

ArrangePage::ArrangePage(core::Song song)
    : song_(std::move(song))
    , end_(song_.end())
{
}

void ArrangePage::setSong(core::Song song)
{
    song_ = std::move(song);
    end_ = song_.end();
    track_ = song_.tracks().empty() ? 0 : std::min(track_, song_.tracks().size() - 1);
    time_ = std::clamp<core::Tick>(time_, 0, std::max<core::Tick>(end_ - 1, 0));
    if (stripCursor_)
    {
        stripCursor_ = std::min(*stripCursor_, song_.sections().size() - 1);
    }
    follow();
}

void ArrangePage::setCursor(std::size_t track, core::Tick time)
{
    track_ = song_.tracks().empty() ? 0 : std::min(track, song_.tracks().size() - 1);
    time_ = std::clamp<core::Tick>(time, 0, std::max<core::Tick>(end_ - 1, 0));
    follow();
}

void ArrangePage::showTime(core::Tick time)
{
    const core::Tick column = columnTicks();
    const core::Tick visible = viewColumns() * column;
    if (time >= leftTick_ && time <= leftTick_ + visible / 3)
    {
        return;
    }
    const core::Tick endColumns = (end_ + column - 1) / column;
    const core::Tick lastLeft = std::max<core::Tick>(endColumns - viewColumns(), 0) * column;
    leftTick_ = std::clamp<core::Tick>(
        std::max<core::Tick>(time - visible / 3, 0) / column * column, 0, lastLeft);
}

void ArrangePage::setStripCursor(std::optional<std::size_t> section)
{
    stripCursor_ = section;
    if (section)
    {
        stripCursor_ = std::min(*section, song_.sections().size() - 1);
        time_ = song_.sectionStart(*stripCursor_);
        showTime(time_);
    }
}

void ArrangePage::setView(std::size_t topTrack, core::Tick leftTick, std::size_t zoom)
{
    zoom_ = std::min(zoom, kZooms.size() - 1);
    topTrack_ = topTrack;
    leftTick_ = std::max<core::Tick>(leftTick, 0);
    follow();
}

int ArrangePage::density(std::size_t notes, core::Tick length, core::Ppqn ppqn)
{
    if (notes == 0 || length <= 0)
    {
        return 0;
    }
    // In notes to a quarter note, times four, against the thresholds times four.
    const core::Tick perQuarter = static_cast<core::Tick>(notes) * ppqn.quarter() * 4;
    return perQuarter < 3 * length    ? 1
           : perQuarter < 6 * length  ? 2
           : perQuarter < 12 * length ? 3
                                      : 4;
}

void ArrangePage::resize(Rect main, CellSize cell)
{
    main_ = main;
    cell_ = {std::max(cell.width, 1), std::max(cell.height, 1)};
    follow();
}

const core::Track& ArrangePage::currentTrack() const
{
    return *song_.tracks()[track_];
}

const core::MeterMap& ArrangePage::meterOf(const core::Track& track) const
{
    return song_.meter(track.id());
}

std::int64_t ArrangePage::bar() const
{
    return meterOf(currentTrack()).toBarBeatTick(time_).bar;
}

std::int64_t ArrangePage::lastBar(const core::Track& track) const
{
    return meterOf(track).toBarBeatTick(end_ - 1).bar;
}

core::Tick ArrangePage::columnTicks() const
{
    return std::max<core::Tick>(kZooms[zoom_].toTicks(song_.ppqn()), 1);
}

int ArrangePage::viewTracks() const
{
    return std::max(main_.height / cell_.height - kRulerRows, 1);
}

int ArrangePage::viewColumns() const
{
    return std::max(main_.width / cell_.width - kHeaderColumns, 1);
}

void ArrangePage::goToBar(std::int64_t bar)
{
    if (song_.tracks().empty())
    {
        return;
    }
    time_ =
        meterOf(currentTrack()).barStart(std::clamp<std::int64_t>(bar, 1, lastBar(currentTrack())));
    follow();
}

void ArrangePage::moveTracks(std::int64_t by)
{
    if (song_.tracks().empty())
    {
        return;
    }
    const auto last = static_cast<std::int64_t>(song_.tracks().size()) - 1;
    track_ = static_cast<std::size_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(track_) + by, 0, last));
    follow();
}

void ArrangePage::moveBars(std::int64_t by)
{
    if (!song_.tracks().empty())
    {
        goToBar(bar() + by);
    }
}

void ArrangePage::goToLastBar()
{
    if (!song_.tracks().empty())
    {
        goToBar(lastBar(currentTrack()));
    }
}

void ArrangePage::scrollTracks(std::int64_t by)
{
    topTrack_ = static_cast<std::size_t>(
        std::max<std::int64_t>(static_cast<std::int64_t>(topTrack_) + by, 0));
    moveTracks(by);
}

void ArrangePage::zoomBy(int steps)
{
    const auto last = static_cast<std::int64_t>(kZooms.size()) - 1;
    zoom_ = static_cast<std::size_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(zoom_) + steps, 0, last));
    leftTick_ = leftTick_ / columnTicks() * columnTicks(); // to a column of the new zoom
    follow();
}

void ArrangePage::resetZoom()
{
    zoomBy(static_cast<int>(kDefaultZoom) - static_cast<int>(zoom_));
}

void ArrangePage::scrollBars(std::int64_t by)
{
    if (song_.tracks().empty())
    {
        return;
    }
    const auto& meter = meterOf(currentTrack());
    const core::Tick from = meter.barStart(bar());
    const core::Tick to =
        meter.barStart(std::clamp<std::int64_t>(bar() + by, 1, lastBar(currentTrack())));
    time_ = to;
    leftTick_ = std::max<core::Tick>(leftTick_ + (to - from), 0);
    follow();
}

void ArrangePage::centre()
{
    if (song_.tracks().empty())
    {
        return;
    }
    topTrack_ = static_cast<std::size_t>(
        std::max<std::int64_t>(static_cast<std::int64_t>(track_) - viewTracks() / 2, 0));
    const auto& meter = meterOf(currentTrack());
    const auto barNumber = meter.toBarBeatTick(time_).bar;
    const core::Tick middle =
        (meter.barStart(barNumber) + std::min(meter.barStart(barNumber + 1), end_)) / 2;
    const core::Tick column = columnTicks();
    const core::Tick left = std::max<core::Tick>(middle - viewColumns() * column / 2, 0);
    leftTick_ = left / column * column;
    follow(); // and back from past the song's end
}

void ArrangePage::follow()
{
    if (song_.tracks().empty())
    {
        return;
    }
    // Tracks: scroll only as far as needed to keep the cursor's track in view, and no
    // further than the last track.
    const auto tracks = static_cast<std::int64_t>(song_.tracks().size());
    const std::int64_t rows = viewTracks();
    auto top = std::clamp<std::int64_t>(static_cast<std::int64_t>(topTrack_), 0,
                                        std::max<std::int64_t>(tracks - rows, 0));
    const auto current = static_cast<std::int64_t>(track_);
    top = std::clamp(top, current - rows + 1, current);
    topTrack_ = static_cast<std::size_t>(top);

    // Time, in whole columns: keep the cursor's bar fully in view, or its start if it's
    // longer than the view, and don't show more than needed after the end.
    const core::Tick column = columnTicks();
    const core::Tick visible = viewColumns() * column;
    const auto& meter = meterOf(currentTrack());
    const auto barNumber = meter.toBarBeatTick(time_).bar;
    const core::Tick start = meter.barStart(barNumber);
    const core::Tick barEnd = std::min(meter.barStart(barNumber + 1), end_);
    // The view may start anywhere, as scrollBars leaves it; when it must move to show the
    // cursor's bar, it moves to a column.
    const core::Tick endColumns = (end_ + column - 1) / column;
    const core::Tick lastLeft = std::max<core::Tick>(endColumns - viewColumns(), 0) * column;
    core::Tick left = std::clamp<core::Tick>(leftTick_, 0, lastLeft);
    if (barEnd > left + visible)
    {
        left = (barEnd - visible + column - 1) / column * column;
    }
    if (start < left)
    {
        left = start / column * column;
    }
    leftTick_ = left;
}

void ArrangePage::addRegions(Frame& frame, std::optional<core::Tick> playFrom) const
{
    const int columns = (main_.width + cell_.width - 1) / cell_.width;
    const int rows = (main_.height + cell_.height - 1) / cell_.height;
    const auto tracks = song_.tracks();

    // The track headers, in text cells.
    CellGrid headers{columns, rows};
    headers.write(1, 0, song_.title, {Role::Accent, Role::Background, kBold});
    headers.write(0, kRulerRows - 2, "Trk Name", kDim);
    headers.write(0, kStripRow, "Sections",
                  stripCursor_ ? Style{Role::CursorText, Role::Cursor, kBold} : kDim);
    for (int row = 0; row < rows; ++row)
    {
        headers.set(kHeaderColumns - 1, row, {U'│', kDim});
    }
    for (int line = 0; line < rows - kRulerRows; ++line)
    {
        const auto index = topTrack_ + static_cast<std::size_t>(line);
        if (index >= tracks.size())
        {
            break;
        }
        const auto& track = *tracks[index];
        const int row = kRulerRows + line;
        auto number = std::to_string(index + 1);
        number.insert(0, 3 - std::min<std::size_t>(number.size(), 3), ' ');
        headers.write(0, row, number,
                      index == track_ ? Style{Role::CursorText, Role::Cursor, kBold} : Style{});
        if (track.name.empty())
        {
            headers.write(4, row, std::string(kNameColumns, '-'), kDim);
        }
        else
        {
            headers.write(4, row, track.name.substr(0, kNameColumns), {});
        }
    }
    frame.push_back(gridRegion(headers, main_, cell_, Role::Background));

    // The timeline, in pixels.
    const int timelineX = kHeaderColumns * cell_.width;
    Region timeline{.bounds = {main_.x + timelineX, main_.y, std::max(main_.width - timelineX, 0),
                               main_.height},
                    .background = Role::Background,
                    .text = {},
                    .under = {},
                    .over = {}};
    if (tracks.empty())
    {
        frame.push_back(std::move(timeline));
        return;
    }
    const core::Tick column = columnTicks();
    const TimeScale scale{.start = leftTick_, .length = column, .x = 0, .width = cell_.width};
    const core::Tick right = leftTick_ + (timeline.bounds.width / cell_.width + 1) * column;
    const core::Tick shownEnd = std::min(right, end_);
    const int tracksTop = kRulerRows * cell_.height;
    const int shownTracks = static_cast<int>(std::min<std::size_t>(
        tracks.size() - topTrack_, static_cast<std::size_t>(rows - kRulerRows)));
    const int tracksBottom = tracksTop + shownTracks * cell_.height;
    const int lineWidth = hairline(cell_); // also the space beside a line
    const core::Tick whole = core::NoteFraction{1, 1}.toTicks(song_.ppqn());
    // Gaps between bars while a column holds no more than a whole note; past that, bars are
    // too narrow for gaps, and the overall shape matters more than single bars.
    const int gap = column <= whole ? lineWidth : 0;
    const auto spaced = [&](core::Tick ticks)
    { return ticks * cell_.width >= minLineSpacing(cell_) * column; };

    // The cursor's bar, as a span of time across every track, and boxed on its own track: its
    // sides in the gaps either side of the bar's block, its top and bottom above and below it.
    {
        const auto& meter = meterOf(currentTrack());
        const auto barNumber = meter.toBarBeatTick(time_).bar;
        const int x0 = scale.toX(meter.barStart(barNumber));
        const int x1 = scale.toX(std::min(meter.barStart(barNumber + 1), end_)) + gap;
        timeline.under.push_back(
            {{x0, tracksTop, x1 - x0, tracksBottom - tracksTop}, Role::Selection, 255});
        if (!stripCursor_ && track_ >= topTrack_ &&
            track_ < topTrack_ + static_cast<std::size_t>(shownTracks))
        {
            const int y = tracksTop + static_cast<int>(track_ - topTrack_) * cell_.height;
            const int side = std::max(gap, 1);
            const int edge = std::max(cell_.height / 10, 1);
            const int width = std::max(x1 - x0, 2 * side);
            timeline.over.push_back({{x0, y, width, edge}, Role::Cursor, 255});
            timeline.over.push_back(
                {{x0, y + cell_.height - edge, width, edge}, Role::Cursor, 255});
            timeline.over.push_back({{x0, y, side, cell_.height}, Role::Cursor, 255});
            timeline.over.push_back(
                {{x0 + width - side, y, side, cell_.height}, Role::Cursor, 255});
        }
    }

    // Each track's bars.
    const int inset = cell_.height / 5;
    for (int line = 0; line < shownTracks; ++line)
    {
        const auto& track = *tracks[topTrack_ + static_cast<std::size_t>(line)];
        const int y = tracksTop + line * cell_.height;
        const auto& events = song_.events(track.id());
        const bool empty = events.empty();
        const auto& meter = meterOf(track);
        for (auto bar = meter.toBarBeatTick(leftTick_).bar;; ++bar)
        {
            const core::Tick start = meter.barStart(bar);
            if (start >= shownEnd)
            {
                break;
            }
            const core::Tick barEnd = std::min(meter.barStart(bar + 1), end_);
            const int x0 = scale.toX(start) + gap;
            const int width = std::max(scale.toX(barEnd) - x0, 1);
            if (empty)
            {
                if (spaced(barEnd - start))
                {
                    // On the bar line, where the marker lines are.
                    const int dot = 2 * lineWidth;
                    timeline.under.push_back(
                        {{scale.toX(start), y + (cell_.height - dot) / 2, dot, dot},
                         Role::Dim,
                         255});
                }
                continue;
            }
            const auto level =
                density(events.startingIn(start, barEnd).size(), barEnd - start, song_.ppqn());
            if (level == 0)
            {
                const int thickness = std::max(cell_.height / 16, 1);
                timeline.under.push_back(
                    {{x0, y + (cell_.height - thickness) / 2, width, thickness},
                     Role::BeatLine,
                     255});
            }
            else
            {
                const auto role = static_cast<Role>(static_cast<int>(Role::Density1) + level - 1);
                timeline.under.push_back(
                    {{x0, y + inset, width, cell_.height - 2 * inset}, role, 255});
            }
        }
    }

    // The ruler: markers by name, the conductor's meters, and its bars, numbered where there's
    // room for the numbers; then the section strip.
    // Section starts and markers: faint lines under the text and bars, so as not to distract,
    // through the ruler and across the tracks. Markers are named up to the next line, cut
    // short with an ellipsis or left out if there's no room; sections are named on the strip.
    // A section's start wins over a marker at the same tick. The play-from marker sits in the
    // same row (decided October 2026, out of the bar numbers' way): a name stops short of it,
    // or starts after it if it's at the name's start.
    const std::optional<int> playX = playFrom && *playFrom >= leftTick_ && *playFrom < right
                                         ? std::optional{scale.toX(*playFrom)}
                                         : std::nullopt;
    std::vector<core::Marker> markers;
    for (std::size_t i = 0; i < song_.sections().size(); ++i)
    {
        markers.push_back({song_.sectionStart(i), {}}); // named on the strip
    }
    for (const auto& marker : song_.markers())
    {
        if (std::ranges::find(markers, marker.tick, &core::Marker::tick) == markers.end())
        {
            markers.push_back(marker);
        }
    }
    std::ranges::stable_sort(markers, {}, &core::Marker::tick);
    std::vector<int> markerLines; // drawn through the ruler once its text is placed
    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        const auto tick = markers[i].tick;
        if (tick < leftTick_ || tick >= shownEnd)
        {
            continue;
        }
        const int x = scale.toX(tick);
        markerLines.push_back(x);
        timeline.under.push_back(
            {{x, tracksTop, lineWidth, tracksBottom - tracksTop}, Role::Marker, kFaint});
        if (markers[i].name.empty())
        {
            continue; // a section's start
        }
        int textX = x + 2 * lineWidth;
        // Characters, to the next marker or the play-from marker, or past the edge.
        int roomEnd = textX + timeline.bounds.width * cell_.width;
        if (i + 1 < markers.size())
        {
            roomEnd = scale.toX(markers[i + 1].tick) - lineWidth;
        }
        if (playX && *playX < textX + cell_.width && *playX + cell_.width > x)
        {
            textX = *playX + cell_.width;
        }
        else if (playX && *playX >= textX)
        {
            roomEnd = std::min(roomEnd, *playX);
        }
        const int room = (roomEnd - textX) / cell_.width;
        auto name = decodeUtf8(markers[i].name);
        if (static_cast<int>(name.size()) > room)
        {
            if (room < kMinSegmentCharacters + 1)
            {
                continue;
            }
            name.resize(static_cast<std::size_t>(room - 1));
            name += U'\u2026';
        }
        timeline.text.push_back(
            {textX, 0, std::move(name), {Role::Marker, Role::Background, kBold}});
    }
    // The conductor's meter changes, each named if the name fits before the next
    // change, or else shown by a tick, so a short change, such as a single bar, never hides
    // the one after it.
    const auto& reference = song_.meter();
    const auto changes = reference.changes();
    for (std::size_t i = 0; i < changes.size(); ++i)
    {
        const auto tick = changes[i].tick;
        if (tick < leftTick_ || tick >= shownEnd)
        {
            continue;
        }
        const int x = scale.toX(tick);
        auto text = signatureText(changes[i].signature);
        const int room = i + 1 < changes.size() ? scale.toX(changes[i + 1].tick) - x - 4 * lineWidth
                                                : timeline.bounds.width;
        if (static_cast<int>(text.size()) * cell_.width <= room)
        {
            timeline.text.push_back({x + 2 * lineWidth, cell_.height, decodeUtf8(text), kDim});
        }
        else
        {
            const int height = cell_.height / 3;
            timeline.under.push_back(
                {{x, cell_.height + height, lineWidth, height}, Role::Dim, 255});
        }
    }
    // Bar numbers, counted in sections: bar 1 and the bar of each section start and marker
    // start one. Each
    // section's first bar is numbered, then every n bars into it, for n of 1, 2, 4, 8..., as
    // few as fit the widest number and a space into n of the shortest bar. A number shows
    // only if it fits before the next section's, so the sections' numbers come first.
    core::Tick shortest = reference.barStart(2);
    for (const auto& change : reference.changes())
    {
        shortest = std::min(shortest, change.signature.barLength().toTicks(song_.ppqn()));
    }
    const auto lastNumber = std::to_string(reference.toBarBeatTick(end_ - 1).bar);
    const core::Tick label = static_cast<core::Tick>(lastNumber.size() + 1) * cell_.width;
    std::int64_t every = 1;
    while (every < 1024 && every * shortest * cell_.width < label * column)
    {
        every *= 2;
    }
    std::vector<std::int64_t> sections{1};
    for (const auto& marker : markers)
    {
        sections.push_back(reference.toBarBeatTick(marker.tick).bar);
    }
    std::ranges::sort(sections);
    const auto [last, unique] = std::ranges::unique(sections);
    sections.erase(last, unique);

    Labels numbers{timeline, 2 * cell_.height, cell_};
    for (auto bar = reference.toBarBeatTick(leftTick_).bar;; ++bar)
    {
        const core::Tick start = reference.barStart(bar);
        if (start >= shownEnd)
        {
            break;
        }
        const auto next = std::ranges::upper_bound(sections, bar);
        const auto section = *std::prev(next);
        if ((bar - section) % every != 0)
        {
            continue;
        }
        const auto text = std::to_string(bar);
        const int x = scale.toX(start) + 2 * lineWidth;
        const bool fits =
            next == sections.end() ||
            x + static_cast<int>(text.size()) * cell_.width < scale.toX(reference.barStart(*next));
        if (fits)
        {
            numbers.add(x, text, {});
        }
    }
    // The markers' lines through the ruler, a row at a time, broken only where a label of the
    // row sits across a line, so the line never runs through text.
    for (const int x : markerLines)
    {
        for (int row = 0; row < kStripRow; ++row)
        {
            const int y = row * cell_.height;
            const bool covered =
                std::ranges::any_of(timeline.text,
                                    [&](const TextRun& run)
                                    {
                                        const int right =
                                            run.x + static_cast<int>(run.text.size()) * cell_.width;
                                        return run.y == y && run.x < x + lineWidth && right > x;
                                    });
            if (!covered)
            {
                timeline.under.push_back({{x, y, lineWidth, cell_.height}, Role::Marker, kFaint});
            }
        }
    }
    // The section strip: each section a block across its span, neighbours in two shades, named
    // as far as there's room; the cursor's section boxed when the cursor is on the strip.
    {
        const int y = kStripRow * cell_.height;
        const int stripInset = std::max(cell_.height / 10, 1);
        for (std::size_t i = 0; i < song_.sections().size(); ++i)
        {
            const core::Tick start = song_.sectionStart(i);
            const core::Tick finish = song_.sectionStart(i + 1);
            if (finish <= leftTick_ || start >= right)
            {
                continue;
            }
            const int x0 = std::max(scale.toX(start), 0) + gap;
            const int x1 = scale.toX(std::min(finish, right));
            if (x1 <= x0)
            {
                continue;
            }
            timeline.under.push_back({{x0, y + stripInset, x1 - x0, cell_.height - 2 * stripInset},
                                      i % 2 == 0 ? Role::Panel : Role::Bar,
                                      255});
            const int room = (x1 - x0 - 2 * lineWidth) / cell_.width;
            auto name = decodeUtf8(song_.sections()[i]->name());
            if (static_cast<int>(name.size()) > room)
            {
                if (room < kMinSegmentCharacters + 1)
                {
                    name.clear();
                }
                else
                {
                    name.resize(static_cast<std::size_t>(room - 1));
                    name += U'…';
                }
            }
            if (!name.empty())
            {
                const bool cursor = stripCursor_ == i;
                timeline.text.push_back({x0 + lineWidth,
                                         y,
                                         std::move(name),
                                         {Role::Text, Role::Background, cursor ? kBold : kPlain}});
            }
            if (stripCursor_ == i)
            {
                const int edge = std::max(cell_.height / 10, 1);
                const int side = std::max(gap, 1);
                timeline.over.push_back({{x0, y, x1 - x0, edge}, Role::Cursor, 255});
                timeline.over.push_back(
                    {{x0, y + cell_.height - edge, x1 - x0, edge}, Role::Cursor, 255});
                timeline.over.push_back({{x0, y, side, cell_.height}, Role::Cursor, 255});
                timeline.over.push_back({{x1 - side, y, side, cell_.height}, Role::Cursor, 255});
            }
        }
    }
    // The end of the song.
    if (end_ < right)
    {
        timeline.under.push_back({{scale.toX(end_), tracksTop, lineWidth, tracksBottom - tracksTop},
                                  Role::BarLine,
                                  255});
    }
    // The play-from marker, in the markers' row, and the playhead, a line through the ruler and
    // the tracks.
    if (playX)
    {
        timeline.text.push_back({*playX, 0, U"▶", {Role::Accent, Role::Background, kPlain}});
    }
    if (playhead_ && *playhead_ >= leftTick_ && *playhead_ < right)
    {
        timeline.over.push_back(
            {{scale.toX(*playhead_), 0, 2 * lineWidth, tracksBottom}, Role::Accent, 255});
    }
    frame.push_back(std::move(timeline));
}

std::string ArrangePage::status() const
{
    if (song_.tracks().empty())
    {
        return "no tracks";
    }
    const auto& track = currentTrack();
    return "track " + std::to_string(track_ + 1) + "  bar " + std::to_string(bar()) + "/" +
           std::to_string(lastBar(track)) + "  " +
           signatureText(meterOf(track).signatureAt(time_)) + "  zoom " +
           core::toString(kZooms[zoom_]);
}

} // namespace modal::ui
