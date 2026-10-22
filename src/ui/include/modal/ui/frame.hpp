// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/style.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The size of a character cell, in pixels.
struct CellSize
{
    int width = 0;
    int height = 0;

    friend bool operator==(CellSize, CellSize) = default;
};

// The width of a thin line, and of the space beside one, in pixels: a seventh of a cell's
// width, at least a pixel, so drawing keeps its proportions at any font size.
inline int hairline(CellSize cell)
{
    return std::max(cell.width / 7, 1);
}

// A rectangle in pixels.
struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }

    friend bool operator==(const Rect&, const Rect&) = default;
};

// A run of text, one character to a cell's width, with its top left corner at a pixel
// position. Its background covers the cells it takes up.
struct TextRun
{
    int x = 0;
    int y = 0;
    std::u32string text;
    Style style;

    friend bool operator==(const TextRun&, const TextRun&) = default;
};

// A filled rectangle in a role's colour, partly transparent if alpha is below 255. Lines are
// thin rectangles.
struct Shape
{
    Rect rect;
    Role role = Role::Text;
    std::uint8_t alpha = 255;

    friend bool operator==(const Shape&, const Shape&) = default;
};

// A part of the window, drawn clipped to its bounds, in layers: its background, the text
// runs' backgrounds, the shapes under the text, the text, then the shapes over it (such as a
// cursor). Positions inside it are relative to its top left corner.
struct Region
{
    Rect bounds;
    Role background = Role::Background;
    std::vector<TextRun> text;
    std::vector<Shape> under;
    std::vector<Shape> over;
};

// Everything to draw in one frame, in order.
using Frame = std::vector<Region>;

// The characters of UTF-8 text; a malformed sequence becomes U+FFFD.
std::u32string decodeUtf8(std::string_view text);

// Characters as UTF-8.
std::string encodeUtf8(std::u32string_view text);

} // namespace modal::ui
