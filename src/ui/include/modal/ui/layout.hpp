#pragma once

#include <modal/ui/frame.hpp>

#include <vector>

namespace modal::ui
{

// Where everything goes in a window: bars along the top and bottom edges, one line high and
// anchored to their edges in pixels, so they follow a resize a pixel at a time, and the main
// area between them.
struct WindowLayout
{
    std::vector<Rect> top; // from the top edge down
    Rect main;
    std::vector<Rect> bottom; // from the top down, the last on the bottom edge

    // The main area in cells: all those it shows, the last row and column perhaps only partly,
    // and the fully visible ones, which the cursor and scrolling keep to.
    int columns = 0;
    int rows = 0;
    int fullColumns = 0;
    int fullRows = 0;
};

WindowLayout layoutWindow(int width, int height, CellSize cell, int topBars, int bottomBars);

} // namespace modal::ui
