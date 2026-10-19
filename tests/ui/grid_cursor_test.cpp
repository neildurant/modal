#include <modal/ui/grid_cursor.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace modal::ui;

namespace
{

// Whether the cursor is in the rows and columns fully in view.
bool inView(const GridCursor& cursor)
{
    return cursor.row() >= cursor.top() && cursor.row() < cursor.top() + cursor.viewRows() &&
           cursor.column() >= cursor.left() &&
           cursor.column() < cursor.left() + cursor.viewColumns();
}

} // namespace

TEST_CASE("the cursor stops at the grid's edges")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveBy(-1, -1);
    CHECK(cursor.row() == 0);
    CHECK(cursor.column() == 0);

    cursor.moveBy(1000, 1000);
    CHECK(cursor.row() == 99);
    CHECK(cursor.column() == 49);

    cursor.moveTo(-5, 20);
    CHECK(cursor.row() == 0);
    CHECK(cursor.column() == 20);

    cursor.moveBy(2'000'000'000, 2'000'000'000); // no overflow
    CHECK(cursor.row() == 99);
}

TEST_CASE("the view scrolls only as far as it must to keep the cursor in it")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveBy(9, 4); // the last row and column in view
    CHECK(cursor.top() == 0);
    CHECK(cursor.left() == 0);

    cursor.moveBy(1, 1);
    CHECK(cursor.top() == 1);
    CHECK(cursor.left() == 1);

    cursor.moveBy(30, 0);
    CHECK(cursor.top() == 31); // the cursor on the view's last row
    cursor.moveBy(-5, 0);
    CHECK(cursor.top() == 31); // still in view, so no scrolling

    cursor.moveTo(0, 0);
    CHECK(cursor.top() == 0);
    CHECK(cursor.left() == 0);
    CHECK(inView(cursor));
}

TEST_CASE("when the view shrinks, it scrolls to keep the cursor in it")
{
    GridCursor cursor{100, 50};
    cursor.setView(20, 10);
    cursor.moveTo(15, 8);
    CHECK(cursor.top() == 0);

    cursor.setView(10, 5);
    CHECK(cursor.top() == 6);
    CHECK(cursor.left() == 4);
    CHECK(inView(cursor));

    cursor.setView(0, -3); // at least one row and column
    CHECK(cursor.viewRows() == 1);
    CHECK(cursor.viewColumns() == 1);
    CHECK(cursor.top() == 15);
    CHECK(cursor.left() == 8);
}

TEST_CASE("when the view grows past the grid's end, it scrolls back to fill it")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveTo(99, 49);
    CHECK(cursor.top() == 90);

    cursor.setView(40, 20);
    CHECK(cursor.top() == 60);
    CHECK(cursor.left() == 30);

    cursor.setView(200, 100); // larger than the grid
    CHECK(cursor.top() == 0);
    CHECK(cursor.left() == 0);
    CHECK(inView(cursor));
}

TEST_CASE("scrolling moves the view and the cursor together, stopping at the edges")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveTo(3, 0);

    cursor.scrollRowsBy(5);
    CHECK(cursor.top() == 5);
    CHECK(cursor.row() == 8);

    cursor.scrollRowsBy(100); // the view stops at the end, the cursor goes on to the last row
    CHECK(cursor.top() == 90);
    CHECK(cursor.row() == 99);

    cursor.scrollRowsBy(-5);
    CHECK(cursor.top() == 85);
    CHECK(cursor.row() == 94);

    cursor.scrollRowsBy(-100);
    CHECK(cursor.top() == 0);
    CHECK(cursor.row() == 0);
}

TEST_CASE("a grid smaller than its view doesn't scroll")
{
    GridCursor cursor{5, 3};
    cursor.setView(10, 10);
    cursor.scrollRowsBy(3);
    CHECK(cursor.top() == 0);
    CHECK(cursor.row() == 3);
    cursor.moveBy(10, 10);
    CHECK(cursor.top() == 0);
    CHECK(cursor.left() == 0);
    CHECK(cursor.row() == 4);
    CHECK(cursor.column() == 2);
}

TEST_CASE("centring scrolls the cursor to the middle of the view, as far as the grid allows")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveTo(40, 20);
    CHECK(cursor.top() == 31);
    cursor.centre();
    CHECK(cursor.top() == 35);
    CHECK(cursor.left() == 18);

    cursor.moveTo(2, 1); // near the start, it can't go further up or left
    cursor.centre();
    CHECK(cursor.top() == 0);
    CHECK(cursor.left() == 0);
    cursor.moveTo(98, 49); // nor past the end
    cursor.centre();
    CHECK(cursor.top() == 90);
    CHECK(cursor.left() == 45);
}

TEST_CASE("scrolling by columns moves the view and the cursor together, stopping at the edges")
{
    GridCursor cursor{100, 50};
    cursor.setView(10, 5);
    cursor.moveTo(0, 2);
    cursor.scrollColumnsBy(3);
    CHECK(cursor.left() == 3);
    CHECK(cursor.column() == 5);
    cursor.scrollColumnsBy(-10); // the view stops at the start, the cursor goes on to it
    CHECK(cursor.left() == 0);
    CHECK(cursor.column() == 0);
    cursor.scrollColumnsBy(100);
    CHECK(cursor.left() == 45);
    CHECK(cursor.column() == 49);
}
