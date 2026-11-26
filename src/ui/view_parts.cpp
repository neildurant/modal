// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/controllers.hpp>
#include <modal/core/takes.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/bar_lines.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

void addTakeBands(Region& region, const core::Song& song, core::TrackId track,
                  const TimeScale& scale, core::Tick from, core::Tick to, int y, CellSize cell)
{
    constexpr std::array colours{Role::Accent, Role::Label, Role::Note};
    const int stroke = hairline(cell);
    const int inset = std::max(cell.height / 7, 1);
    const int height = cell.height - 2 * inset;
    for (auto section = song.sectionAt(from); section < song.sections().size(); ++section)
    {
        const auto start = song.sectionStart(section);
        if (start >= to)
        {
            break;
        }
        const auto* part = song.sections()[section]->content(track);
        if (!part || part->takes.empty())
        {
            continue;
        }
        const auto end = std::min(to, song.sectionStart(section + 1));
        for (const auto& span :
             core::activeTakeRanges(*part, std::max(from, start) - start, end - start))
        {
            const auto take = std::ranges::find(part->takes, span.take, &core::Take::id);
            if (take == part->takes.end())
            {
                continue;
            }
            const int first = std::max(scale.toX(start + span.from), 0);
            if (first >= region.bounds.width)
            {
                continue;
            }
            const int last = std::clamp(scale.toX(start + span.to), first + 1, region.bounds.width);
            const int width = last - first;
            const int left = first + (width > stroke ? stroke : 0);
            const auto index = static_cast<std::size_t>(take - part->takes.begin());
            const auto colour = colours[index % colours.size()];
            region.under.push_back({{left, y + inset, last - left, height}, colour, 100});

            const auto text = decodeUtf8("T" + std::to_string(index + 1));
            if (last - left >= static_cast<int>(text.size()) * cell.width + 2 * stroke)
            {
                region.text.push_back(
                    {left + stroke, y, text, {Role::Text, Role::Background, kBold}});
            }
            else
            {
                region.under.push_back(
                    {{left, y + inset, std::min(stroke, last - left), height}, colour, 165});
            }
        }
    }
}

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};

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
    const auto drum = std::ranges::find(inspection.fields, "Drum", &InspectorField::heading);
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
    const bool editing = editor != nullptr && editor->active();
    for (const auto& slot : slots)
    {
        auto it = std::ranges::find(inspection.fields, slot.field, &InspectorField::heading);
        if (it == inspection.fields.end())
            continue;
        auto shown = *it;
        shown.editable = it->editable && editing;
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
                    {x(kExactColumn + exactWidth + 2 + (drum == inspection.fields.end() ? 0 : 8)),
                     y(0), U"Unit rounded to ticks", label});
            value = value.substr(0, value.find(" ("));
        }
        if (slot.field == "Position" || slot.field == "Length" || slot.field == "Ends")
            value = aligned(std::move(value));
        inspectorValue(region, shown, decodeUtf8(value), x(slot.valueColumn), y(slot.row),
                       slot.width, cell, editor, true);
    }
    // Keep the timing fields fixed while giving a drum articulation its full name in the
    // space beside them. Two rows leave the panel's Keep/Cancel row clear while editing.
    if (drum != inspection.fields.end())
    {
        const int column = kExactColumn + exactWidth + 3;
        const int room = std::max(columns - (column - left), 0);
        if (room > 0)
        {
            region.text.push_back({x(column), y(0), U"Drum:", label});
            const auto name = decodeUtf8(drum->value);
            for (std::size_t row = 0; row < 2; ++row)
            {
                const auto first = row * static_cast<std::size_t>(room);
                if (first >= name.size())
                    break;
                auto part = name.substr(first, static_cast<std::size_t>(room));
                if (row == 1 && first + part.size() < name.size())
                    part.back() = U'…';
                region.text.push_back({x(column),
                                       y(static_cast<int>(row) + 1),
                                       std::move(part),
                                       {Role::Text, Role::Panel, kPlain}});
            }
        }
    }
    return region;
}

} // namespace

int inspectorHeight(CellSize cell)
{
    return 2 * cell.height + (cell.height / 4) * 2;
}

void addField(Region& region, int x, int y, int columns, CellSize cell)
{
    const auto edge = hairline(cell);
    region.under.push_back(
        {{x, y + edge, columns * cell.width, std::max(1, cell.height - 2 * edge)}, Role::Field});
}

void addTrackButton(Region& region, int x, int y, CellSize cell, TrackField field,
                    const core::Track& track, Role background, bool cursor, bool recording)
{
    const bool on = trackButtonOn(field, track);
    const bool filled = on && (field != TrackField::Thru || track.arm);
    const auto colour = field == TrackField::Mute         ? Role::TrackMute
                        : field == TrackField::Solo       ? Role::TrackSolo
                        : field == TrackField::Arm        ? Role::TrackArm
                        : field == TrackField::Thru       ? Role::TrackThru
                        : field == TrackField::ChaseNotes ? Role::TrackChase
                                                          : Role::TrackQuantize;
    const auto letter = field == TrackField::Arm && track.arm && recording
                            ? std::u32string{U"R"}
                            : decodeUtf8(toggleLetter(field));
    const auto badge = U" " + letter + U" ";
    const auto edge = hairline(cell);
    const int w = kTrackButtonColumns * cell.width;
    // Text backgrounds are painted before shapes; only the badge carries a glyph.
    region.under.push_back(
        {{x, y + edge, w, std::max(1, cell.height - 2 * edge)}, filled ? colour : Role::Panel});
    region.text.push_back({x,
                           y,
                           badge,
                           {filled ? Role::ToggleText
                            : on   ? colour
                                   : Role::Dim,
                            background, kBold}});
    if (cursor)
    {
        region.over.push_back({{x, y, w, edge}, Role::Cursor});
        region.over.push_back({{x, y + cell.height - edge, w, edge}, Role::Cursor});
        region.over.push_back({{x, y, edge, cell.height}, Role::Cursor});
        region.over.push_back({{x + w - edge, y, edge, cell.height}, Role::Cursor});
    }
}

void inspectorValue(Region& region, const InspectorField& field, std::u32string text, int x, int y,
                    int columns, CellSize cell, const InspectorEditor* editor, bool rightAligned,
                    Rect* listAt)
{
    if (columns < 1)
        return;
    const bool edited = editor && editor->active() && editor->heading() == field.heading;
    const auto box = static_cast<std::size_t>(columns);
    // A field chosen from a list: its value, then an arrow at its right.
    const bool listed = !field.choices.empty() || (edited && editor->list() != nullptr);
    if (listed && box >= 3)
    {
        if (edited)
        {
            text = decodeUtf8(editor->shown());
            if (listAt != nullptr)
                *listAt = {region.bounds.x + x, region.bounds.y + y, columns * cell.width,
                           cell.height};
        }
        if (text.size() > box - 2)
            text = text.substr(0, box - 3) + U'…';
        if (rightAligned)
            text.insert(0, box - 2 - text.size(), U' ');
        else
            text.resize(box - 2, U' ');
        text += std::u32string{U' ', ChoiceList::kArrow};
        // A control, as a typed field is: its value and arrow on one field.
        if (!edited && field.editable)
            addField(region, x, y, columns, cell);
        region.text.push_back({x, y, std::move(text),
                               edited           ? Style{Role::Text, Role::Selection, kPlain}
                               : field.warning  ? Style{Role::Warning, Role::Panel, kPlain}
                               : field.editable ? Style{Role::Text, Role::Panel, kPlain}
                                                : Style{Role::Dim, Role::Panel, kPlain}});
        return;
    }
    if (edited)
    {
        text = editor->line().text();
        const auto typed = editor->line().cursor();
        const bool whole = editor->replacing() && !text.empty();
        // A value keeps its place as editing starts: only a cursor past its end takes a cell,
        // moving a right-aligned value along by one then.
        const auto used = text.size() + (!whole && typed == text.size() ? 1 : 0);
        const auto upTo = whole ? text.size() : typed + 1;
        const std::size_t first = upTo > box ? upTo - box : 0;
        const auto pad = rightAligned && first == 0 && used <= box ? box - used : 0;
        auto visible = std::u32string(pad, U' ') + text.substr(first, box - pad);
        visible.resize(box, U' ');
        region.text.push_back({x, y, visible, {Role::Text, Role::Selection, kPlain}});
        const Style cursor{Role::CursorText, Role::Cursor, kPlain};
        if (whole)
        {
            // All of it highlighted: typing replaces it, until a key moves the cursor.
            region.text.push_back({x + static_cast<int>(pad) * cell.width, y,
                                   visible.substr(pad, std::min(text.size() - first, box - pad)),
                                   cursor});
        }
        else
        {
            region.text.push_back({x + static_cast<int>(pad + typed - first) * cell.width, y,
                                   std::u32string(1, typed < text.size() ? text[typed] : U' '),
                                   cursor});
        }
    }
    else
    {
        if (text.size() > box)
            text = text.substr(0, box - 1) + U'…';
        // Free text to type shows as a field, across its room, even when empty.
        const bool asField = field.freeText && field.editable;
        if (asField)
        {
            text.resize(box, U' ');
            addField(region, x, y, columns, cell);
        }
        if (rightAligned)
            x += static_cast<int>(box - text.size()) * cell.width;
        region.text.push_back({x,
                               y,
                               std::move(text),
                               {field.warning    ? Role::Warning
                                : field.editable ? Role::Text
                                                 : Role::Dim,
                                Role::Panel, kPlain}});
    }
}

void addChoiceList(Frame& frame, const InspectorEditor* editor, Rect listAt, Rect within,
                   CellSize cell)
{
    if (editor != nullptr && editor->active() && editor->list() != nullptr)
        frame.push_back(editor->list()->region(listAt, within, cell));
}

int panelButtonsY(Rect bounds, CellSize cell)
{
    return bounds.height - cell.height - cell.height / 4;
}

namespace
{

// A panel's Keep and Cancel buttons, at its bottom right while editing, the one Tab is on lit as
// the cursor is.
void addPanelButtons(Region& region, const InspectorEditor* editor, CellSize cell)
{
    // Only while editing (decided 17 November 2026, after use): browsing, the corner is the
    // view's own, as the piano roll's New note is.
    const bool editing = editor != nullptr && editor->panel();
    if (!editing)
        return;
    const int margin = BarSpacing::forCell(cell).margin;
    int x = region.bounds.width - margin - kPanelButtonColumns * cell.width;
    const int y = panelButtonsY(region.bounds, cell);
    for (const auto button : {InspectorEditor::kKeep, InspectorEditor::kCancel})
    {
        const bool on = editing && editor->heading() == button;
        auto text = U"[ " + decodeUtf8(button) + U" ]";
        const Style style = on        ? Style{Role::CursorText, Role::Cursor, kBold}
                            : editing ? Style{Role::Text, Role::Panel, kPlain}
                                      : Style{Role::Dim, Role::Panel, kPlain};
        const auto width = static_cast<int>(text.size());
        region.text.push_back({x, y, std::move(text), style});
        x += (width + 2) * cell.width;
    }
}

// The panel tinted while a field in it is being edited, as the controller lane is while the
// cursor's in it, so where the keys go shows (asked for 17 November 2026, after use).
constexpr std::uint8_t kEditingTint = 80;

Region tintedWhileEditing(Region region, const InspectorEditor* editor)
{
    if (editor != nullptr && editor->active())
        region.under.insert(region.under.begin(),
                            Shape{{0, 0, region.bounds.width, region.bounds.height},
                                  Role::Selection,
                                  kEditingTint});
    return region;
}

} // namespace

Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth, const InspectorEditor* editor, Rect* listAt, bool panel)
{
    const bool buttons = panel && std::ranges::any_of(inspection.fields, &InspectorField::editable);
    if (inspection.kind == "Note" &&
        std::ranges::find(inspection.fields, "Units", &InspectorField::heading) !=
            inspection.fields.end())
    {
        auto region = noteInspectorRegion(inspection, bounds, width, cell, positionWidth, editor);
        if (buttons)
            addPanelButtons(region, editor, cell);
        return tintedWhileEditing(std::move(region), editor);
    }
    // On a panel of its own: the kind of row, then headings above their values. Each column
    // is as wide as its longest value can be, so the columns stay put as the cursor moves;
    // a value that doesn't fit is cut short, and those after it left out.
    Region region{.bounds = bounds, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const int margin = BarSpacing::forCell(cell).margin;
    // Clear of the buttons, where they share the values' row, as in an inspector of two rows.
    const bool sharing = buttons && panelButtonsY(bounds, cell) <= cell.height / 4 + cell.height;
    const int columns =
        std::max((width - 2 * margin) / cell.width - (sharing ? kPanelButtonColumns + 3 : 0), 0);
    const bool editing = editor != nullptr && editor->active();
    const Style kind{Role::Accent, Role::Panel, kBold};
    const Style heading{Role::Dim, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell.width; };
    const auto widthOf = [&](std::string_view name)
    {
        constexpr std::array<std::pair<std::string_view, int>, 21> kWidths{{
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
            // A ramp's, each as wide as its widest value, so none moves another as it
            // changes or is edited (decided 17 November 2026, after use).
            {"From", 6},
            {"To", 6},
            {"Shape", 13}, // Equal power, and the list's arrow
            {"Amount", 6},
            {"Density", 12},
        }};
        if (name == "Position" || name == "Ends" || name == "Start" || name == "End")
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
        auto shown = field;
        shown.editable = field.editable && editing;
        if (field.underKind)
        {
            inspectorValue(region, shown, decodeUtf8(field.value), x(0), top + cell.height,
                           widthOf("Kind"), cell, editor);
            continue;
        }
        auto title = decodeUtf8(field.heading);
        auto text = decodeUtf8(field.value);
        const bool edited =
            editor != nullptr && editor->active() && editor->heading() == field.heading;
        if (edited && editor->list() == nullptr)
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
        // Room for the cursor while editing, and for a list's arrow.
        const int extra = !field.choices.empty() ? 2 : edited ? 1 : 0;
        const int wide = std::max({static_cast<int>(title.size()),
                                   static_cast<int>(text.size()) + extra, widthOf(field.heading)});
        inspectorValue(region, shown, std::move(text), x(at), top + cell.height,
                       std::min(wide, room), cell, editor, false, listAt);
        at += wide + 3;
    }
    if (buttons)
        addPanelButtons(region, editor, cell);
    return tintedWhileEditing(std::move(region), editor);
}

void addRowSigns(Region& region, std::vector<RowSign> signs, CellSize cell)
{
    std::ranges::stable_sort(
        signs, [](const RowSign& a, const RowSign& b)
        { return std::tuple{a.y, a.x, !a.text.empty()} < std::tuple{b.y, b.x, !b.text.empty()}; });
    int right = 0; // the last's in the row
    for (std::size_t i = 0; i < signs.size(); ++i)
    {
        auto x = signs[i].x;
        if (i > 0 && signs[i - 1].y == signs[i].y && signs[i].moves)
        {
            x = std::max(x, right);
        }
        const bool next = i + 1 < signs.size() && signs[i + 1].y == signs[i].y;
        if (const auto& text = signs[i].text; !text.empty())
        {
            // Half a space after what it follows, so a tempo and a meter don't read as one
            // number, while a mark a beat on leaves room.
            if (i > 0 && signs[i - 1].y == signs[i].y && right >= signs[i].x)
            {
                x = right + cell.width / 2;
            }
            const int end = x + static_cast<int>(text.size()) * cell.width;
            if (!next || signs[i + 1].x >= end)
            {
                region.under.push_back(
                    {{x, signs[i].y, end - x, cell.height}, Role::Background, 255});
                region.text.push_back(
                    {x, signs[i].y, text, {signs[i].role, Role::Background, kPlain}});
                right = end;
            }
            else
            {
                // A tick in the lower part of the row, so the next sign can still be read.
                const int line = std::max(hairline(cell), 1);
                const int height = cell.height / 3;
                region.under.push_back(
                    {{x, signs[i].y + cell.height - height, line, height}, signs[i].role, 255});
                right = x + 2 * line;
            }
            continue;
        }
        right = x + cell.width;
        if (const auto& label = signs[i].label; !label.empty())
        {
            const int end = right + static_cast<int>(label.size()) * cell.width;
            // A text, a meter, goes after the label rather than hiding it.
            const bool room = !next || signs[i + 1].x >= end || !signs[i + 1].text.empty();
            if (room)
            {
                region.text.push_back(
                    {right, signs[i].y, label, {signs[i].role, Role::Background, kPlain}});
                right = end;
            }
        }
        region.under.push_back({{x, signs[i].y, cell.width, cell.height}, Role::Background, 255});
        region.text.push_back({x,
                               signs[i].y,
                               std::u32string(1, signs[i].character),
                               {signs[i].role, Role::Background, kPlain}});
    }
}

void addTempoSigns(std::vector<RowSign>& signs, const core::TempoMap& tempo, core::Tick from,
                   core::Tick to, int y, const std::function<int(core::Tick)>& toX)
{
    std::optional<core::Tempo> last;
    for (const auto& change : tempo.changes())
    {
        if (last && change.tempo != *last && change.tick >= from && change.tick < to)
        {
            signs.push_back({toX(change.tick), y, U'\u25B2', Role::EventSign,
                             decodeUtf8(std::to_string(std::lround(change.tempo.bpm())))});
        }
        last = change.tempo;
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

void addRangeBracket(Region& region, const RangeBracket& bracket, int bottom, CellSize cell)
{
    if (bracket.x1 <= bracket.x0)
        return;
    const int thick = std::max(hairline(cell), 2);
    if (bracket.lifted)
        bottom -= 2 * thick;
    const int y = bottom - thick;
    if (bracket.dashed)
    {
        // Dashes from its start, half a cell long with a quarter between.
        const int dash = std::max(cell.width / 2, 2);
        const int gap = std::max(cell.width / 4, 1);
        for (int x = bracket.x0; x < bracket.x1; x += dash + gap)
            region.over.push_back(
                {{x, y, std::min(dash, bracket.x1 - x), thick}, bracket.role, 255});
    }
    else
        region.over.push_back({{bracket.x0, y, bracket.x1 - bracket.x0, thick}, bracket.role, 255});
    const int cap = std::max(cell.height / 3, thick);
    if (bracket.startShown)
        region.over.push_back({{bracket.x0, bottom - cap, thick, cap}, bracket.role, 255});
    if (bracket.endShown)
        region.over.push_back({{bracket.x1 - thick, bottom - cap, thick, cap}, bracket.role, 255});
}

void addRangeBrackets(Region& region, const core::ArmedSpans& ranges, core::Tick left,
                      core::Tick right, const std::function<int(core::Tick)>& toX, int width,
                      int bottom, CellSize cell)
{
    const auto bracket = [&](core::ArmedSpan span) -> std::optional<RangeBracket>
    {
        if (span.to <= left || span.from >= right)
            return std::nullopt;
        return RangeBracket{span.from >= left ? toX(span.from) : 0,
                            span.to <= right ? toX(span.to) : width, span.from >= left,
                            span.to <= right};
    };
    if (ranges.punch)
    {
        if (auto punch = bracket(*ranges.punch))
        {
            punch->role = ranges.punchOn ? Role::IndicatorArm : Role::Dim;
            addRangeBracket(region, *punch, bottom, cell);
        }
    }
    if (ranges.loop)
    {
        if (auto loop = bracket(*ranges.loop))
        {
            loop->role = ranges.loopOn ? Role::Accent : Role::Dim;
            loop->dashed = false;
            loop->lifted = ranges.punch && ranges.punch->from < ranges.loop->to &&
                           ranges.loop->from < ranges.punch->to;
            addRangeBracket(region, *loop, bottom, cell);
        }
    }
}

std::string padText(std::string text, std::size_t width, bool alignRight)
{
    const auto length = decodeUtf8(text).size();
    if (length >= width)
    {
        return text;
    }
    const std::string padding(width - length, ' ');
    return alignRight ? padding + text : text + padding;
}

namespace
{

std::string meterText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

// Text cut short with an ellipsis at a number of characters.
std::string cutText(const std::string& text, std::size_t most)
{
    auto characters = decodeUtf8(text);
    if (characters.size() <= most)
    {
        return text;
    }
    characters.resize(most - 1);
    characters += U'…';
    return encodeUtf8(characters);
}

constexpr std::size_t kSectionNameWidth = 16;
constexpr std::size_t kTrackNameWidth = 32; // as the Tracks view's Name column
constexpr std::size_t kPortNameWidth = 24;  // as its Output column
constexpr std::size_t kMeterWidth = 5;      // up to 12/16

} // namespace

std::string whenText(core::MetronomeSettings::When when)
{
    using enum core::MetronomeSettings::When;
    return when == Off ? "off" : when == Recording ? "while recording" : "always";
}

std::string countInText(const core::CountInSettings& countIn)
{
    const auto count = [](int n, std::string_view one)
    { return std::to_string(n) + " " + std::string{one} + (n == 1 ? "" : "s"); };
    switch (countIn.mode)
    {
    case core::CountInSettings::Mode::None:
        break;
    case core::CountInSettings::Mode::CountIn:
        return "Count-in " +
               (countIn.beats > 0 ? count(countIn.beats, "beat") : count(countIn.bars, "bar"));
    case core::CountInSettings::Mode::PreRoll:
        return "Pre-roll " + (countIn.preRollBeats > 0 ? count(countIn.preRollBeats, "beat")
                                                       : count(countIn.preRollBars, "bar"));
    }
    return "No count-in";
}

PlaceParts placeParts(const core::Song& song, const core::MeterMap& meter, core::Tick tick)
{
    // The song's last place is the widest a place in it needs; a time past it, its own.
    const auto last = std::max(song.end(), tick);
    const auto time = [&](core::Tick at) { return songTime(song.tempo().toTime(at)); };
    auto position = positionText(meter, tick);
    auto clock = time(tick);
    const auto positionWidth = decodeUtf8(positionText(meter, last)).size();
    const auto timeWidth = decodeUtf8(time(last)).size();
    return {padText(std::move(position), positionWidth, true),
            padText(std::move(clock), timeWidth, true),
            padText(meterText(meter.signatureAt(tick)), kMeterWidth, false)};
}

std::string tempoText(const core::Song& song, core::Tick tick)
{
    std::size_t widest = 0;
    for (const auto& change : song.tempo().changes())
    {
        widest = std::max(widest, core::bpmText(change.tempo).size());
    }
    return padText(core::bpmText(song.tempo().tempoAt(tick)), widest, true) + " BPM";
}

std::string sectionText(const core::Song& song, core::Tick tick, std::size_t most)
{
    most = std::clamp<std::size_t>(most, 2, kSectionNameWidth);
    std::size_t longest = 0;
    for (const auto& section : song.sections())
    {
        longest = std::max(longest, decodeUtf8(section->name()).size());
    }
    longest = std::min(longest, most);
    return padText(cutText(song.sections()[song.sectionAt(tick)]->name(), most), longest, false);
}

Region labelledBar(const std::vector<BarPair>& left, const std::vector<BarPair>& right, Rect bounds,
                   int width, CellSize cell)
{
    constexpr std::size_t kBetween = 3; // spaces between one pair and the next
    const auto spacing = BarSpacing::forCell(cell);
    const auto room =
        cell.width > 0 ? static_cast<std::size_t>(
                             std::max((width - 2 * spacing.margin - spacing.gap) / cell.width, 0))
                       : std::size_t{0};
    const auto length = [](const BarPair& pair)
    {
        return decodeUtf8(pair.label).size() +
               (pair.value.empty() ? 0 : 1 + decodeUtf8(pair.value).size());
    };
    // Shown unless it's among those left out: those dropped at `dropped` or higher.
    const auto shown = [](const BarPair& pair, int dropped)
    { return pair.drop == 0 || pair.drop < dropped; };
    const auto needs = [&](int dropped)
    {
        std::size_t total = 0;
        for (const auto* group : {&left, &right})
        {
            std::size_t count = 0;
            for (const auto& pair : *group)
            {
                if (shown(pair, dropped))
                {
                    total += length(pair) + (count++ > 0 ? kBetween : 0);
                }
            }
        }
        return total;
    };
    // Leaving out the first to go, then the next, until it fits or only what stays is left.
    int highest = 0;
    for (const auto* group : {&left, &right})
    {
        for (const auto& pair : *group)
        {
            highest = std::max(highest, pair.drop);
        }
    }
    int dropped = highest + 1; // nothing left out
    while (dropped > 1 && needs(dropped) > room)
    {
        --dropped;
    }
    const Style label{Role::Dim, Role::Bar, kPlain};
    Bar bar;
    const auto add = [&](std::vector<Segment>& segments, const std::vector<BarPair>& pairs)
    {
        for (const auto& pair : pairs)
        {
            if (!shown(pair, dropped))
            {
                continue;
            }
            if (!segments.empty())
            {
                segments.push_back({std::string(kBetween, ' '), kBarText});
            }
            segments.push_back({pair.label, label});
            if (!pair.value.empty())
            {
                segments.push_back({" " + pair.value, kBarText});
            }
        }
    };
    add(bar.left, left);
    add(bar.right, right);
    auto region = layoutBar(bar, width, cell, spacing);
    region.bounds = bounds;
    return region;
}

std::vector<BarPair> trackPairs(const core::Song& song, std::size_t track)
{
    const auto tracks = song.tracks();
    std::size_t longestName = 0;
    std::size_t longestPort = 0;
    for (const auto& each : tracks)
    {
        longestName = std::max(longestName, decodeUtf8(each->name).size());
        longestPort =
            std::max(longestPort, decodeUtf8(each->port.empty() ? "Default" : each->port).size());
    }
    const auto numberWidth = std::string{"Track :"}.size() + std::to_string(tracks.size()).size();
    if (track == 0 || track > tracks.size())
    {
        return {{padText("Track 0:", numberWidth, false), "Conductor"}};
    }
    const auto& shown = *tracks[track - 1];
    std::vector<BarPair> pairs{{padText("Track " + std::to_string(track) + ":", numberWidth, false),
                                padText(shown.name, std::min(longestName, kTrackNameWidth), false)},
                               {"Channel:", padText(std::to_string(shown.channel), 2, true)},
                               {"Output:", padText(shown.port.empty() ? "Default" : shown.port,
                                                   std::min(longestPort, kPortNameWidth), false)}};
    // Last, as they come and go from track to track.
    if (shown.quantize.on)
    {
        pairs.push_back({"Quantise:", quantizeSummary(shown), kDropQuantise});
    }
    const auto& setup = shown.programSetup;
    if (setup.program)
    {
        pairs.push_back({"Program:", std::to_string(*setup.program), kDropProgram});
    }
    if (setup.bankMsb || setup.bankLsb)
    {
        const auto part = [](std::optional<std::uint8_t> value)
        { return value ? std::to_string(*value) : std::string{"-"}; };
        pairs.push_back({"Bank:", part(setup.bankMsb) + "/" + part(setup.bankLsb), kDropBank});
    }
    return pairs;
}

std::vector<BarPair> cursorPlacePairs(const core::Song& song, const core::MeterMap& meter,
                                      core::Tick tick, std::optional<core::TrackId> track)
{
    auto parts = placeParts(song, meter, tick);
    const auto ownIn = [&](std::size_t section)
    {
        const auto* content = song.sections()[section]->content(*track);
        return content != nullptr && content->meter.has_value();
    };
    // Only for a track with a meter of its own somewhere, so other songs lose no room; as wide
    // in every section, so nothing moves going from one to the next.
    if (track &&
        std::ranges::any_of(std::views::iota(std::size_t{0}, song.sections().size()), ownIn))
    {
        parts.meter = padText(meterText(meter.signatureAt(tick)) +
                                  (ownIn(song.sectionAt(tick)) ? " own" : ""),
                              kMeterWidth + 4, false);
    }
    return {{"Position:", std::move(parts.position)},
            {"Time:", std::move(parts.time)},
            {"Meter:", std::move(parts.meter)}};
}

Region trackBarRegion(const core::Song& song, std::size_t track, std::optional<core::Tick> tick,
                      const core::MeterMap& meter, Rect bounds, int width, CellSize cell)
{
    std::vector<BarPair> right;
    if (tick)
    {
        // The section and its bars, which vary, before the cursor's place, whose widths are
        // fixed, so nothing to their right moves.
        const auto section = song.sectionAt(*tick);
        const auto first = meter.toBarBeatTick(song.sectionStart(section)).bar;
        const auto last = meter.toBarBeatTick(song.sectionStart(section + 1) - 1).bar;
        right.push_back({"Section:", song.sections()[section]->name(), kDropSection});
        right.push_back({"Bars:", std::to_string(first) + "–" + std::to_string(last), kDropBars});
        const auto id = track > 0 && track <= song.tracks().size()
                            ? std::optional{song.tracks()[track - 1]->id()}
                            : std::nullopt;
        std::ranges::move(cursorPlacePairs(song, meter, *tick, id), std::back_inserter(right));
    }
    return labelledBar(trackPairs(song, track), right, bounds, width, cell);
}

Put putRegister(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at)
{
    // Into each track's own events, so what goes under a ramp is kept beneath it, with the
    // register's ramps.
    Put put;
    const auto tracks = song.tracks();
    for (std::size_t i = 0;
         i < std::max(reg.tracks.size(), reg.ramps.size()) && firstTrack + i < tracks.size(); ++i)
    {
        auto added = i < reg.tracks.size() ? reg.tracks[i] : std::vector<core::Event>{};
        auto ramps = i < reg.ramps.size() ? reg.ramps[i] : std::vector<core::ControllerRamp>{};
        if (added.empty() && ramps.empty())
        {
            continue;
        }
        for (auto& event : added)
        {
            event.tick += at;
        }
        for (auto& ramp : ramps)
        {
            ramp.start += at;
        }
        const auto id = tracks[firstTrack + i]->id();
        const auto count = added.size();
        const auto done = song.updateTrackContent(
            id,
            [&](core::EventList& source, std::vector<core::ControllerRamp>& kept)
            {
                source = core::merged(source, std::move(added));
                std::ranges::copy(ramps, std::back_inserter(kept));
            });
        if (!done.done())
        {
            put.refused = done;
            put.refusedTrack = id;
            return put;
        }
        put.events += count;
        put.ramps += ramps.size();
        ++put.tracks;
    }
    putBars(song, reg, firstTrack, at);
    return put;
}

void takeBars(EventRegister& reg, const core::Song& song, std::size_t first, core::Tick from)
{
    const auto tracks = song.tracks();
    const auto span = registerSpan(reg);
    reg.meters.clear();
    for (auto i = first; i < first + reg.tracks.size() && i < tracks.size(); ++i)
    {
        const auto& meter = song.meter(tracks[i]->id());
        core::MeterMap bars{song.ppqn(), meter.signatureAt(from)};
        for (const auto& change : meter.changes())
        {
            if (change.tick > from && change.tick < from + span)
                bars.set(change.tick - from, change.signature);
        }
        reg.meters.push_back(std::move(bars));
    }
}

bool putBars(core::Song& song, const EventRegister& reg, std::size_t firstTrack, core::Tick at)
{
    if (!reg.bars)
        return false;
    const auto tracks = song.tracks();
    const auto span = registerSpan(reg);
    bool changed = false;
    for (std::size_t i = 0; i < reg.meters.size() && firstTrack + i < tracks.size(); ++i)
        changed = song.keepBars(tracks[firstTrack + i]->id(), at, span, reg.meters[i]) || changed;
    return changed;
}

std::string keptBarsText(const core::Song& song, const EventRegister& reg, std::size_t firstTrack,
                         core::Tick at)
{
    if (!reg.bars || reg.meters.empty())
        return {};
    auto tried = song; // cheap: copies share their tracks and sections
    if (!putBars(tried, reg, firstTrack, at))
        return {};
    const auto signature = reg.meters.front().signatureAt(0);
    return ", keeping its " + std::to_string(signature.numerator) + "/" +
           std::to_string(signature.denominator);
}

std::optional<std::string> rampEventEditRefusal(const core::Song& song, core::TrackId track,
                                                const core::EventList& changed)
{
    const auto ramps = song.ramps(track);
    if (const auto index = core::changedRampPoints(song.events(track), changed, ramps))
    {
        return "That would change the protected " + laneTitle(ramps[*index].lane) + " ramp in " +
               song.track(track)->name + "; , c R makes it ordinary events first";
    }
    return std::nullopt;
}

std::string laneTitle(const core::Lane& lane)
{
    switch (lane.kind)
    {
    case core::Lane::Kind::Controller:
    {
        const auto name = core::controllerName(lane.controller);
        return std::to_string(lane.controller) + (name.empty() ? "" : " " + name);
    }
    case core::Lane::Kind::PitchBend:
        return "Pitch bend";
    case core::Lane::Kind::Pressure:
        return "Aftertouch";
    case core::Lane::Kind::Velocity:
        break;
    }
    return "Velocity";
}

std::string shapeName(core::ControllerRamp::Shape shape)
{
    for (const auto& [each, name] : kRampShapes)
    {
        if (each == shape)
        {
            return std::string{name};
        }
    }
    return "Linear";
}

Inspection rampInspection(const core::ControllerRamp& ramp, const core::MeterMap& meter)
{
    Inspection result;
    result.kind = "Ramp";
    // Dim but while its draft is open, as every inspector's values are (decided 17 November
    // 2026, after use), which the drawing sees to.
    const auto add =
        [&](std::string heading, std::string value, std::string editText = {}, bool editable = true)
    {
        result.fields.push_back({.heading = std::move(heading),
                                 .value = std::move(value),
                                 .editable = editable,
                                 .editText = std::move(editText)});
    };
    add("Start", positionText(meter, ramp.start));
    add("End", positionText(meter, ramp.end()));
    add("From", std::to_string(ramp.from));
    add("To", std::to_string(ramp.to));
    add("Shape", shapeName(ramp.shape));
    auto& shape = result.fields.back();
    for (const auto& [each, name] : kRampShapes)
    {
        shape.choices.emplace_back(name);
    }
    // The curve's amount always in its place, so the fields after it don't move as the shape
    // changes (decided 17 November 2026, after use); a dash, and passed over, for another shape.
    if (ramp.shape == core::ControllerRamp::Shape::Curve)
    {
        add("Amount", (ramp.amount > 0 ? "+" : "") + std::to_string(ramp.amount),
            std::to_string(ramp.amount));
    }
    else
    {
        add("Amount", "-", {}, false);
    }
    add("Density", std::to_string(ramp.density) + " a quarter", std::to_string(ramp.density));
    result.fields.push_back({"Controller", laneTitle(ramp.lane)});
    return result;
}

std::optional<RampCut> rampCut(const core::Song& song, std::size_t first, std::size_t last,
                               core::Tick at)
{
    const auto tracks = song.tracks();
    for (auto i = first; i <= last && i < tracks.size(); ++i)
    {
        const auto ramps = song.ramps(tracks[i]->id());
        for (std::size_t r = 0; r < ramps.size(); ++r)
        {
            if (ramps[r].start < at && ramps[r].end() >= at)
            {
                return RampCut{tracks[i]->id(), r};
            }
        }
    }
    return std::nullopt;
}

std::optional<RampCut> rampCut(const core::Song& song, std::size_t first, std::size_t last,
                               core::Tick from, core::Tick to)
{
    if (auto cut = rampCut(song, first, last, from))
    {
        return cut;
    }
    return rampCut(song, first, last, to);
}

std::string rampRefusalText(const core::Song::RampEdit& edit)
{
    using Refusal = core::Song::RampRefusal;
    switch (edit.refusal)
    {
    case Refusal::Collides:
        return "A ramp would touch or overlap another of its controller there";
    case Refusal::CrossesSection:
        return "A ramp would run past its section's end";
    case Refusal::PastEnd:
        return "A ramp would run past the song's end";
    case Refusal::Invalid:
        return "A ramp wouldn't fit there";
    case Refusal::NoTrack:
    case Refusal::NoRamp:
    case Refusal::None:
        break;
    }
    return "There's no ramp there";
}

std::optional<std::string> shorteningCuts(const core::Song& song, std::size_t section,
                                          std::int64_t bars)
{
    if (bars >= 0 || section >= song.sections().size())
    {
        return std::nullopt;
    }
    const auto& meter = song.sections()[section]->meter();
    const auto last = meter.toBarBeatTick(song.sections()[section]->length() - 1).bar;
    if (last + bars < 1)
    {
        return std::nullopt;
    }
    const auto end = song.sectionStart(section) + meter.barStart(last + bars + 1);
    if (const auto ramp = song.rampAcross(end))
    {
        return rampCutText(song, RampCut{ramp->track, ramp->index});
    }
    return std::nullopt;
}

std::size_t rampCount(const EventRegister& reg)
{
    std::size_t count = 0;
    for (const auto& ramps : reg.ramps)
    {
        count += ramps.size();
    }
    return count;
}

std::optional<std::string> rampPutRefusal(const core::Song& song, const EventRegister& reg,
                                          std::size_t firstTrack, core::Tick at, bool insert,
                                          std::int64_t bars)
{
    const auto width = std::max<std::size_t>(std::max(reg.tracks.size(), reg.ramps.size()), 1);
    if (insert)
    {
        if (const auto cut = rampCut(song, firstTrack, firstTrack + width - 1, at))
        {
            return rampCutText(song, *cut);
        }
    }
    if (rampCount(reg) == 0)
    {
        return std::nullopt;
    }
    auto trial = song;
    Put put;
    if (insert)
    {
        if (bars > 0)
        {
            trial.resizeSection(trial.sectionAt(at), bars);
        }
        put = rippleInsert(trial, reg, firstTrack, at);
    }
    else
    {
        put = putRegister(trial, reg, firstTrack, at);
    }
    return put.refused ? std::optional{rampRefusalText(*put.refused)} : std::nullopt;
}

std::string rampCutText(const core::Song& song, const RampCut& cut)
{
    const auto& ramp = song.ramps(cut.track)[cut.index];
    const auto* track = song.track(cut.track);
    return "That would cut the " + core::toString(ramp.lane) + " ramp at " +
           positionText(song.meter(cut.track), ramp.start) +
           (track != nullptr && !track->name.empty() ? " in " + track->name : std::string{}) +
           ": move it, or , c R makes it events";
}

std::string sectionsAddedText(const core::Song& song, std::size_t before)
{
    const auto sections = song.sections();
    std::string names;
    for (auto i = before; i < sections.size(); ++i)
        names += (names.empty() ? "" : ", ") + sections[i]->name();
    if (names.empty())
        return {};
    return (sections.size() - before == 1 ? "; added section " : "; added sections ") + names;
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
        // Each track's own events and its ramps, a ramp inside the span going with it and those
        // after it closing up as the events do; callers refuse a span through a ramp first.
        auto& taken = removed.tracks.emplace_back();
        auto& takenRamps = removed.ramps.emplace_back();
        song.updateTrackContent(
            tracks[i]->id(),
            [&](core::EventList& events, std::vector<core::ControllerRamp>& ramps)
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
                std::vector<core::ControllerRamp> left;
                for (auto ramp : ramps)
                {
                    if (ramp.start >= from && ramp.start < to)
                    {
                        ramp.start -= from;
                        takenRamps.push_back(ramp);
                        continue;
                    }
                    if (ramp.start >= to && ramp.start < end)
                    {
                        ramp.start -= to - from;
                    }
                    left.push_back(ramp);
                }
                ramps = std::move(left);
            });
    }
    takeBars(removed, song, first, from);
    return removed;
}

EventRegister takeEvents(const core::Song& song, std::size_t first, std::size_t last,
                         core::Tick from, core::Tick to)
{
    EventRegister taken;
    const auto tracks = song.tracks();
    for (auto i = first; i <= last && i < tracks.size(); ++i)
    {
        // A track's own events and the ramps starting in the span, so what's put plays as this
        // did (decided 17 November 2026); callers refuse a span through a ramp first.
        const auto id = tracks[i]->id();
        auto& events = taken.tracks.emplace_back();
        for (auto event : song.sourceEvents(id).startingIn(from, to))
        {
            event.tick -= from;
            events.push_back(std::move(event));
        }
        auto& ramps = taken.ramps.emplace_back();
        for (auto ramp : song.ramps(id))
        {
            if (ramp.start >= from && ramp.start < to)
            {
                ramp.start -= from;
                ramps.push_back(ramp);
            }
        }
    }
    taken.span = to - from;
    takeBars(taken, song, first, from);
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
    for (std::size_t i = 0;
         i < std::max(reg.tracks.size(), reg.ramps.size()) && firstTrack + i < tracks.size(); ++i)
    {
        for (const auto& event : i < reg.tracks.size() ? reg.tracks[i] : std::vector<core::Event>{})
        {
            latest = std::max(latest, at + event.tick);
        }
        for (const auto& event : song.events(tracks[firstTrack + i]->id()).startingIn(at, end))
        {
            latest = std::max(latest, event.tick + span);
        }
        // A ramp's end too, which must stay in the section.
        if (i < reg.ramps.size())
        {
            for (const auto& ramp : reg.ramps[i])
            {
                latest = std::max(latest, at + ramp.end());
            }
        }
        for (const auto& ramp : song.ramps(tracks[firstTrack + i]->id()))
        {
            if (ramp.start >= at && ramp.start < end)
            {
                latest = std::max(latest, ramp.end() + span);
            }
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
    for (std::size_t i = 0;
         i < std::max(reg.tracks.size(), reg.ramps.size()) && firstTrack + i < tracks.size(); ++i)
    {
        // The events and ramps from there to the section's end, together; callers refuse an
        // insertion inside a ramp first.
        song.updateTrackContent(
            tracks[firstTrack + i]->id(),
            [&](core::EventList& events, std::vector<core::ControllerRamp>& ramps)
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
                for (auto& ramp : ramps)
                {
                    if (ramp.start >= at && ramp.start < end)
                    {
                        ramp.start += span;
                    }
                }
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
