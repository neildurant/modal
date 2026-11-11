// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/ports_view.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
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
    case PortsView::Column::Alias:
        return "Alias";
    case PortsView::Column::Name:
        return "Port";
    default:
        return {};
    }
}

std::string_view about(PortsView::Column column, PortDirection direction)
{
    const bool input = direction == PortDirection::Input;
    switch (column)
    {
    case PortsView::Column::Enabled:
        return input ? "Enabled: listened to, for Thru, recording and the In light"
                     : "Enabled: offered to tracks to play to";
    case PortsView::Column::Default:
        return input ? "Default: only an output can be the default"
                     : "Default: the output for tracks without one of their own";
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
        result.push_back({direction, std::nullopt});
        bool any = false;
        if (table != nullptr)
        {
            for (std::size_t i = 0; i < table->ports().size(); ++i)
            {
                if (table->ports()[i].direction == direction)
                {
                    result.push_back({direction, i});
                    any = true;
                }
            }
        }
        if (!any)
        {
            result.push_back({direction, std::nullopt}); // "None found"
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
    const auto rows = static_cast<std::size_t>(std::max(layout_.fullRows - 1, 1));
    const auto at = line();
    // A group's heading stays in view above its first port.
    const auto want = at > 0 && !lines()[at - 1].port ? at - 1 : at;
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
    case Column::Default:
        return 4;
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
    // Unique among its direction's, as a track names a port by it.
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
        if (port->direction == PortDirection::Input)
        {
            host_.refuse("Only an output can be the default");
            return;
        }
        changed.setDefaultOutput(table->defaultOutput() == name ? std::string{} : name);
        change(changed);
        host_.setMessage(changed.defaultOutput().empty()
                             ? "No default output: tracks without a port aren't played"
                             : "Default output: " + shown,
                         MessageKind::Information);
        return;
    default:
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
        column_ = std::min(column_ + n, kColumns.size() - 1);
    }
    else if (command == "cursor-first")
    {
        column_ = 0;
    }
    else if (command == "cursor-last")
    {
        column_ = kColumns.size() - 1;
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
    // What the column under the cursor is, then the port's detected name whole, and whether it
    // can be used.
    const auto* port = current();
    if (port == nullptr)
    {
        return {.kind = "Ports", .fields = {{"About", "No MIDI ports found on this machine"}}};
    }
    Inspection result{.kind = port->direction == PortDirection::Input ? "Input" : "Output",
                      .fields = {}};
    result.fields.push_back({"About", std::string{about(column(), port->direction)}});
    // Why it can't be used, before its name, which may be long.
    if (!port->present)
    {
        InspectorField absent{"Not connected", "not usable until it's back"};
        absent.warning = port->enabled;
        result.fields.push_back(std::move(absent));
    }
    else if (!port->enabled)
    {
        result.fields.push_back({"Disabled", port->direction == PortDirection::Input
                                                 ? "not listened to"
                                                 : "not offered to tracks"});
    }
    result.fields.push_back({"Port", port->name});
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
    const auto edge = hairline(cell_);
    const Style dim{Role::Dim, Role::Background, kPlain};

    // The headings, over the text columns; the buttons' letters say what they are.
    int x = cell_.width;
    for (const auto column : kColumns)
    {
        if (const auto text = heading(column); !text.empty())
        {
            table.text.push_back({x, 0, decodeUtf8(text), dim});
        }
        x += columnWidth(column) * cell_.width;
    }

    const auto rows = static_cast<std::size_t>(std::max(layout_.rows, 1));
    for (auto i = top_; i < all.size() && i - top_ + 1 < rows; ++i)
    {
        const int y = static_cast<int>(i - top_ + 1) * cell_.height;
        const auto& entry = all[i];
        if (!entry.port)
        {
            const bool groupHeading = i == 0 || all[i - 1].direction != entry.direction;
            table.text.push_back(groupHeading ? TextRun{cell_.width,
                                                        y,
                                                        decodeUtf8(groupName(entry.direction)),
                                                        {Role::Accent, Role::Background, kBold}}
                                              : TextRun{cell_.width * 5, y, U"None found", dim});
            continue;
        }
        const auto& port = ports->ports()[*entry.port];
        x = cell_.width;
        for (std::size_t c = 0; c < kColumns.size(); ++c)
        {
            const auto column = kColumns[c];
            const bool here = i == at && c == column_;
            if (column == Column::Enabled || column == Column::Default)
            {
                const bool output = port.direction == PortDirection::Output;
                if (column == Column::Default && !output)
                {
                    if (here)
                    {
                        table.text.push_back(
                            {x, y, U"   ", {Role::CursorText, Role::Cursor, kPlain}});
                    }
                    x += columnWidth(column) * cell_.width;
                    continue;
                }
                const bool on =
                    column == Column::Enabled ? port.enabled : ports->defaultOutput() == port.name;
                const auto colour = column == Column::Enabled ? Role::TrackSolo : Role::TrackChase;
                const int w = 3 * cell_.width;
                table.under.push_back({{x, y + edge, w, std::max(1, cell_.height - 2 * edge)},
                                       on ? colour : Role::Panel});
                table.text.push_back(
                    {x,
                     y,
                     column == Column::Enabled ? U" E " : U" D ",
                     {on ? Role::ToggleText : Role::Dim, Role::Background, kBold}});
                if (here)
                {
                    table.over.push_back({{x, y, w, edge}, Role::Cursor});
                    table.over.push_back({{x, y + cell_.height - edge, w, edge}, Role::Cursor});
                    table.over.push_back({{x, y, edge, cell_.height}, Role::Cursor});
                    table.over.push_back({{x + w - edge, y, edge, cell_.height}, Role::Cursor});
                }
                x += columnWidth(column) * cell_.width;
                continue;
            }
            const int room = columnWidth(column) - 3;
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
            // says so in the warning colour, as a song may be counting on it.
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
                if (here)
                {
                    style = {Role::CursorText, Role::Cursor, kPlain};
                }
                table.text.push_back({x, y, std::move(text), style});
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
    if (ports != nullptr && !ports->defaultOutput().empty())
    {
        const auto* port = ports->find(PortDirection::Output, ports->defaultOutput());
        right.push_back({"Default output:", port != nullptr ? port->shown() : std::string{}});
    }
    frame.push_back(labelledBar(left, right, layout_.top[0], width_, cell_));
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_, 9, nullptr));
}

} // namespace modal::ui
