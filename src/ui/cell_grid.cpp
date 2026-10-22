// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/cell_grid.hpp>

#include <algorithm>
#include <cstddef>

namespace modal::ui
{

CellGrid::CellGrid(int columns, int rows, Style style)
    : columns_(std::max(columns, 0))
    , rows_(std::max(rows, 0))
    , cells_(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_),
             Cell{U' ', style})
{
}

bool CellGrid::contains(int column, int row) const
{
    return column >= 0 && row >= 0 && column < columns_ && row < rows_;
}

Cell CellGrid::at(int column, int row) const
{
    if (!contains(column, row))
    {
        return {};
    }
    return cells_[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) +
                  static_cast<std::size_t>(column)];
}

void CellGrid::set(int column, int row, Cell cell)
{
    if (contains(column, row))
    {
        cells_[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) +
               static_cast<std::size_t>(column)] = cell;
    }
}

int CellGrid::write(int column, int row, std::string_view text, Style style)
{
    for (const char32_t character : decodeUtf8(text))
    {
        set(column++, row, {character, style});
    }
    return column;
}

void CellGrid::fill(int column, int row, int columns, int rows, Cell cell)
{
    for (int r = row; r < row + rows; ++r)
    {
        for (int c = column; c < column + columns; ++c)
        {
            set(c, r, cell);
        }
    }
}

Region gridRegion(const CellGrid& grid, Rect bounds, CellSize cell, Role background)
{
    Region region{.bounds = bounds, .background = background, .text = {}, .under = {}, .over = {}};
    for (int row = 0; row < grid.rows(); ++row)
    {
        int column = 0;
        while (column < grid.columns())
        {
            const Style style = grid.at(column, row).style;
            TextRun run{column * cell.width, row * cell.height, {}, style};
            while (column < grid.columns() && grid.at(column, row).style == style)
            {
                run.text.push_back(grid.at(column, row).character);
                ++column;
            }
            region.text.push_back(std::move(run));
        }
    }
    return region;
}

} // namespace modal::ui
