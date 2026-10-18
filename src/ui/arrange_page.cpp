#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/cell_grid.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <variant>

namespace modal::ui
{

namespace
{

constexpr Style kDim{Role::Dim, Role::Background, kPlain};
constexpr std::uint8_t kFaint = 96; // the opacity of marker lines across the tracks

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

// The end of a song: the end of its last note or event, or of its last marker, rounded up to
// the reference track's next bar line, and at least one bar.
core::Tick songEnd(const core::Song& song)
{
    core::Tick last = 0;
    for (const auto& track : song.tracks())
    {
        for (const auto& event : track->events.all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            last = std::max(last, event.tick + (note != nullptr ? note->duration : 0));
        }
    }
    for (const auto& marker : song.markers())
    {
        last = std::max(last, marker.tick + 1);
    }
    const auto& meter = song.referenceMeter();
    return last == 0 ? meter.barStart(2) : meter.barStart(meter.toBarBeatTick(last - 1).bar + 1);
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
    , end_(songEnd(song_))
{
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

std::int64_t ArrangePage::bar() const
{
    return currentTrack().meter.toBarBeatTick(time_).bar;
}

std::int64_t ArrangePage::lastBar(const core::Track& track) const
{
    return track.meter.toBarBeatTick(end_ - 1).bar;
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
    time_ =
        currentTrack().meter.barStart(std::clamp<std::int64_t>(bar, 1, lastBar(currentTrack())));
}

void ArrangePage::moveTracks(std::int64_t by)
{
    const auto last = static_cast<std::int64_t>(song_.tracks().size()) - 1;
    track_ = static_cast<std::size_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(track_) + by, 0, last));
}

bool ArrangePage::handle(const Key& key, int count)
{
    if (song_.tracks().empty())
    {
        return false;
    }
    const std::int64_t n = std::max(count, 1);
    const auto is = [&](char32_t c, NamedKey named)
    { return key == Key::typed(c) || (named != NamedKey::None && key == Key::of(named)); };
    const auto ctrl = [&](char32_t c) { return key == Key::typed(c, kCtrl); };
    const int halfPage = std::max(viewTracks() / 2, 1);
    const int page = std::max(viewTracks() - 2, 1);

    if (is(U'h', NamedKey::Left))
    {
        goToBar(bar() - n);
    }
    else if (is(U'l', NamedKey::Right))
    {
        goToBar(bar() + n);
    }
    else if (is(U'j', NamedKey::Down))
    {
        moveTracks(n);
    }
    else if (is(U'k', NamedKey::Up))
    {
        moveTracks(-n);
    }
    else if (is(U'0', NamedKey::Home))
    {
        goToBar(1);
    }
    else if (is(U'$', NamedKey::End))
    {
        goToBar(lastBar(currentTrack()));
    }
    else if (is(U'G', NamedKey::None))
    {
        goToBar(count > 0 ? count : lastBar(currentTrack()));
    }
    else if (ctrl(U'd') || ctrl(U'u'))
    {
        const auto by = n * halfPage * (ctrl(U'd') ? 1 : -1);
        topTrack_ = static_cast<std::size_t>(
            std::max<std::int64_t>(static_cast<std::int64_t>(topTrack_) + by, 0));
        moveTracks(by);
    }
    else if (ctrl(U'f') || ctrl(U'b') || key == Key::of(NamedKey::PageDown) ||
             key == Key::of(NamedKey::PageUp))
    {
        const bool down = ctrl(U'f') || key == Key::of(NamedKey::PageDown);
        const auto by = n * page * (down ? 1 : -1);
        topTrack_ = static_cast<std::size_t>(
            std::max<std::int64_t>(static_cast<std::int64_t>(topTrack_) + by, 0));
        moveTracks(by);
    }
    else if (is(U'+', NamedKey::None) || is(U'=', NamedKey::None))
    {
        zoom_ = zoom_ > 0 ? zoom_ - std::min<std::size_t>(static_cast<std::size_t>(n), zoom_) : 0;
    }
    else if (is(U'-', NamedKey::None))
    {
        zoom_ = std::min(zoom_ + static_cast<std::size_t>(n), kZooms.size() - 1);
    }
    else
    {
        return false;
    }
    follow();
    return true;
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
    const auto& meter = currentTrack().meter;
    const auto barNumber = meter.toBarBeatTick(time_).bar;
    const core::Tick start = meter.barStart(barNumber);
    const core::Tick barEnd = std::min(meter.barStart(barNumber + 1), end_);
    const core::Tick endColumns = (end_ + column - 1) / column;
    core::Tick left = std::clamp<core::Tick>(leftTick_ / column, 0,
                                             std::max<core::Tick>(endColumns - viewColumns(), 0));
    if (barEnd > (left * column) + visible)
    {
        left = (barEnd - visible + column - 1) / column;
    }
    left = std::min(left, start / column);
    leftTick_ = left * column;
}

void ArrangePage::addRegions(Frame& frame) const
{
    const int columns = (main_.width + cell_.width - 1) / cell_.width;
    const int rows = (main_.height + cell_.height - 1) / cell_.height;
    const auto tracks = song_.tracks();

    // The track headers, in text cells.
    CellGrid headers{columns, rows};
    headers.write(1, 0, song_.title, {Role::Accent, Role::Background, kBold});
    headers.write(0, kRulerRows - 1, "Trk Name", kDim);
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
    const int lineWidth = std::max(cell_.width / 7, 1);
    const core::Tick whole = core::NoteFraction{1, 1}.toTicks(song_.ppqn());
    // Gaps between bars while a column holds no more than a whole note; past that, bars are
    // too narrow for gaps, and the overall shape matters more than single bars.
    const int gap = column <= whole ? lineWidth : 0;
    const auto spaced = [&](core::Tick ticks)
    { return ticks * cell_.width >= kMinLineSpacing * column; };

    // The cursor's bar, as a span of time across every track, and boxed on its own track: its
    // sides in the gaps either side of the bar's block, its top and bottom above and below it.
    {
        const auto& meter = currentTrack().meter;
        const auto barNumber = meter.toBarBeatTick(time_).bar;
        const int x0 = scale.toX(meter.barStart(barNumber));
        const int x1 = scale.toX(std::min(meter.barStart(barNumber + 1), end_)) + gap;
        timeline.under.push_back(
            {{x0, tracksTop, x1 - x0, tracksBottom - tracksTop}, Role::Selection, 255});
        if (track_ >= topTrack_ && track_ < topTrack_ + static_cast<std::size_t>(shownTracks))
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
        const bool empty = track.events.empty();
        const auto& meter = track.meter;
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
                    const int dot = std::max(lineWidth, 2);
                    timeline.under.push_back(
                        {{x0, y + (cell_.height - dot) / 2, dot, dot}, Role::Dim, 255});
                }
                continue;
            }
            const auto level = density(track.events.startingIn(start, barEnd).size(),
                                       barEnd - start, song_.ppqn());
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

    // The ruler: markers by name, the reference track's meters, and its bars, numbered where
    // there's room for the numbers.
    // Markers: lines under the text and bars, solid beside their names and faint across the
    // tracks so as not to distract, leaving the meters and bar numbers clear between; named
    // up to the next marker, cut short with an ellipsis or left out if there's no room.
    const auto markers = song_.markers();
    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        const auto tick = markers[i].tick;
        if (tick < leftTick_ || tick >= shownEnd)
        {
            continue;
        }
        const int x = scale.toX(tick);
        timeline.under.push_back({{x, 0, lineWidth, cell_.height}, Role::Marker, 255});
        timeline.under.push_back(
            {{x, tracksTop, lineWidth, tracksBottom - tracksTop}, Role::Marker, kFaint});
        const int textX = x + lineWidth + 2;
        int room = timeline.bounds.width; // characters, to the next marker or past the edge
        if (i + 1 < markers.size())
        {
            room = (scale.toX(markers[i + 1].tick) - textX - 2) / cell_.width;
        }
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
    const auto& reference = song_.referenceMeter();
    Labels meterLabels{timeline, cell_.height, cell_};
    for (const auto& change : reference.changes())
    {
        if (change.tick >= leftTick_ && change.tick < shownEnd)
        {
            meterLabels.add(scale.toX(change.tick) + 2, signatureText(change.signature), kDim);
        }
    }
    // Bar numbers on bars 1, 1 + n, 1 + 2n... for n of 1, 2, 4, 8..., as few as fit the
    // widest number and a space into n of the shortest bar, so none crowd the next.
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
    Labels numbers{timeline, 2 * cell_.height, cell_};
    for (auto bar = reference.toBarBeatTick(leftTick_).bar;; ++bar)
    {
        const core::Tick start = reference.barStart(bar);
        if (start >= shownEnd)
        {
            break;
        }
        if ((bar - 1) % every == 0)
        {
            numbers.add(scale.toX(start), std::to_string(bar), {});
        }
    }
    // The end of the song.
    if (end_ < right)
    {
        timeline.under.push_back({{scale.toX(end_), tracksTop, lineWidth, tracksBottom - tracksTop},
                                  Role::BarLine,
                                  255});
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
           std::to_string(lastBar(track)) + "  " + signatureText(track.meter.signatureAt(time_)) +
           "  zoom " + core::toString(kZooms[zoom_]);
}

} // namespace modal::ui
