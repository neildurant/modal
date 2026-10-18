#include <modal/ui/layout.hpp>

#include <algorithm>

namespace modal::ui
{

WindowLayout layoutWindow(int width, int height, CellSize cell, int topBars, int bottomBars)
{
    WindowLayout layout;
    width = std::max(width, 0);
    height = std::max(height, 0);
    const int line = std::max(cell.height, 1);

    for (int i = 0; i < topBars; ++i)
    {
        layout.top.push_back({0, i * line, width, line});
    }
    for (int i = bottomBars; i > 0; --i)
    {
        layout.bottom.push_back({0, height - i * line, width, line});
    }

    const int mainTop = topBars * line;
    const int mainHeight = std::max(height - (topBars + bottomBars) * line, 0);
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
