#include <modal/ui/grid_cursor.hpp>

#include <algorithm>
#include <cstdint>

namespace modal::ui
{

namespace
{

// A position moved by an amount, stopping at 0 and the last, without overflowing.
int moved(int position, std::int64_t by, int count)
{
    return static_cast<int>(std::clamp<std::int64_t>(position + by, 0, std::max(count - 1, 0)));
}

} // namespace

GridCursor::GridCursor(int rows, int columns)
    : rows_(std::max(rows, 1))
    , columns_(std::max(columns, 1))
{
}

void GridCursor::setView(int rows, int columns)
{
    viewRows_ = std::max(rows, 1);
    viewColumns_ = std::max(columns, 1);
    // A larger view may now reach past the grid's end; scroll back to fill it.
    top_ = std::clamp(top_, 0, std::max(rows_ - viewRows_, 0));
    left_ = std::clamp(left_, 0, std::max(columns_ - viewColumns_, 0));
    follow();
}

void GridCursor::moveTo(int row, int column)
{
    row_ = moved(row, 0, rows_);
    column_ = moved(column, 0, columns_);
    follow();
}

void GridCursor::moveBy(int rows, int columns)
{
    row_ = moved(row_, rows, rows_);
    column_ = moved(column_, columns, columns_);
    follow();
}

void GridCursor::scrollRowsBy(int rows)
{
    top_ = moved(top_, rows, std::max(rows_ - viewRows_ + 1, 1));
    row_ = moved(row_, rows, rows_);
    follow();
}

void GridCursor::scrollColumnsBy(int columns)
{
    left_ = moved(left_, columns, std::max(columns_ - viewColumns_ + 1, 1));
    column_ = moved(column_, columns, columns_);
    follow();
}

void GridCursor::centre()
{
    top_ = std::clamp(row_ - viewRows_ / 2, 0, std::max(rows_ - viewRows_, 0));
    left_ = std::clamp(column_ - viewColumns_ / 2, 0, std::max(columns_ - viewColumns_, 0));
    follow();
}

void GridCursor::follow()
{
    top_ = std::clamp(top_, row_ - viewRows_ + 1, row_);
    left_ = std::clamp(left_, column_ - viewColumns_ + 1, column_);
}

} // namespace modal::ui
