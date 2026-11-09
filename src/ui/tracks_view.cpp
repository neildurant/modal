// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>
#include <modal/ui/tracks_view.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/view_state.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{
bool isToggle(TrackField field)
{
    return field == TrackField::Mute || field == TrackField::Solo ||
           field == TrackField::ChaseNotes || field == TrackField::Quantize;
}

bool toggleState(TrackField field, const core::Track& track)
{
    return field == TrackField::Mute         ? track.mute
           : field == TrackField::Solo       ? track.solo
           : field == TrackField::ChaseNotes ? track.chaseNotes
                                             : track.quantize.on;
}

// The inspector's fields for playback quantise, and what each sets.
constexpr std::array<std::pair<std::string_view, TrackField>, 4> kQuantizeFields{{
    {"Grid", TrackField::QuantizeGrid},
    {"Strength", TrackField::QuantizeStrength},
    {"Swing", TrackField::QuantizeSwing},
    {"Moves", TrackField::QuantizeMove},
}};
} // namespace

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

int TracksView::columnWidth(const TrackColumn& column) const
{
    if (column.field == TrackField::Port)
    {
        // As wide as the longest port a track names, up to 24 characters, the whole name in
        // the bar above and the inspector.
        std::size_t longest = 0;
        for (const auto& track : document_.song().tracks())
        {
            longest = std::max(longest, decodeUtf8(track->port).size());
        }
        return std::clamp(static_cast<int>(longest) + 1, column.width, 24 + 1);
    }
    if (column.field != TrackField::Name)
    {
        return column.width;
    }
    std::size_t longest = 0;
    for (const auto& track : document_.song().tracks())
    {
        longest = std::max(longest, decodeUtf8(track->name).size());
    }
    // Up to 32 characters, scrolling the table sideways if it must, or further if that leaves
    // the table within the window. A character more for the space after it.
    int others = 0;
    for (const auto& other : trackColumns())
    {
        others += other.field == TrackField::Name ? 0 : columnWidth(other);
    }
    const int widest = std::max(32 + 1, layout_.fullColumns - 2 - others);
    return std::max(column.width, std::min(static_cast<int>(longest) + 1, widest));
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
        x += columnWidth(columns[i]);
    }
    const int room = std::max(layout_.fullColumns - 2, 1);
    if (x < left_)
    {
        left_ = x;
    }
    if (x + columnWidth(columns[column_]) > left_ + room)
    {
        left_ = std::max(0, x + columnWidth(columns[column_]) - room);
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
    quantizeEditing_ = false;
    portChoice_.reset();
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
    return placeText(document_.song(), cursorMeter(), tick);
}
std::string TracksView::cursorStatus() const
{
    return document_.song().tracks().empty()
               ? "No tracks"
               : "Track " + std::to_string(row() + 1) + "  " + timeStatus(cursor_.tick);
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
    // What the table has no room for, the cursor's column's first; fields are edited in the table.
    Inspection result{"Track", {}};
    if (!cursor_.track)
    {
        return result;
    }
    const auto& song = document_.song();
    const auto& track = *song.track(*cursor_.track);
    const auto field = trackColumns()[column_].field;
    if (field == TrackField::Quantize || field == TrackField::QuantizeSummary)
    {
        auto fields = quantizeFields();
        if (!quantizeEditing_)
        {
            for (auto& shown : fields.fields)
            {
                shown.editable = track.quantize.on; // dim while it's off
            }
        }
        result = std::move(fields);
        if (const auto* player = host_.player())
        {
            for (const auto& warning : player->quantizeWarnings())
            {
                if (warning.track == track.id())
                {
                    InspectorField left{
                        warning.leftInPlace > 0 ? "Left where stored" : "Plays as stored",
                        warning.leftInPlace > 0
                            ? std::to_string(warning.leftInPlace) +
                                  (warning.leftInPlace == 1 ? " note, at "
                                                            : " notes, the first at ") +
                                  position(warning.at) +
                                  ", as another of its pitch starts on that line"
                            : "a pedal would pass the next of its pedal at " +
                                  position(warning.at)};
                    left.warning = warning.leftInPlace == 0;
                    result.fields.push_back(std::move(left));
                }
            }
        }
        return result;
    }
    if (field == TrackField::ChaseNotes)
    {
        result.fields.push_back({"When enabled", "Start notes already held"});
    }
    if (field == TrackField::Port && portMissing(track))
    {
        InspectorField missing{"Port not found", track.port};
        missing.warning = true;
        result.fields.push_back(std::move(missing));
    }
    if (field == TrackField::Program || field == TrackField::BankMsb ||
        field == TrackField::BankLsb)
    {
        if (const auto own = programEvents(song, track.id()); own.count > 0)
        {
            result.fields.push_back({"Program events", std::to_string(own.count) + ", first at " +
                                                           position(own.first)});
        }
        if (const auto clashes = programClashes(song, track); !clashes.empty())
        {
            std::string names;
            for (const auto index : clashes)
            {
                names += (names.empty() ? "" : ", ") + std::to_string(index + 1);
            }
            InspectorField clash{"Differs from tracks on this channel", names};
            clash.warning = true;
            result.fields.push_back(std::move(clash));
        }
    }
    result.fields.push_back({"Name", track.name.empty() ? "-" : track.name});
    return result;
}

Inspection TracksView::editableFields() const
{
    Inspection result{"Track", {}};
    if (!cursor_.track)
    {
        return result;
    }
    const auto& track = *document_.song().track(*cursor_.track);
    for (const auto& column : trackColumns())
    {
        if (column.field != TrackField::Number && column.field != TrackField::Port &&
            column.field != TrackField::QuantizeSummary && !isToggle(column.field))
        {
            const auto text = trackFieldText(column.field, track, row());
            result.fields.push_back({std::string{column.heading}, text, true, text});
        }
    }
    return result;
}

Inspection TracksView::quantizeFields() const
{
    Inspection result{"Quantise", {}};
    if (!cursor_.track)
    {
        return result;
    }
    const auto& track = *document_.song().track(*cursor_.track);
    for (const auto& [heading, field] : kQuantizeFields)
    {
        auto text = trackFieldText(field, track, row());
        auto edit = text.ends_with('%') ? text.substr(0, text.size() - 1) : text;
        result.fields.push_back({std::string{heading}, std::move(text), true, std::move(edit)});
    }
    return result;
}

TrackField TracksView::fieldFor(std::string_view heading) const
{
    if (quantizeEditing_)
    {
        const auto it = std::ranges::find(kQuantizeFields, heading,
                                          &std::pair<std::string_view, TrackField>::first);
        if (it != kQuantizeFields.end())
        {
            return it->second;
        }
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
    if (const auto* player = host_.player();
        player != nullptr && player->playing() &&
        (field == TrackField::Program || field == TrackField::BankMsb ||
         field == TrackField::BankLsb))
    {
        const auto at = player->position().value_or(player->marker());
        if (std::ranges::any_of(ids, [&](core::TrackId id)
                                { return programBefore(document_.song(), id, at); }))
        {
            host_.setMessage("Sent at the next play: the track's own program change is in force",
                             MessageKind::Information);
        }
    }
    return {};
}

std::optional<bool> TracksView::runTyping(std::string_view command)
{
    if (portChoice_)
    {
        return runPortChoice(command);
    }
    if (quantizeEditing_)
    {
        // The inspector's own fields: Tab goes between them, as in the note inspector.
        const auto result =
            editor_.run(command, host_,
                        {
                            .inspection = [this] { return quantizeFields(); },
                            .set = [this](const std::string& heading, const std::string& text)
                            { return edit(fieldFor(heading), text); },
                            .step = [this](const std::string& heading, int steps)
                            { return edit(fieldFor(heading), {}, steps); },
                        });
        quantizeEditing_ = editor_.active();
        return result;
    }
    const bool next = command == "command-line-next" || command == "command-line-previous";
    const auto result =
        editor_.run(next ? "command-line-run" : command, host_,
                    {
                        .inspection = [this] { return editableFields(); },
                        .set = [this](const std::string& heading, const std::string& text)
                        { return edit(fieldFor(heading), text); },
                        .step = [this](const std::string& heading, int steps)
                        { return edit(fieldFor(heading), {}, steps); },
                    });
    if (next && !editor_.active())
    {
        nextField(command == "command-line-next");
    }
    return result;
}

std::optional<std::string> TracksView::typingHint() const
{
    if (portChoice_)
    {
        return "Type to narrow the list; Up and Down choose, Enter or Tab takes it, Escape "
               "leaves it";
    }
    if (!editor_.active())
    {
        return std::nullopt;
    }
    if (quantizeEditing_)
    {
        const auto& heading = editor_.heading();
        if (heading == "Grid")
            return "A note length, such as 1/4, 1/8, 1/16 or 1/8t for a triplet, or grid for the "
                   "track's own; Up and Down go coarser and finer, through triplets too";
        if (heading == "Strength")
            return "How far towards the line, 0% to 100%; Up and Down step 5%";
        if (heading == "Swing")
            return "25% to 75%: 50% straight, 66% a triplet feel, under 50% pushed; Up and Down "
                   "step 1%";
        return "starts, ends, both or lengths; Up and Down go through them";
    }
    switch (fieldFor(editor_.heading()))
    {
    case TrackField::Name:
        return "Any name; Enter sets it, Tab goes on, Escape leaves it";
    case TrackField::Channel:
        return "1 to 16; Up and Down step it";
    case TrackField::BankMsb:
    case TrackField::BankLsb:
    case TrackField::Program:
        return "0 to 127, or nothing to leave it unset; Up and Down step it";
    case TrackField::Transpose:
        return "Semitones, -127 to +127; Up and Down step it";
    case TrackField::VelocityOffset:
        return "Added to every velocity, -127 to +127; Up and Down step it";
    case TrackField::TimeOffset:
        return "Such as -40ms, +120t or +1/16-22t; Up and Down step it, by the track's grid if "
               "it's musical";
    default:
        return std::nullopt;
    }
}

bool TracksView::openField()
{
    if (trackColumns()[column_].field == TrackField::QuantizeSummary)
    {
        quantizeEditing_ = editor_.open(quantizeFields());
        return quantizeEditing_;
    }
    if (trackColumns()[column_].field != TrackField::Port)
    {
        return editor_.open(editableFields(), trackColumns()[column_].heading);
    }
    const auto& track = *document_.song().track(*cursor_.track);
    PortChoice choice;
    choice.current = track.port;
    choice.ports.emplace_back();
    for (auto& port : host_.outputPorts())
    {
        choice.ports.push_back(std::move(port));
    }
    const auto at = std::ranges::find(choice.ports, track.port);
    if (at == choice.ports.end())
    {
        choice.ports.push_back(track.port);
        choice.chosen = choice.ports.size() - 1;
        choice.gone = choice.chosen;
    }
    else
    {
        choice.chosen = static_cast<std::size_t>(at - choice.ports.begin());
    }
    constexpr std::size_t kShown = 8;
    choice.top = choice.chosen >= kShown ? choice.chosen - kShown + 1 : 0;
    portChoice_ = std::move(choice);
    return true;
}

void TracksView::nextField(bool forwards)
{
    const auto columns = trackColumns();
    do
    {
        column_ = forwards ? (column_ + 1) % columns.size()
                           : (column_ + columns.size() - 1) % columns.size();
    } while (columns[column_].field == TrackField::Number || isToggle(columns[column_].field));
    ensureVisible();
    if (openField())
    {
        host_.startTyping();
    }
}

void TracksView::type(char32_t character)
{
    if (portChoice_)
    {
        portChoice_->typed += character;
        portChoice_->chosen = 0;
        portChoice_->top = 0;
    }
    else
    {
        editor_.type(character);
    }
}

std::vector<std::size_t> TracksView::portMatches() const
{
    const auto folded = [](std::u32string text)
    {
        for (auto& c : text)
        {
            c = c < 128 ? static_cast<char32_t>(std::tolower(static_cast<int>(c))) : c;
        }
        return text;
    };
    const auto wanted = folded(portChoice_->typed);
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < portChoice_->ports.size(); ++i)
    {
        const auto& port = portChoice_->ports[i];
        if (folded(decodeUtf8(port.empty() ? "default" : port)).find(wanted) !=
            std::u32string::npos)
        {
            result.push_back(i);
        }
    }
    return result;
}

std::optional<bool> TracksView::runPortChoice(std::string_view command)
{
    constexpr std::size_t kShown = 8;
    auto& choice = *portChoice_;
    const auto matches = portMatches();
    const auto moveBy = [&](std::ptrdiff_t by)
    {
        if (matches.empty())
        {
            return;
        }
        const auto last = static_cast<std::ptrdiff_t>(matches.size()) - 1;
        choice.chosen = static_cast<std::size_t>(
            std::clamp(static_cast<std::ptrdiff_t>(choice.chosen) + by, std::ptrdiff_t{0}, last));
        choice.top = std::min(choice.top, choice.chosen);
        choice.top = choice.chosen >= choice.top + kShown ? choice.chosen - kShown + 1 : choice.top;
    };
    if (command == "command-line-cancel")
    {
        portChoice_.reset();
        host_.stopTyping();
    }
    else if (command == "command-line-older")
    {
        moveBy(-1);
    }
    else if (command == "command-line-newer")
    {
        moveBy(1);
    }
    else if (command == "command-line-page-up")
    {
        moveBy(-static_cast<std::ptrdiff_t>(kShown));
    }
    else if (command == "command-line-page-down")
    {
        moveBy(static_cast<std::ptrdiff_t>(kShown));
    }
    else if (command == "command-line-backspace" || command == "command-line-clear")
    {
        if (command == "command-line-clear")
        {
            choice.typed.clear();
        }
        else if (!choice.typed.empty())
        {
            choice.typed.pop_back();
        }
        choice.chosen = 0;
        choice.top = 0;
    }
    else if (command == "command-line-run" || command == "command-line-next" ||
             command == "command-line-previous")
    {
        if (matches.empty())
        {
            host_.refuse("No port matches that");
            return true;
        }
        const auto port = choice.ports[matches[choice.chosen]];
        // On a selection, the others may not have it yet even if the cursor's track does.
        const bool changed = port != choice.current || selected().size() > 1;
        portChoice_.reset();
        host_.stopTyping();
        if (changed)
        {
            if (auto why = edit(TrackField::Port, port))
            {
                host_.refuse(std::move(*why));
                return true;
            }
        }
        if (command != "command-line-run")
        {
            nextField(command == "command-line-next");
        }
    }
    return true;
}

bool TracksView::portMissing(const core::Track& track) const
{
    const auto* player = host_.player();
    return player != nullptr && !track.port.empty() &&
           std::ranges::find(player->missingPorts(), track.port) != player->missingPorts().end();
}

void TracksView::addPortChoice(Frame& frame) const
{
    constexpr std::size_t kShown = 8;
    const auto matches = portMatches();
    const auto rows = std::clamp<std::size_t>(matches.size(), 1, kShown);
    const auto label = [&](std::size_t match)
    {
        const auto& port = portChoice_->ports[matches[match]];
        return decodeUtf8(port.empty()                          ? std::string{"default"}
                          : matches[match] == portChoice_->gone ? port + " (not found)"
                                                                : port);
    };

    // At the cell, below it, or above where there's no room; as wide as the longest in view.
    int x = (1 - left_) * cell_.width;
    int cellWidth = 0;
    for (std::size_t c = 0; c < trackColumns().size(); ++c)
    {
        cellWidth = columnWidth(trackColumns()[c]);
        if (c == column_)
        {
            break;
        }
        x += cellWidth * cell_.width;
    }
    std::size_t widest = static_cast<std::size_t>(cellWidth);
    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        widest = std::max(widest, label(i).size() + 2);
    }
    const int width = std::min(static_cast<int>(widest) * cell_.width, layout_.main.width);
    x = std::clamp(x, 0, layout_.main.width - width);
    const int height = static_cast<int>(rows) * cell_.height;
    const int cellY = static_cast<int>(row() - top_ + 1) * cell_.height;
    const int y = cellY + cell_.height + height <= layout_.main.height
                      ? cellY + cell_.height
                      : std::max(cellY - height, 0);

    Region list{.bounds = {layout_.main.x + x, layout_.main.y + y, width, height},
                .background = Role::Panel,
                .text = {},
                .under = {},
                .over = {}};
    if (matches.empty())
    {
        list.text.push_back({cell_.width, 0, U"No port matches", {Role::Dim, Role::Panel, kPlain}});
    }
    for (std::size_t i = portChoice_->top; i < matches.size() && i < portChoice_->top + rows; ++i)
    {
        auto text = label(i);
        const bool here = i == portChoice_->chosen;
        const bool missing = text.ends_with(U"(not found)");
        text.insert(0, 1, U' ');
        text.resize(static_cast<std::size_t>(width / cell_.width), U' ');
        list.text.push_back({0, static_cast<int>(i - portChoice_->top) * cell_.height,
                             std::move(text),
                             here      ? Style{Role::CursorText, Role::Cursor, kPlain}
                             : missing ? Style{Role::Warning, Role::Panel, kPlain}
                                       : Style{Role::Text, Role::Panel, kPlain}});
    }
    frame.push_back(std::move(list));
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
        else if (const auto field = trackColumns()[column_].field; isToggle(field))
        {
            toggle(field);
        }
        else if (trackColumns()[column_].field == TrackField::Number)
        {
            host_.refuse("The track number follows its position");
        }
        else if (openField())
        {
            host_.startTyping();
        }
    }
    else if (command == "tracks-mute-toggle")
    {
        toggle(TrackField::Mute);
    }
    else if (command == "tracks-solo-toggle")
    {
        toggle(TrackField::Solo);
    }
    else if (command == "tracks-quantize-toggle")
    {
        toggle(TrackField::Quantize);
    }
    else if (command == "tracks-chase-toggle")
    {
        toggle(TrackField::ChaseNotes);
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

void TracksView::toggle(TrackField field)
{
    if (!cursor_.track)
    {
        host_.refuse("No track to change");
        return;
    }
    // Selected rows all follow the cursor's row, so they end up the same.
    const bool on = !toggleState(field, *document_.song().track(*cursor_.track));
    if (auto why = edit(field, on ? "on" : "off"))
    {
        host_.refuse(*why);
    }
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
    auto chosen = host_.movingTracks();
    if (chosen.empty() && host_.selection())
    {
        chosen = selected();
    }
    const auto draw = [&](std::size_t index, int y, const core::Track* track)
    {
        int x = (1 - left_) * cell_.width;
        for (std::size_t c = 0; c < columns.size(); ++c)
        {
            const auto& column = columns[c];
            const int wide = columnWidth(column);
            auto text = decodeUtf8(track ? trackFieldText(column.field, *track, index)
                                         : std::string{column.heading});
            const bool program = track && column.field == TrackField::Program;
            if (program) // a mark for the track's own program changes, keeping the digits in line
            {
                text += programEvents(document_.song(), track->id()).count > 0 ? U'•' : U' ';
            }
            if (text.size() >= static_cast<std::size_t>(wide))
            {
                text.resize(static_cast<std::size_t>(wide - 2));
                text += U'…';
            }
            const auto padding = static_cast<std::size_t>(wide - 1) - text.size();
            if (column.numeric) // the heading too, over its values
                text.insert(0, padding, U' ');
            else
                text.append(padding, U' ');
            const bool toggle = track && isToggle(column.field);
            if (toggle)
                text.assign(static_cast<std::size_t>(wide - 1), U' ');
            Style style{track ? Role::Text : Role::Dim, Role::Background, kPlain};
            if (program && !programClashes(document_.song(), *track).empty())
            {
                style.foreground = Role::Warning;
            }
            if (track && std::ranges::find(chosen, track->id()) != chosen.end())
            {
                style.background = Role::Selection;
                // Into the space after the column, so a selected row is one unbroken band.
                table.text.push_back(
                    {x + (wide - 1) * cell_.width, y, U" ", {Role::Text, Role::Selection, kPlain}});
            }
            if (track && !toggle && index == row() && c == column_)
            {
                style = {Role::CursorText, Role::Cursor, kPlain};
            }
            if (track && ((column.field == TrackField::Port && portMissing(*track)) ||
                          (column.field == TrackField::QuantizeSummary && !track->quantize.on)))
            {
                style.foreground = Role::Dim;
            }
            if (track && portChoice_ && index == row() && c == column_)
            {
                // What's typed to narrow the list, or else the port it's on now, with a cursor.
                auto shown = portChoice_->typed.empty() ? decodeUtf8(portChoice_->current)
                                                        : portChoice_->typed;
                const auto box = static_cast<std::size_t>(wide - 1);
                if (shown.size() >= box)
                {
                    shown = shown.substr(shown.size() - box + 1);
                }
                const auto at = portChoice_->typed.empty() ? std::size_t{0} : shown.size();
                const Style editing{Role::Text, Role::Selection, kPlain};
                auto filled = shown;
                filled.resize(box, U' ');
                table.text.push_back({x, y, std::move(filled), editing});
                table.text.push_back({x + static_cast<int>(at) * cell_.width,
                                      y,
                                      std::u32string(1, at < shown.size() ? shown[at] : U' '),
                                      {Role::CursorText, Role::Cursor, kPlain}});
            }
            else if (track && editor_.active() && !quantizeEditing_ && index == row() &&
                     c == column_)
            {
                inspectorValue(table, {std::string{column.heading}, {}}, {}, x, y, wide - 1, cell_,
                               &editor_, column.numeric);
            }
            else
            {
                table.text.push_back({x, y, std::move(text), style});
            }
            if (toggle)
            {
                const bool on = toggleState(column.field, *track);
                const auto colour = column.field == TrackField::Mute         ? Role::TrackMute
                                    : column.field == TrackField::Solo       ? Role::TrackSolo
                                    : column.field == TrackField::ChaseNotes ? Role::TrackChase
                                                                             : Role::TrackQuantize;
                const auto badge =
                    U" " + decodeUtf8(trackFieldText(column.field, *track, index)) + U" ";
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
            x += wide * cell_.width;
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
    if (portChoice_)
    {
        addPortChoice(frame);
    }
    if (layout_.top.size() < 2)
    {
        return;
    }
    frame.push_back(trackBarRegion(document_.song(), tracks.empty() ? 0 : row() + 1, cursor_.tick,
                                   cursorMeter(), layout_.top[0], width_, cell_));
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_, 9,
                                    quantizeEditing_ ? &editor_ : nullptr));
}
} // namespace modal::ui
