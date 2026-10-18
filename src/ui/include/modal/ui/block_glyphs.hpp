#pragma once

#include <cstdint>
#include <vector>

namespace modal::ui
{

// A filled rectangle in a cell, in pixels from the cell's top left corner. An alpha below 255
// blends it with what's beneath, for the shades.
struct CellRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::uint8_t alpha = 255;

    friend bool operator==(const CellRect&, const CellRect&) = default;
};

// Box drawing lines, light and heavy (U+2500 to U+257F, apart from the double, dashed, rounded
// and diagonal ones), and block elements (U+2580 to U+259F), as rectangles in a cell of the
// given size. Drawn this way rather than from the font, they reach the cell's edges exactly, so
// they join their neighbours without seams. Empty for any other character, which the font draws.
//
// Lines are about an eighth of the cell's width thick, heavy ones twice that, centred in the
// cell, so lines in neighbouring cells meet. Eighths are rounded to whole pixels, and halves
// split the cell so the two halves fill it.
std::vector<CellRect> blockGlyph(char32_t character, int width, int height);

} // namespace modal::ui
