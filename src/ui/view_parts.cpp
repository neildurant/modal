// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};

} // namespace

int inspectorHeight(CellSize cell)
{
    return 2 * cell.height + (cell.height / 4) * 2;
}

Region inspectorRegion(const Inspection& inspection, Rect bounds, int width, CellSize cell,
                       int positionWidth)
{
    // On a panel of its own: the kind of row, then headings above their values. Each column
    // is as wide as its longest value can be, so the columns stay put as the cursor moves;
    // a value that doesn't fit is cut short, and those after it left out.
    Region region{.bounds = bounds, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const int margin = BarSpacing::forCell(cell).margin;
    const int columns = std::max((width - 2 * margin) / cell.width, 0);
    const Style kind{Role::Accent, Role::Panel, kBold};
    const Style heading{Role::Dim, Role::Panel, kPlain};
    const Style value{Role::Text, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell.width; };
    const auto widthOf = [&](std::string_view name)
    {
        constexpr std::array<std::pair<std::string_view, int>, 16> kWidths{{
            {"Time", 9},
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
            {"Bars", 9},
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
        auto title = decodeUtf8(field.heading);
        auto text = decodeUtf8(field.value);
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
        cut(text);
        region.text.push_back({x(at), top, title, heading});
        region.text.push_back({x(at), top + cell.height, text, value});
        at += std::max({static_cast<int>(title.size()), static_cast<int>(text.size()),
                        widthOf(field.heading)}) +
              3;
    }
    return region;
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

} // namespace modal::ui
