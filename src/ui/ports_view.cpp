// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/ports_view.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace modal::ui
{

namespace
{

std::string_view heading(PortsView::Column column)
{
    switch (column)
    {
    case PortsView::Column::Enabled:
        return "On";
    case PortsView::Column::Default:
        return "Default";
    case PortsView::Column::Alias:
        return "Alias";
    case PortsView::Column::Name:
        return "Port";
    case PortsView::Column::Status:
        return {};
    }
    return {};
}

// Where in its column a button's mark goes: under the middle of the heading.
int markOffset(PortsView::Column column)
{
    return column == PortsView::Column::Default ? 3 : 0;
}

std::string_view about(PortsView::Column column, PortDirection direction)
{
    const bool input = direction == PortDirection::Input;
    switch (column)
    {
    case PortsView::Column::Enabled:
        return input ? "On: listened to, for Thru, recording and the In light"
                     : "On: offered to tracks to play to";
    case PortsView::Column::Default:
        return input ? "Default: the input of tracks set to Default, one at a time"
                     : "Default: the output of tracks set to Default, one at a time";
    case PortsView::Column::Alias:
        return "Alias: what tracks and songs call it, if not its detected name";
    case PortsView::Column::Name:
        return "Port: the name the system gives it";
    case PortsView::Column::Status:
        return "Whether it's there now";
    }
    return {};
}

std::string groupName(PortDirection direction)
{
    return direction == PortDirection::Input ? "Inputs" : "Outputs";
}

} // namespace

PortsView::PortsView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
{
}

std::vector<int> PortsView::topHeights(CellSize cell) const
{
    return {cell.height, inspectorHeight(cell)};
}

void PortsView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = cell;
    ensureVisible();
}

std::vector<PortsView::Line> PortsView::lines() const
{
    std::vector<Line> result;
    const auto* table = host_.portTable();
    for (const auto direction : {PortDirection::Input, PortDirection::Output})
    {
        if (direction == PortDirection::Output)
        {
            result.push_back({PortDirection::Input, LineKind::Blank});
            result.push_back({PortDirection::Input, LineKind::Blank});
        }
        result.push_back({direction, LineKind::Heading});
        result.push_back({direction, LineKind::Blank});
        result.push_back({direction, LineKind::Columns});
        bool any = false;
        if (table != nullptr)
        {
            for (std::size_t i = 0; i < table->ports().size(); ++i)
            {
                if (table->ports()[i].direction == direction)
                {
                    result.push_back({direction, LineKind::Port, i});
                    any = true;
                }
            }
        }
        if (!any)
        {
            result.push_back({direction, LineKind::None});
        }
    }
    return result;
}

std::size_t PortsView::line() const
{
    const auto all = lines();
    const auto* table = host_.portTable();
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (!all[i].port)
        {
            continue;
        }
        const auto& port = table->ports()[*all[i].port];
        if (port.direction == direction_ && port.name == port_)
        {
            return i;
        }
        first = first ? first : std::optional{i};
    }
    return first.value_or(1);
}

const KnownPort* PortsView::current() const
{
    const auto all = lines();
    const auto at = line();
    const auto* table = host_.portTable();
    return table != nullptr && at < all.size() && all[at].port ? &table->ports()[*all[at].port]
                                                               : nullptr;
}

void PortsView::go(std::size_t to, int towards)
{
    // To the port nearest the line asked for, on in the way it went, or else back.
    const auto all = lines();
    const auto* table = host_.portTable();
    to = std::min(to, all.size() - 1);
    const auto take = [&](std::size_t i)
    {
        const auto& port = table->ports()[*all[i].port];
        direction_ = port.direction;
        port_ = port.name;
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        const int step = pass == 0 ? towards : -towards;
        for (auto i = static_cast<std::ptrdiff_t>(to);
             i >= 0 && i < static_cast<std::ptrdiff_t>(all.size()); i += step == 0 ? 1 : step)
        {
            if (all[static_cast<std::size_t>(i)].port)
            {
                take(static_cast<std::size_t>(i));
                ensureVisible();
                return;
            }
        }
    }
    ensureVisible();
}

void PortsView::ensureVisible()
{
    const auto rows = static_cast<std::size_t>(std::max(layout_.fullRows, 1));
    const auto at = line();
    // A group's headings stay in view above its first port.
    const auto all = lines();
    auto want = at;
    while (want > 0 && !all[want - 1].port && all[want - 1].direction == all[at].direction)
    {
        --want;
    }
    top_ = std::min(top_, want);
    if (at >= top_ + rows)
    {
        top_ = at - rows + 1;
    }
}

int PortsView::columnWidth(Column column) const
{
    const auto* table = host_.portTable();
    const auto longest = [&](auto of)
    {
        std::size_t most = 0;
        if (table != nullptr)
        {
            for (const auto& port : table->ports())
            {
                most = std::max(most, decodeUtf8(of(port)).size());
            }
        }
        return static_cast<int>(most);
    };
    switch (column)
    {
    case Column::Enabled:
        return 4;
    case Column::Default:
        return 9;
    case Column::Alias:
        return std::max(longest([](const KnownPort& p) { return p.alias; }), 16) + 3;
    case Column::Name:
        return std::max(longest([](const KnownPort& p) { return p.name; }), 16) + 3;
    case Column::Status:
        return 16; // "not connected" and the gap
    }
    return 4;
}

void PortsView::refresh()
{
    go(line(), 1);
}

void PortsView::showSong()
{
    songCursor_ = {};
    editor_.close();
}

const core::MeterMap& PortsView::cursorMeter() const
{
    return songCursor_.track ? document_.song().meter(*songCursor_.track)
                             : document_.song().meter();
}

std::string PortsView::position(core::Tick tick) const
{
    return positionText(cursorMeter(), tick);
}

core::EditPlace PortsView::place() const
{
    return {.track = songCursor_.track, .tick = songCursor_.tick};
}

void PortsView::restore(const core::EditPlace& place)
{
    songCursor_.track = place.track;
    songCursor_.tick = place.tick;
}

void PortsView::change(const PortTable& table)
{
    host_.changePorts(table);
    refresh();
}

std::optional<std::string> PortsView::setAlias(const std::string& typed)
{
    const auto* port = current();
    const auto* table = host_.portTable();
    if (port == nullptr || table == nullptr)
    {
        return "No port to name";
    }
    auto alias = typed;
    alias.erase(0, alias.find_first_not_of(" \t"));
    alias.erase(alias.find_last_not_of(" \t") + 1);
    // Not the choosers' own words, and unique among its direction's, as a track names a port
    // by it.
    for (const std::string_view taken : {"Default", "All", "*"})
    {
        if (alias.size() == taken.size() &&
            std::ranges::equal(alias, taken,
                               [](char a, char b)
                               {
                                   return std::tolower(static_cast<unsigned char>(a)) ==
                                          std::tolower(static_cast<unsigned char>(b));
                               }))
        {
            return "Tracks' choosers use " + std::string{taken} + " for themselves";
        }
    }
    for (const auto& other : table->ports())
    {
        if (!alias.empty() && other.direction == port->direction && &other != port &&
            (other.alias == alias || (other.alias.empty() && other.name == alias)))
        {
            return "Another " +
                   std::string{port->direction == PortDirection::Input ? "input" : "output"} +
                   " is called " + alias;
        }
    }
    auto changed = *table;
    if (changed.setAlias(port->direction, port->name, alias))
    {
        const auto name = port->name; // the change replaces the table it points into
        change(changed);
        host_.setMessage(alias.empty() ? name + " has no alias" : name + " is called " + alias,
                         MessageKind::Information);
    }
    return std::nullopt;
}

void PortsView::act()
{
    const auto* port = current();
    const auto* table = host_.portTable();
    if (port == nullptr || table == nullptr)
    {
        host_.refuse("No ports found");
        return;
    }
    auto changed = *table;
    const auto name = port->name;
    const auto shown = port->shown();
    switch (column())
    {
    case Column::Enabled:
    {
        const bool on = !port->enabled;
        changed.setEnabled(port->direction, name, on);
        change(changed);
        host_.setMessage((on ? "Enabled " : "Disabled ") + shown +
                             (on ? (port->direction == PortDirection::Input ? ": listened to"
                                                                            : ": offered to tracks")
                                 : ""),
                         MessageKind::Information);
        return;
    }
    case Column::Default:
    {
        const auto direction = port->direction;
        const bool input = direction == PortDirection::Input;
        changed.setDefault(direction, table->defaultPort(direction) == name ? std::string{} : name);
        change(changed);
        host_.setMessage(changed.defaultPort(direction).empty()
                             ? (input ? "No default input: tracks set to Default hear nothing"
                                      : "No default output: tracks set to Default aren't played")
                             : (input ? "Default input: " : "Default output: ") + shown,
                         MessageKind::Information);
        return;
    }
    case Column::Name: // the detected name is the port's own, not to change
    case Column::Status:
        return;
    case Column::Alias:
        if (editor_.open({.kind = "Port",
                          .fields = {{.heading = "Alias",
                                      .value = port->alias,
                                      .editable = true,
                                      .editText = port->alias}}},
                         "Alias"))
        {
            host_.startTyping();
        }
        return;
    }
}

std::optional<bool> PortsView::run(std::string_view command, int count)
{
    const auto n = static_cast<std::size_t>(std::max(count, 1));
    const auto all = lines();
    if (command == "cursor-down")
    {
        // Past the groups' headings, n ports on.
        auto at = line();
        for (std::size_t moved = 0; moved < n;)
        {
            auto next = at + 1;
            while (next < all.size() && !all[next].port)
                ++next;
            if (next >= all.size())
                break;
            at = next;
            ++moved;
        }
        go(at, 1);
    }
    else if (command == "cursor-up")
    {
        auto at = line();
        for (std::size_t moved = 0; moved < n;)
        {
            auto before = at;
            while (before > 0 && !all[--before].port)
            {
            }
            if (!all[before].port || before == at)
                break;
            at = before;
            ++moved;
        }
        go(at, -1);
    }
    else if (command == "cursor-left")
    {
        column_ -= std::min(column_, n);
    }
    else if (command == "cursor-right")
    {
        column_ = std::min(column_ + n, kLastColumn);
    }
    else if (command == "cursor-first")
    {
        column_ = 0;
    }
    else if (command == "cursor-last")
    {
        column_ = kLastColumn;
    }
    else if (command == "go-first")
    {
        go(0, 1);
    }
    else if (command == "go-last")
    {
        go(all.size() - 1, -1);
    }
    else if (command == "field-edit" || command == "inspector-edit")
    {
        act();
    }
    else if (command == "event-delete")
    {
        const auto* port = current();
        const auto* table = host_.portTable();
        if (port == nullptr || table == nullptr)
        {
            host_.refuse("No ports found");
        }
        else if (port->present)
        {
            host_.refuse(port->shown() + " is there now, so it would be found again; disable it "
                                         "instead");
        }
        else
        {
            // On to the next port, or the one before if it was the last.
            const auto at = line();
            std::optional<std::size_t> next;
            for (auto i = at + 1; i < all.size() && !next; ++i)
                if (all[i].port)
                    next = i;
            for (auto i = at; i > 0 && !next; --i)
                if (all[i - 1].port)
                    next = i - 1;
            const auto shown = port->shown();
            auto changed = *table;
            changed.forget(port->direction, port->name);
            if (next)
            {
                const auto& to = table->ports()[*all[*next].port];
                direction_ = to.direction;
                port_ = to.name;
            }
            change(changed);
            host_.setMessage("Forgot " + shown, MessageKind::Information);
        }
    }
    else if (command == "ports-refresh")
    {
        host_.lookForPorts();
        refresh();
        host_.setMessage("Looked for ports again", MessageKind::Information);
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

bool PortsView::operate(std::string_view, std::string_view, int)
{
    host_.refuse("There's nothing in the ports view to operate on");
    return false;
}

std::optional<std::string> PortsView::typingHint() const
{
    return editor_.active() ? std::optional<std::string>{"A short name for the port, or nothing "
                                                         "for its detected name; Enter sets it, "
                                                         "Escape leaves it"}
                            : std::nullopt;
}

void PortsView::type(char32_t character)
{
    editor_.type(character);
}

std::optional<bool> PortsView::runTyping(std::string_view command)
{
    return editor_.run(
        command, host_,
        {
            .inspection =
                [this]
            {
                const auto* port = current();
                const auto alias = port != nullptr ? port->alias : std::string{};
                return Inspection{
                    .kind = "Port",
                    .fields = {
                        {.heading = "Alias", .value = alias, .editable = true, .editText = alias}}};
            },
            .set = [this](const std::string&, const std::string& text) { return setAlias(text); },
            .step = [](const std::string&, int)
            { return std::optional<std::string>{"An alias can't be stepped"}; },
        });
}

Inspection PortsView::inspection() const
{
    // The port's detected name whole, then whether it can be used, then what the column under
    // the cursor is. The name and the state are as wide on every row, and what varies most
    // comes last, so nothing moves as the cursor does (decided 12 November 2026, after use).
    const auto* port = current();
    if (port == nullptr)
    {
        return {.kind = "Ports", .fields = {{"About", "No MIDI ports found on this machine"}}};
    }
    Inspection result{.kind = port->direction == PortDirection::Input ? "Input" : "Output",
                      .fields = {}};
    std::size_t longest = 0;
    if (const auto* table = host_.portTable())
        for (const auto& known : table->ports())
            longest = std::max(longest, decodeUtf8(known.name).size());
    result.fields.push_back({"Port", padText(port->name, longest, false)});
    // Not connected, in the warning colour if it's enabled, as it's wanted but can't be used.
    InspectorField state{"State", padText(!port->present   ? "Not connected"
                                          : !port->enabled ? "Disabled"
                                                           : "Enabled",
                                          13, false)};
    state.warning = !port->present && port->enabled;
    result.fields.push_back(std::move(state));
    result.fields.push_back({"About", std::string{about(column(), port->direction)}});
    return result;
}

void PortsView::addRegions(Frame& frame) const
{
    Region table{.bounds = layout_.main,
                 .background = Role::Background,
                 .text = {},
                 .under = {},
                 .over = {}};
    const auto* ports = host_.portTable();
    const auto all = lines();
    const auto at = line();
    const Style dim{Role::Dim, Role::Background, kPlain};
    const Style cursor{Role::CursorText, Role::Cursor, kPlain};

    const auto rows = static_cast<std::size_t>(std::max(layout_.rows, 1));
    for (auto i = top_; i < all.size() && i - top_ < rows; ++i)
    {
        const int y = static_cast<int>(i - top_) * cell_.height;
        const auto& entry = all[i];
        int x = cell_.width;
        switch (entry.kind)
        {
        case LineKind::Blank:
            continue;
        case LineKind::Heading:
            table.text.push_back({x,
                                  y,
                                  decodeUtf8(groupName(entry.direction)),
                                  {Role::Accent, Role::Background, kBold}});
            continue;
        case LineKind::Columns:
            for (const auto column : kColumns)
            {
                if (const auto text = heading(column); !text.empty())
                {
                    table.text.push_back({x, y, decodeUtf8(text), dim});
                }
                x += columnWidth(column) * cell_.width;
            }
            continue;
        case LineKind::None:
            x += (columnWidth(Column::Enabled) + columnWidth(Column::Default)) * cell_.width;
            table.text.push_back({x, y, U"None found", dim});
            continue;
        case LineKind::Port:
            break;
        }
        const auto& port = ports->ports()[*entry.port];
        for (std::size_t c = 0; c < kColumns.size(); ++c)
        {
            const auto column = kColumns[c];
            const bool here = i == at && c == column_;
            const int room = columnWidth(column) -
                             (column == Column::Enabled || column == Column::Default ? 2 : 3);
            if (column == Column::Enabled || column == Column::Default)
            {
                // A tick or cross for on, and a radio button for the default, one at a time;
                // the cursor fills the heading's width.
                const bool on = column == Column::Enabled
                                    ? port.enabled
                                    : ports->defaultPort(port.direction) == port.name;
                std::u32string mark(static_cast<std::size_t>(room), U' ');
                mark[static_cast<std::size_t>(markOffset(column))] =
                    column == Column::Enabled ? (on ? U'✓' : U'✗') : (on ? U'•' : U'◦');
                const auto colour = !on                         ? Role::Dim
                                    : column == Column::Enabled ? Role::TrackSolo
                                    : port.enabled              ? Role::Accent
                                                                : Role::Dim;
                table.text.push_back({x, y, std::move(mark),
                                      here ? cursor : Style{colour, Role::Background, kPlain}});
                x += columnWidth(column) * cell_.width;
                continue;
            }
            auto text = decodeUtf8(column == Column::Alias  ? port.alias
                                   : column == Column::Name ? port.name
                                   : port.present           ? std::string{}
                                                            : std::string{"not connected"});
            if (text.size() > static_cast<std::size_t>(room))
            {
                text.resize(static_cast<std::size_t>(std::max(room - 1, 0)));
                text += U'…';
            }
            text.resize(static_cast<std::size_t>(std::max(room, 0)), U' ');
            // Grey while disabled, as is a port that isn't there; but one enabled and not there
            // says so in the warning colour, as a song may be counting on it. The alias is typed,
            // so shows as a field.
            Style style{port.enabled && port.present ? Role::Text : Role::Dim, Role::Background,
                        kPlain};
            if (column == Column::Status && port.enabled && !port.present)
            {
                style.foreground = Role::Warning;
            }
            if (here && editor_.active() && column == Column::Alias)
            {
                inspectorValue(table, {"Alias", {}}, {}, x, y, room, cell_, &editor_);
            }
            else
            {
                if (column == Column::Alias && !here)
                {
                    addField(table, x, y, room, cell_);
                }
                table.text.push_back({x, y, std::move(text), here ? cursor : style});
            }
            x += columnWidth(column) * cell_.width;
        }
    }
    frame.push_back(std::move(table));
    if (layout_.top.size() < 2)
    {
        return;
    }
    // The bar: how many of each are enabled.
    std::vector<BarPair> left{{"MIDI ports", {}}};
    std::vector<BarPair> right;
    for (const auto direction : {PortDirection::Input, PortDirection::Output})
    {
        std::size_t enabled = 0;
        std::size_t known = 0;
        if (ports != nullptr)
        {
            for (const auto& port : ports->ports())
            {
                if (port.direction == direction)
                {
                    ++known;
                    enabled += port.enabled ? 1 : 0;
                }
            }
        }
        right.push_back({groupName(direction) + ":",
                         std::to_string(enabled) + " of " + std::to_string(known) + " enabled"});
    }
    frame.push_back(labelledBar(left, right, layout_.top[0], width_, cell_));
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_, 9, nullptr));
}

} // namespace modal::ui
