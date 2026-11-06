// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};

// A fixed-width value box, shared by the compact note panel and the general inspector.
void inspectorValue(Region& region, const InspectorField& field, std::u32string text, int x, int y,
                    int columns, CellSize cell, const InspectorEditor* editor,
                    bool rightAligned = false)
{
    if (columns < 1)
        return;
    const bool edited = editor && editor->active() && editor->heading() == field.heading;
    const auto box = static_cast<std::size_t>(columns);
    if (edited)
    {
        text = editor->line().text();
        const auto typed = editor->line().cursor();
        const std::size_t first = typed + 1 > box ? typed + 1 - box : 0;
        const auto pad = first == 0 && text.size() + 1 <= box ? box - text.size() - 1 : 0;
        auto visible = std::u32string(pad, U' ') + text.substr(first, box);
        visible.resize(box, U' ');
        region.text.push_back({x, y, visible, {Role::Text, Role::Selection, kPlain}});
        region.text.push_back({x + static_cast<int>(pad + typed - first) * cell.width,
                               y,
                               std::u32string(1, typed < text.size() ? text[typed] : U' '),
                               {Role::CursorText, Role::Cursor, kPlain}});
    }
    else
    {
        if (text.size() > box)
            text = text.substr(0, box - 1) + U'…';
        if (rightAligned)
            x += static_cast<int>(box - text.size()) * cell.width;
        region.text.push_back({x,
                               y,
                               std::move(text),
                               {field.editable ? Role::Text : Role::Dim, Role::Panel, kPlain}});
    }
}

Region noteInspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                           int positionWidth, const InspectorEditor* editor)
{
    struct Slot
    {
        std::string_view field;
        std::string_view label;
        int row;
        int labelColumn;
        int valueColumn;
        int width;
    };
    const int exactWidth = std::max(positionWidth, 9) + inspection.beatDigits - 1;
    constexpr int kExactColumn = 48;
    const std::array slots{
        Slot{"Pitch", "Pitch:", 1, 0, 10, 9},
        Slot{"Velocity", "Velocity:", 2, 0, 10, 9},
        Slot{"Release", "Off Vel:", 3, 0, 10, 9},
        Slot{"Units", "Units:", 0, 22, 30, 6},
        Slot{"Start step", "Start:", 1, 22, 30, 6},
        Slot{"Start fine", "", 1, 38, 38, 8},
        Slot{"Position", "", 1, kExactColumn, kExactColumn, exactWidth},
        Slot{"Length steps", "Length:", 2, 22, 30, 6},
        Slot{"Length fine", "", 2, 38, 38, 8},
        Slot{"Length", "", 2, kExactColumn, kExactColumn, exactWidth},
        Slot{"Ends step", "Ends:", 3, 22, 30, 6},
        Slot{"Ends fine", "", 3, 38, 38, 8},
        Slot{"Ends", "", 3, kExactColumn, kExactColumn, exactWidth},
    };
    Region region{.bounds = bounds, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const int margin = BarSpacing::forCell(cell).margin;
    const int columns = std::max(1, (width - 2 * margin) / cell.width);
    int left = 0;
    if (editor && editor->active())
        for (const auto& slot : slots)
            if (slot.field == editor->heading())
                left = std::max(0, slot.valueColumn + slot.width - columns);
    const auto x = [&](int col) { return margin + (col - left) * cell.width; };
    const auto y = [&](int row) { return cell.height / 4 + row * cell.height; };
    const Style label{Role::Dim, Role::Panel, kPlain};
    // A bar, beat and tick, with room for the widest beat, so the colons line up whatever's
    // in them; the bars and ticks line up by being on the right.
    const auto aligned = [&](std::string text)
    {
        const auto first = text.find(':');
        const auto second = text.find(':', first + 1);
        if (second != std::string::npos)
        {
            const auto digits = static_cast<std::size_t>(inspection.beatDigits);
            const auto beat = second - first - 1;
            text.insert(first + 1, digits > beat ? digits - beat : 0, ' ');
        }
        return text;
    };

    region.text.push_back(
        {x(0), y(0), decodeUtf8(inspection.kind), {Role::Accent, Role::Panel, kBold}});
    const std::u32string fine = U"Fine(t)";
    region.text.push_back({x(38 + 8 - static_cast<int>(fine.size())), y(0), fine, label});
    // Over the exact values, each letter above its part: the T above the ticks' first digit.
    const auto position =
        std::ranges::find(inspection.fields, "Position", &InspectorField::heading);
    if (position != inspection.fields.end())
    {
        const auto digits = position->value.size() - position->value.rfind(':') - 1;
        const auto heading =
            decodeUtf8(aligned("B:B:T")) + std::u32string(digits > 0 ? digits - 1 : 0, U' ');
        region.text.push_back({x(kExactColumn + exactWidth - static_cast<int>(heading.size())),
                               y(0), heading, label});
    }
    for (const auto& slot : slots)
    {
        const auto it = std::ranges::find(inspection.fields, slot.field, &InspectorField::heading);
        if (it == inspection.fields.end())
            continue;
        if (!slot.label.empty())
            region.text.push_back(
                {x(slot.labelColumn), y(slot.row), decodeUtf8(slot.label), label});
        auto value = it->value;
        if (slot.field.ends_with(" fine"))
        {
            if (value.ends_with('t'))
                value.pop_back();
        }
        if (slot.field == "Length" || slot.field == "Units")
        {
            if (slot.field == "Units" && value.find("(rounded)") != std::string::npos)
                region.text.push_back(
                    {x(kExactColumn + exactWidth + 2), y(0), U"Unit rounded to ticks", label});
            value = value.substr(0, value.find(" ("));
        }
        if (slot.field == "Position" || slot.field == "Length" || slot.field == "Ends")
            value = aligned(std::move(value));
        inspectorValue(region, *it, decodeUtf8(value), x(slot.valueColumn), y(slot.row), slot.width,
                       cell, editor, true);
    }
    return region;
}

} // namespace

int inspectorHeight(CellSize cell)
{
    return 2 * cell.height + (cell.height / 4) * 2;
}

Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth, const InspectorEditor* editor)
{
    if (inspection.kind == "Note" &&
        std::ranges::find(inspection.fields, "Units", &InspectorField::heading) !=
            inspection.fields.end())
        return noteInspectorRegion(inspection, bounds, width, cell, positionWidth, editor);
    // On a panel of its own: the kind of row, then headings above their values. Each column
    // is as wide as its longest value can be, so the columns stay put as the cursor moves;
    // a value that doesn't fit is cut short, and those after it left out.
    Region region{.bounds = bounds, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const int margin = BarSpacing::forCell(cell).margin;
    const int columns = std::max((width - 2 * margin) / cell.width, 0);
    const Style kind{Role::Accent, Role::Panel, kBold};
    const Style heading{Role::Dim, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell.width; };
    const auto widthOf = [&](std::string_view name)
    {
        constexpr std::array<std::pair<std::string_view, int>, 16> kWidths{{
            {"Bar", 10},
            {"Pitch", 9},
            {"Velocity", 8},
            {"Length", 15},
            {"Release", 7},
            {"Controller", 25},
            {"Value", 5},
            {"Program", 7},
            {"Bend", 6},
            {"Pressure", 8},
            {"Bytes", 5},
            {"Tempo", 12},
            {"Per quarter", 12},
            {"Meter", 6},
            {"Bars", 10}, // as wide as Bar, so what comes after is in the same place
            {"Kind", 13},
        }};
        if (name == "Position" || name == "Ends")
        {
            return std::max(positionWidth, 8);
        }
        const auto it = std::ranges::find(kWidths, name, &std::pair<std::string_view, int>::first);
        return it == kWidths.end() ? 0 : it->second;
    };

    const int top = cell.height / 4;
    region.text.push_back({x(0), top, decodeUtf8(inspection.kind), kind});

    int at = widthOf("Kind") + 3;
    for (const auto& field : inspection.fields)
    {
        if (field.underKind)
        {
            inspectorValue(region, field, decodeUtf8(field.value), x(0), top + cell.height,
                           widthOf("Kind"), cell, editor);
            continue;
        }
        auto title = decodeUtf8(field.heading);
        auto text = decodeUtf8(field.value);
        const bool edited =
            editor != nullptr && editor->active() && editor->heading() == field.heading;
        if (edited)
        {
            text = editor->line().text();
        }
        const int room = columns - at;
        if (room < kMinSegmentCharacters + 1)
        {
            break;
        }
        const auto cut = [&](std::u32string& line)
        {
            if (static_cast<int>(line.size()) > room)
            {
                line.resize(static_cast<std::size_t>(room - 1));
                line += U'…';
            }
        };
        cut(title);
        region.text.push_back({x(at), top, title, heading});
        const int wide =
            std::max({static_cast<int>(title.size()),
                      static_cast<int>(text.size()) + (edited ? 1 : 0), widthOf(field.heading)});
        inspectorValue(region, field, std::move(text), x(at), top + cell.height,
                       std::min(wide, room), cell, editor);
        at += wide + 3;
    }
    return region;
}

void addRowSigns(Region& region, std::vector<RowSign> signs, CellSize cell)
{
    std::ranges::stable_sort(signs, [](const RowSign& a, const RowSign& b)
                             { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    int right = 0; // the last's in the row
    for (std::size_t i = 0; i < signs.size(); ++i)
    {
        auto x = signs[i].x;
        if (i > 0 && signs[i - 1].y == signs[i].y && signs[i].moves)
        {
            x = std::max(x, right);
        }
        right = x + cell.width;
        region.under.push_back({{x, signs[i].y, cell.width, cell.height}, Role::Background, 255});
        region.text.push_back({x,
                               signs[i].y,
                               std::u32string(1, signs[i].character),
                               {signs[i].role, Role::Background, kPlain}});
    }
}

void addMarkDot(Region& region, int x, int y, CellSize cell)
{
    const int side = std::max(cell.height / 4, 3);
    const int ring = std::max(hairline(cell), 1);
    const int top = y + (cell.height - side) / 2;
    region.under.push_back(
        {{x, top - ring, side + 2 * ring, side + 2 * ring}, Role::Background, 255});
    region.under.push_back({{x + ring, top, side, side}, Role::Text, 255});
}

Region trackBarRegion(const core::Song& song, std::size_t track, core::Tick tick,
                      const core::MeterMap& meter, Rect bounds, int width, CellSize cell)
{
    const auto section = song.sectionAt(tick);
    const Style label{Role::Dim, Role::Bar, kPlain};

    // The track's bar: each value after its label.
    Bar top;
    const auto pair = [&](std::vector<Segment>& group, std::string name, std::string value)
    {
        group.push_back({std::move(name), label});
        group.push_back({" " + std::move(value) + "     ", kBarText});
    };
    if (track == 0)
    {
        pair(top.left, "Track 0:", "Conductor");
    }
    else
    {
        const auto& shown = *song.tracks()[track - 1];
        pair(top.left, "Track " + std::to_string(track) + ":", shown.name);
        pair(top.left, "Channel:", std::to_string(shown.channel));
        pair(top.left, "Port:", shown.port.empty() ? "default" : shown.port);
    }
    const auto first = meter.toBarBeatTick(song.sectionStart(section)).bar;
    const auto last = meter.toBarBeatTick(song.sectionStart(section + 1) - 1).bar;
    pair(top.right, "Section:", song.sections()[section]->name());
    pair(top.right, "Bars:", std::to_string(first) + "–" + std::to_string(last));
    top.right.back().text.resize(top.right.back().text.size() - 5); // no space after the last

    auto region = layoutBar(top, width, cell, BarSpacing::forCell(cell));
    region.bounds = bounds;
    return region;
}

Put putRegister(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at)
{
    Put put;
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < reg.tracks.size() && firstTrack + i < tracks.size(); ++i)
    {
        auto added = reg.tracks[i];
        if (added.empty())
        {
            continue;
        }
        for (auto& event : added)
        {
            event.tick += at;
        }
        put.events += added.size();
        ++put.tracks;
        song.updateEvents(tracks[firstTrack + i]->id(), [&](core::EventList& events)
                          { events = core::merged(events, std::move(added)); });
    }
    return put;
}

core::Tick sectionEndAt(const core::Song& song, core::Tick tick)
{
    return song.sectionStart(song.sectionAt(tick) + 1);
}

SectionRange sectionsFrom(const core::Song& song, core::Tick tick, int count)
{
    SectionRange range;
    range.first = song.sectionAt(tick);
    range.count = std::min(static_cast<std::size_t>(std::max(count, 1)),
                           song.sections().size() - range.first);
    range.from = song.sectionStart(range.first);
    range.to = song.sectionStart(range.first + range.count);
    return range;
}

std::optional<std::string> sectionDescription(std::string_view op)
{
    if (op == "operator-delete")
    {
        return "this section, taken out of the song";
    }
    if (op == "operator-yank")
    {
        return "this section, as a section";
    }
    return std::nullopt;
}

std::string sectionsText(const core::Song& song, const SectionRange& range)
{
    const auto& name = song.sections()[range.first]->name();
    return range.count == 1 ? name : std::to_string(range.count) + " sections from " + name;
}

EventRegister rippleDelete(core::Song& song, std::size_t first, std::size_t last, core::Tick from,
                           core::Tick to)
{
    EventRegister removed;
    const auto end = sectionEndAt(song, from);
    to = std::min(to, end);
    removed.span = std::max<core::Tick>(to - from, 0);
    const auto tracks = song.tracks();
    for (auto i = first; i <= last && i < tracks.size(); ++i)
    {
        auto& taken = removed.tracks.emplace_back();
        song.updateEvents(tracks[i]->id(),
                          [&](core::EventList& events)
                          {
                              core::EventList kept;
                              for (auto event : events.all())
                              {
                                  if (event.tick >= from && event.tick < to)
                                  {
                                      event.tick -= from;
                                      taken.push_back(std::move(event));
                                      continue;
                                  }
                                  if (event.tick >= to && event.tick < end)
                                  {
                                      event.tick -= to - from;
                                  }
                                  kept.insert(std::move(event));
                              }
                              events = std::move(kept);
                          });
    }
    return removed;
}

EventRegister takeEvents(const core::Song& song, std::size_t first, std::size_t last,
                         core::Tick from, core::Tick to)
{
    EventRegister taken;
    const auto tracks = song.tracks();
    for (auto i = first; i <= last && i < tracks.size(); ++i)
    {
        auto& events = taken.tracks.emplace_back();
        for (auto event : song.events(tracks[i]->id()).startingIn(from, to))
        {
            event.tick -= from;
            events.push_back(std::move(event));
        }
    }
    taken.span = to - from;
    return taken;
}

std::string barsText(const core::MeterMap& meter, core::Tick from, core::Tick to)
{
    const auto first = meter.toBarBeatTick(from).bar;
    const auto last = meter.toBarBeatTick(std::max(to - 1, from)).bar;
    return first == last ? "bar " + std::to_string(first)
                         : "bars " + std::to_string(first) + "–" + std::to_string(last);
}

core::Tick registerSpan(const EventRegister& reg)
{
    if (reg.span > 0)
    {
        return reg.span;
    }
    core::Tick span = 0;
    for (const auto& events : reg.tracks)
    {
        for (const auto& event : events)
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            span =
                std::max(span, event.tick + (note ? std::max<core::Tick>(note->duration, 1) : 1));
        }
    }
    return span;
}

std::int64_t barsToInsert(const core::Song& song, const EventRegister& reg, std::size_t firstTrack,
                          core::Tick at)
{
    // The latest start once inserted: the events moved on, and the register's own.
    const auto end = sectionEndAt(song, at);
    const auto span = registerSpan(reg);
    const auto tracks = song.tracks();
    core::Tick latest = -1;
    for (std::size_t i = 0; i < reg.tracks.size() && firstTrack + i < tracks.size(); ++i)
    {
        for (const auto& event : reg.tracks[i])
        {
            latest = std::max(latest, at + event.tick);
        }
        for (const auto& event : song.events(tracks[firstTrack + i]->id()).startingIn(at, end))
        {
            latest = std::max(latest, event.tick + span);
        }
    }
    // A bar at a time, in the meter the section ends in, until it reaches.
    const auto section = song.sectionAt(at);
    auto longer = song;
    std::int64_t bars = 0;
    while (longer.sectionStart(section + 1) <= latest && longer.resizeSection(section, 1))
    {
        ++bars;
    }
    return bars;
}

Put rippleInsert(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at)
{
    const auto end = sectionEndAt(song, at);
    const auto span = registerSpan(reg);
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < reg.tracks.size() && firstTrack + i < tracks.size(); ++i)
    {
        song.updateEvents(tracks[firstTrack + i]->id(),
                          [&](core::EventList& events)
                          {
                              core::EventList moved;
                              for (auto event : events.all())
                              {
                                  if (event.tick >= at && event.tick < end)
                                  {
                                      event.tick += span;
                                  }
                                  moved.insert(std::move(event));
                              }
                              events = std::move(moved);
                          });
    }
    return putRegister(song, reg, firstTrack, at);
}

std::optional<core::EventQuery> queryFor(const core::EventData& data)
{
    core::EventQuery query;
    if (const auto* note = std::get_if<core::Note>(&data))
    {
        query.pitch = note->pitch;
    }
    else if (const auto* change = std::get_if<core::ControlChange>(&data))
    {
        query.kind = core::EventQuery::Kind::Controller;
        query.controller = change->controller;
    }
    else if (std::holds_alternative<core::PitchBend>(data))
    {
        query.kind = core::EventQuery::Kind::Bend;
    }
    else
    {
        return std::nullopt;
    }
    return query;
}

std::optional<std::string> describeTimeRange(std::string_view motion)
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 20> kRanges{{
        {"cursor-match-next", "to the next match"},
        {"object-match", "the next match"},
        {"cursor-match-previous", "from the match before"},
        {"search-next", "to the next match"},
        {"search-previous", "from the match before"},
        {"cursor-mark", "to the mark"},
        {"find-next", "through the next note named"},
        {"find-previous", "from the note named before"},
        {"find-repeat", "to the note found again"},
        {"find-repeat-back", "to the note found again, the other way"},
        {"cursor-find-next", "to the note found"},
        {"cursor-find-previous", "from the note found"},
        {"cursor-bar-line-next", "to the next bar line"},
        {"cursor-bar-line-previous", "from the bar line before"},
        {"cursor-section-next", "to the next section"},
        {"cursor-section-previous", "from the section's start"},
        {"cursor-label-next", "to the next label"},
        {"cursor-label-previous", "from the label before"},
        {"go-last", "to the end of the song"},
        {"go-first", "from the start of the song"},
    }};
    const auto it =
        std::ranges::find(kRanges, motion, &std::pair<std::string_view, std::string_view>::first);
    return it == kRanges.end() ? std::nullopt : std::optional{std::string{it->second}};
}

std::optional<core::Tick> startToEnd(const EventRegister& reg, core::Tick end)
{
    const auto at = end - registerSpan(reg);
    return at >= 0 ? std::optional{at} : std::nullopt;
}

std::string putPlace(const core::MeterMap& meter, core::Tick at, bool bars)
{
    return bars ? "bar " + std::to_string(meter.toBarBeatTick(at).bar) : positionText(meter, at);
}

std::string eventCount(std::size_t events)
{
    return events == 1 ? std::string{"1 event"} : std::to_string(events) + " events";
}

} // namespace modal::ui
