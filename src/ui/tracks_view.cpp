// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>
#include <modal/ui/tracks_view.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/view_state.hpp>

#include <algorithm>

namespace modal::ui
{

TracksView::TracksView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
{
    showSong();
}

std::vector<int> TracksView::topHeights(CellSize cell) const
{
    return {cell.height, inspectorHeight(cell)};
}

void TracksView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = cell;
    ensureVisible();
}

std::size_t TracksView::row() const
{
    const auto tracks = document_.song().tracks();
    const auto found =
        std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == cursor_.track; });
    return found == tracks.end() ? 0 : static_cast<std::size_t>(found - tracks.begin());
}

void TracksView::go(std::size_t index)
{
    const auto tracks = document_.song().tracks();
    cursor_.track = tracks.empty()
                        ? std::nullopt
                        : std::optional{tracks[std::min(index, tracks.size() - 1)]->id()};
    ensureVisible();
}

void TracksView::ensureVisible()
{
    const auto rows = static_cast<std::size_t>(std::max(layout_.fullRows - 1, 1));
    const auto at = row();
    top_ = std::min(top_, at);
    if (at >= top_ + rows)
    {
        top_ = at - rows + 1;
    }
    if (layout_.fullColumns <= 0)
        return;
    int x = 0;
    const auto columns = trackColumns();
    column_ = std::min(column_, columns.size() - 1);
    for (std::size_t i = 0; i < column_; ++i)
    {
        x += columns[i].width;
    }
    const int room = std::max(layout_.fullColumns - 2, 1);
    if (x < left_)
    {
        left_ = x;
    }
    if (x + columns[column_].width > left_ + room)
    {
        left_ = std::max(0, x + columns[column_].width - room);
    }
}

void TracksView::setCursor(const SongCursor& cursor)
{
    cursor_ = cursor;
    go(row());
}
void TracksView::refresh()
{
    go(row());
}
void TracksView::showSong()
{
    cursor_ = {};
    top_ = 0;
    left_ = 0;
    column_ = 1;
    editor_.close();
    go(0);
}
void TracksView::showPlayhead(core::Tick, Follow) {}
void TracksView::stopPlayhead(std::optional<core::Tick>) {}

const core::MeterMap& TracksView::cursorMeter() const
{
    return cursor_.track ? document_.song().meter(*cursor_.track) : document_.song().meter();
}
std::string TracksView::position(core::Tick tick) const
{
    return positionText(cursorMeter(), tick);
}
std::string TracksView::timeStatus(core::Tick tick) const
{
    return position(tick);
}
std::string TracksView::cursorStatus() const
{
    return document_.song().tracks().empty()
               ? "No tracks"
               : "Track " + std::to_string(row() + 1) + "  " + position(cursor_.tick);
}

core::EditPlace TracksView::place() const
{
    return placeWith(cursor_.track, cursor_.tick, TracksState{top_, column_, left_, cursor_.pitch});
}
void TracksView::restore(const core::EditPlace& place)
{
    cursor_.track = place.track;
    cursor_.tick = place.tick;
    if (const auto state = tracksStateOf(place))
    {
        top_ = state->top;
        column_ = state->column;
        left_ = state->left;
        cursor_.pitch = state->pitch;
    }
    refresh();
}

std::vector<core::TrackId> TracksView::selected() const
{
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return {};
    }
    auto first = row();
    auto last = first;
    if (const auto selection = host_.selection(); selection && selection->where == Where::TrackList)
    {
        const auto found = std::ranges::find_if(tracks, [&](const auto& t)
                                                { return t->id() == selection->anchor.track; });
        if (found != tracks.end())
        {
            const auto anchor = static_cast<std::size_t>(found - tracks.begin());
            first = std::min(first, anchor);
            last = std::max(last, anchor);
        }
    }
    std::vector<core::TrackId> ids;
    for (auto i = first; i <= last; ++i)
    {
        ids.push_back(tracks[i]->id());
    }
    return ids;
}

std::optional<std::string> TracksView::selectionStatus() const
{
    const auto selection = host_.selection();
    if (!selection || selection->where != Where::TrackList)
    {
        return std::nullopt;
    }
    const auto n = selected().size();
    return std::to_string(n) + (n == 1 ? " whole track" : " whole tracks");
}

Inspection TracksView::inspection() const
{
    Inspection result{"Track", {}};
    if (!cursor_.track)
    {
        return result;
    }
    const auto& track = *document_.song().track(*cursor_.track);
    const auto& column = trackColumns()[column_];
    const auto full = trackFieldText(column.field, track, row(), document_.song().ppqn());
    auto shown = decodeUtf8(full);
    if (shown.size() > 28)
    {
        shown.resize(27);
        shown += U'…';
    }
    result.fields.push_back(
        {std::string{column.heading}, encodeUtf8(shown), column.field != TrackField::Number, full});
    if (column.field == TrackField::ChaseNotes)
        result.fields.push_back({"When enabled", "Start notes already held"});
    result.fields.push_back({"Grid", core::toString(track.grid)});
    result.fields.push_back({"Default length", core::toString(track.defaultLength())});
    result.fields.push_back({"Default velocity", std::to_string(track.noteVelocity)});
    return result;
}

TrackField TracksView::fieldFor(std::string_view heading) const
{
    if (heading == "Chase notes")
    {
        return TrackField::ChaseNotes;
    }
    for (const auto& column : trackColumns())
    {
        if (column.heading == heading)
        {
            return column.field;
        }
    }
    return TrackField::Number;
}

std::optional<std::string> TracksView::edit(TrackField field, std::string_view text, int steps,
                                            bool fine)
{
    const auto ids = selected();
    if (ids.empty())
    {
        return "No track to edit";
    }
    if (ids.size() > 1 && field == TrackField::Name)
    {
        return "Rename one track at a time";
    }
    std::vector<core::Track> edited;
    for (const auto id : ids)
    {
        auto track = *document_.song().track(id);
        const auto why = steps ? stepTrackField(track, field, steps, fine, document_.song().ppqn())
                               : setTrackField(track, field, text, document_.song().ppqn());
        if (why)
        {
            return why;
        }
        edited.push_back(std::move(track));
    }
    document_.edit("Edit track settings", place(),
                   [&](core::Song& song)
                   {
                       for (const auto& track : edited)
                       {
                           song.updateTrack(track.id(), [&](core::Track& into) { into = track; });
                       }
                   });
    return {};
}

std::optional<bool> TracksView::runTyping(std::string_view command)
{
    const bool next = command == "command-line-next" || command == "command-line-previous";
    const auto result =
        editor_.run(next ? "command-line-run" : command, host_,
                    {
                        .inspection = [this] { return inspection(); },
                        .set = [this](const std::string& heading, const std::string& text)
                        { return edit(fieldFor(heading), text); },
                        .step = [this](const std::string& heading, int steps)
                        { return edit(fieldFor(heading), {}, steps); },
                    });
    if (next && !editor_.active())
    {
        const auto columns = trackColumns();
        do
        {
            column_ = command == "command-line-next"
                          ? (column_ + 1) % columns.size()
                          : (column_ + columns.size() - 1) % columns.size();
        } while (columns[column_].field == TrackField::Number);
        ensureVisible();
        if (editor_.open(inspection(), columns[column_].heading))
            host_.startTyping();
    }
    return result;
}

std::optional<bool> TracksView::run(std::string_view command, int count)
{
    const auto n = static_cast<std::size_t>(std::max(count, 1));
    if (command == "cursor-down" || command == "track-next")
    {
        go(row() + n);
    }
    else if (command == "cursor-up" || command == "track-previous")
    {
        go(row() - std::min(row(), n));
    }
    else if (command == "cursor-left")
    {
        column_ -= std::min(column_, n);
        ensureVisible();
    }
    else if (command == "cursor-right")
    {
        column_ = std::min(column_ + n, trackColumns().size() - 1);
        ensureVisible();
    }
    else if (command == "go-first")
    {
        go(count ? n - 1 : 0);
    }
    else if (command == "go-last")
    {
        go(count ? n - 1 : document_.song().tracks().size());
    }
    else if (command == "cursor-first")
    {
        column_ = 0;
        ensureVisible();
    }
    else if (command == "cursor-last")
    {
        column_ = trackColumns().size() - 1;
        ensureVisible();
    }
    else if (command == "scroll-page-down" || command == "scroll-half-down")
    {
        go(row() + n * static_cast<std::size_t>(std::max(1, layout_.fullRows - 1)) /
                       (command == "scroll-half-down" ? 2 : 1));
    }
    else if (command == "scroll-page-up" || command == "scroll-half-up")
    {
        const auto by = n * static_cast<std::size_t>(std::max(1, layout_.fullRows - 1)) /
                        (command == "scroll-half-up" ? 2 : 1);
        go(row() - std::min(row(), by));
    }
    else if (command == "scroll-centre")
    {
        const auto half = static_cast<std::size_t>(std::max(1, layout_.fullRows - 1) / 2);
        top_ = row() - std::min(row(), half);
    }
    else if (command == "field-edit" || command == "inspector-edit")
    {
        if (!cursor_.track)
        {
            host_.refuse("No track to edit");
        }
        else if (command == "field-edit" && trackColumns()[column_].field == TrackField::Number)
        {
            host_.refuse("The track number follows its position");
        }
        else if (editor_.open(inspection(), command == "field-edit"
                                                ? trackColumns()[column_].heading
                                                : std::string_view{}))
        {
            host_.startTyping();
        }
    }
    else if (command == "field-increase" || command == "field-decrease" ||
             command == "field-increase-fine" || command == "field-decrease-fine" ||
             command == "number-increase" || command == "number-decrease")
    {
        const bool up = command.find("increase") != std::string_view::npos;
        if (auto why =
                edit(trackColumns()[column_].field, {},
                     up ? static_cast<int>(n) : -static_cast<int>(n), command.ends_with("fine")))
        {
            host_.refuse(*why);
        }
    }
    else if (command == "put-after" || command == "put-before" || command == "put-replace")
    {
        return put(command == "put-before", command == "put-replace", count);
    }
    else if (command == "event-yank")
    {
        return operate("operator-yank", "operator-line", count);
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

bool TracksView::operate(std::string_view op, std::string_view motion, int count)
{
    if (op != "operator-yank" && op != "operator-delete")
    {
        host_.refuse("That operator does not act on whole tracks");
        return true;
    }
    if (op == "operator-delete" && motion != "visual")
    {
        host_.refuse("Use V to select whole tracks, then d to delete them");
        return true;
    }
    auto ids = selected();
    if (motion == "operator-line")
    {
        ids.clear();
        const auto tracks = document_.song().tracks();
        for (auto i = row();
             i < std::min(tracks.size(), row() + static_cast<std::size_t>(std::max(count, 1))); ++i)
        {
            ids.push_back(tracks[i]->id());
        }
    }
    else if (motion != "visual")
    {
        host_.refuse("Use V to select whole tracks, then y or d");
        return true;
    }
    if (ids.empty())
    {
        host_.refuse("No track to take");
        return true;
    }
    EventRegister taken;
    for (auto id : ids)
    {
        taken.wholeTracks.push_back(*document_.song().copyTrack(id));
    }
    if (op == "operator-delete")
    {
        const auto at = row();
        document_.edit("Delete whole tracks", place(),
                       [&](core::Song& song)
                       {
                           for (auto id : ids)
                           {
                               song.removeTrack(id);
                           }
                       });
        go(at);
    }
    host_.eventRegister() = std::move(taken);
    host_.setMessage(std::string{op == "operator-yank" ? "Yanked " : "Deleted "} +
                         std::to_string(ids.size()) +
                         (ids.size() == 1 ? " whole track" : " whole tracks"),
                     MessageKind::Information);
    return true;
}

bool TracksView::put(bool before, bool replace, int count)
{
    const auto copies = host_.eventRegister().wholeTracks;
    if (copies.empty())
    {
        host_.refuse("Yank whole tracks in this view before putting them");
        return true;
    }
    for (const auto& copy : copies)
    {
        if (!document_.song().canInsertTrack(copy))
        {
            host_.refuse("The copied tracks need the same PPQN, section names and lengths");
            return true;
        }
    }
    const auto ids = selected();
    auto at = row() + (before || document_.song().tracks().empty() ? 0 : 1);
    if (replace && !ids.empty())
    {
        const auto tracks = document_.song().tracks();
        const auto it =
            std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == ids.front(); });
        at = static_cast<std::size_t>(it - tracks.begin());
    }
    std::optional<core::TrackId> first;
    document_.edit(replace ? "Replace whole tracks" : "Insert whole tracks", place(),
                   [&](core::Song& song)
                   {
                       if (replace)
                       {
                           for (const auto id : ids)
                           {
                               song.removeTrack(id);
                           }
                       }
                       for (int n = 0; n < std::max(count, 1); ++n)
                       {
                           for (const auto& copy : copies)
                           {
                               const auto id = song.insertTrack(at++, copy);
                               if (!first)
                               {
                                   first = id;
                               }
                           }
                       }
                   });
    cursor_.track = first;
    refresh();
    host_.setMessage("Put whole tracks", MessageKind::Information);
    return true;
}

void TracksView::addRegions(Frame& frame) const
{
    Region table{.bounds = layout_.main,
                 .background = Role::Background,
                 .text = {},
                 .under = {},
                 .over = {}};
    const auto columns = trackColumns();
    const auto tracks = document_.song().tracks();
    const auto chosen = host_.selection() ? selected() : std::vector<core::TrackId>{};
    const auto draw = [&](std::size_t index, int y, const core::Track* track)
    {
        int x = (1 - left_) * cell_.width;
        for (std::size_t c = 0; c < columns.size(); ++c)
        {
            const auto& column = columns[c];
            auto text = decodeUtf8(
                track ? trackFieldText(column.field, *track, index, document_.song().ppqn())
                      : std::string{column.heading});
            if (text.size() >= static_cast<std::size_t>(column.width))
            {
                text.resize(static_cast<std::size_t>(column.width - 2));
                text += U'…';
            }
            const auto padding = static_cast<std::size_t>(column.width - 1) - text.size();
            if (track && column.numeric)
                text.insert(0, padding, U' ');
            else
                text.append(padding, U' ');
            const bool toggle =
                track && (column.field == TrackField::Mute || column.field == TrackField::Solo ||
                          column.field == TrackField::ChaseNotes);
            if (toggle)
                text.assign(static_cast<std::size_t>(column.width - 1), U' ');
            Style style{track ? Role::Text : Role::Dim, Role::Background, kPlain};
            if (track && std::ranges::find(chosen, track->id()) != chosen.end())
            {
                style.background = Role::Selection;
            }
            if (track && !toggle && index == row() && c == column_)
            {
                style = {Role::CursorText, Role::Cursor, kPlain};
            }
            table.text.push_back({x, y, std::move(text), style});
            if (toggle)
            {
                const bool on = column.field == TrackField::Mute   ? track->mute
                                : column.field == TrackField::Solo ? track->solo
                                                                   : track->chaseNotes;
                const auto colour = column.field == TrackField::Mute   ? Role::TrackMute
                                    : column.field == TrackField::Solo ? Role::TrackSolo
                                                                       : Role::TrackChase;
                const auto badge = U" " +
                                   decodeUtf8(trackFieldText(column.field, *track, index,
                                                             document_.song().ppqn())) +
                                   U" ";
                const auto edge = hairline(cell_);
                const int w = 3 * cell_.width;
                // Text backgrounds are painted before shapes; only the badge carries a glyph.
                table.under.push_back({{x, y + edge, w, std::max(1, cell_.height - 2 * edge)},
                                       on ? colour : Role::Panel});
                table.text.push_back(
                    {x, y, badge, {on ? Role::ToggleText : Role::Dim, style.background, kBold}});
                if (index == row() && c == column_)
                {
                    table.over.push_back({{x, y, w, edge}, Role::Cursor});
                    table.over.push_back({{x, y + cell_.height - edge, w, edge}, Role::Cursor});
                    table.over.push_back({{x, y, edge, cell_.height}, Role::Cursor});
                    table.over.push_back({{x + w - edge, y, edge, cell_.height}, Role::Cursor});
                }
            }
            x += column.width * cell_.width;
        }
    };
    draw(0, 0, nullptr);
    for (auto i = top_;
         i < tracks.size() && i - top_ + 1 < static_cast<std::size_t>(std::max(layout_.rows, 1));
         ++i)
    {
        draw(i, static_cast<int>(i - top_ + 1) * cell_.height, tracks[i].get());
    }
    if (tracks.empty())
    {
        table.text.push_back(
            {cell_.width, cell_.height, U"No tracks", {Role::Dim, Role::Background, kPlain}});
    }
    frame.push_back(std::move(table));
    if (layout_.top.size() < 2)
    {
        return;
    }
    frame.push_back(trackBarRegion(document_.song(), tracks.empty() ? 0 : row() + 1, cursor_.tick,
                                   cursorMeter(), layout_.top[0], width_, cell_));
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_, 9, &editor_));
}
} // namespace modal::ui
