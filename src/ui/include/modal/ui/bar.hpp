// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/frame.hpp>
#include <modal/ui/style.hpp>

#include <string>
#include <vector>

namespace modal::ui
{

// A piece of a bar's text, in its own style.
struct Segment
{
    std::string text; // UTF-8
    Style style{Role::BarText, Role::Bar};
};

// A line of text such as the status line, which isn't moved around as a grid: segments in
// groups anchored to the left edge, the centre and the right edge.
struct Bar
{
    std::vector<Segment> left;
    std::vector<Segment> centre;
    std::vector<Segment> right;
    Role background = Role::Bar;
};

// The space around a bar's groups, in pixels.
struct BarSpacing
{
    int margin = 0; // between a group and its edge
    int gap = 0;    // the least between two groups

    // The defaults for a cell size: half a cell's width at the edges, two cells between.
    static BarSpacing forCell(CellSize cell) { return {cell.width / 2, cell.width * 2}; }
};

// The fewest characters of a segment worth showing before an ellipsis; one that can't show
// this many is dropped instead.
inline constexpr int kMinSegmentCharacters = 3;

// Lays a bar out in a region of the given width, one cell high, at (0, 0). The right group is
// kept whole and the left group gives way: its last segment that doesn't fit is cut short with
// an ellipsis, or dropped with those after it. The centre group is shown only if it fits
// between the others. If the right group alone doesn't fit, it is cut from its left end.
Region layoutBar(const Bar& bar, int width, CellSize cell, BarSpacing spacing);

} // namespace modal::ui
