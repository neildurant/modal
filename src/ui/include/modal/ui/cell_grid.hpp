#pragma once

#include <modal/ui/frame.hpp>
#include <modal/ui/style.hpp>

#include <string_view>
#include <vector>

namespace modal::ui
{

struct Cell
{
    char32_t character = U' ';
    Style style;

    friend bool operator==(const Cell&, const Cell&) = default;
};

// A rectangle of cells, for the views moved around cell by cell. Writes outside it are
// clipped.
class CellGrid
{
public:
    CellGrid() = default;
    CellGrid(int columns, int rows, Style style = {});

    int columns() const { return columns_; }
    int rows() const { return rows_; }

    // The cell at a column and row, or a blank cell outside the grid.
    Cell at(int column, int row) const;

    void set(int column, int row, Cell cell);

    // Writes UTF-8 text from a cell rightwards, one character to a cell, clipped at the
    // grid's edge. Returns the column after the last character written.
    int write(int column, int row, std::string_view text, Style style);

    // Fills a rectangle of cells, in columns and rows.
    void fill(int column, int row, int columns, int rows, Cell cell);

private:
    bool contains(int column, int row) const;

    int columns_ = 0;
    int rows_ = 0;
    std::vector<Cell> cells_;
};

// The grid as a region with its top left corner at the bounds' top left: one text run for
// each stretch of cells with the same style in a row. Cells the bounds cut off are left for
// clipping.
Region gridRegion(const CellGrid& grid, Rect bounds, CellSize cell, Role background);

} // namespace modal::ui
