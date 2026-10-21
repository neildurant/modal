#include <modal/ui/layout.hpp>

#include <algorithm>

namespace modal::ui
{

WindowLayout layoutWindow(int width, int height, CellSize cell, int topBars, int bottomBars)
{
    const std::vector<int> heights(static_cast<std::size_t>(std::max(topBars, 0)),
                                   std::max(cell.height, 1));
    return layoutWindow(width, height, cell, heights, bottomBars, 0);
}

WindowLayout layoutWindow(int width, int height, CellSize cell, std::span<const int> topHeights,
                          int bottomBars, int gap)
{
    WindowLayout layout;
    width = std::max(width, 0);
    height = std::max(height, 0);
    const int line = std::max(cell.height, 1);

    int mainTop = 0;
    for (const int barHeight : topHeights)
    {
        layout.top.push_back({0, mainTop, width, barHeight});
        mainTop += barHeight + gap;
    }
    for (int i = bottomBars; i > 0; --i)
    {
        layout.bottom.push_back({0, height - i * line, width, line});
    }

    const int mainHeight = std::max(height - mainTop - bottomBars * line, 0);
    layout.main = {0, mainTop, width, mainHeight};
    if (cell.width > 0 && cell.height > 0)
    {
        layout.columns = (width + cell.width - 1) / cell.width;
        layout.rows = (mainHeight + cell.height - 1) / cell.height;
        layout.fullColumns = width / cell.width;
        layout.fullRows = mainHeight / cell.height;
    }
    return layout;
}

} // namespace modal::ui
