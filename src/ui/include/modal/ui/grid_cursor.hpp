#pragma once

namespace modal::ui
{

// A cursor on a grid of rows and columns, counting from 0, and the part of the grid in view,
// which follows it. The view counts only the rows and columns fully shown, so the cursor is
// never cut off by the window's edge; it scrolls only as far as needed to keep the cursor in
// view.
class GridCursor
{
public:
    GridCursor(int rows, int columns);

    int rows() const { return rows_; }
    int columns() const { return columns_; }

    int row() const { return row_; }
    int column() const { return column_; }

    // The first row and column in view, and how many are fully in view.
    int top() const { return top_; }
    int left() const { return left_; }
    int viewRows() const { return viewRows_; }
    int viewColumns() const { return viewColumns_; }

    // Sets how many rows and columns are fully in view (at least one of each), as when the
    // window is resized, scrolling if the cursor would be out of it.
    void setView(int rows, int columns);

    // Moves the cursor, stopping at the grid's edges.
    void moveTo(int row, int column);
    void moveBy(int rows, int columns);

    // Scrolls the view by some rows (up if negative) and moves the cursor as far, as Vim's
    // Ctrl-D does. At the grid's edge the view stops and the cursor goes on to the edge.
    void scrollBy(int rows);

private:
    void follow();

    int rows_;
    int columns_;
    int row_ = 0;
    int column_ = 0;
    int top_ = 0;
    int left_ = 0;
    int viewRows_ = 1;
    int viewColumns_ = 1;
};

} // namespace modal::ui
