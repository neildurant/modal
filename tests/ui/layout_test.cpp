#include <modal/ui/layout.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace modal::ui;

TEST_CASE("bars are anchored to their edges, and the main area takes the rest")
{
    const CellSize cell{10, 20};
    const auto layout = layoutWindow(805, 433, cell, 1, 2);
    REQUIRE(layout.top.size() == 1);
    REQUIRE(layout.bottom.size() == 2);
    CHECK(layout.top[0] == Rect{0, 0, 805, 20});
    CHECK(layout.bottom[0] == Rect{0, 393, 805, 20});
    CHECK(layout.bottom[1] == Rect{0, 413, 805, 20}); // touching the bottom edge
    CHECK(layout.main == Rect{0, 20, 805, 373});

    // 805 / 10 = 80.5 columns and 373 / 20 = 18.65 rows: the last of each partly visible.
    CHECK(layout.columns == 81);
    CHECK(layout.fullColumns == 80);
    CHECK(layout.rows == 19);
    CHECK(layout.fullRows == 18);
}

TEST_CASE("as the window grows a pixel at a time, the bottom bar follows its edge")
{
    const CellSize cell{10, 20};
    for (int height = 400; height <= 440; ++height)
    {
        const auto layout = layoutWindow(800, height, cell, 1, 1);
        CAPTURE(height);
        CHECK(layout.bottom[0].bottom() == height);
        CHECK(layout.main.y == 20);
        CHECK(layout.main.bottom() == layout.bottom[0].y);
        CHECK(layout.fullRows == layout.main.height / 20);
        CHECK(layout.rows == (layout.main.height + 19) / 20);
    }
}

TEST_CASE("a window too small for its bars leaves the main area empty")
{
    const auto layout = layoutWindow(100, 30, {10, 20}, 1, 1);
    CHECK(layout.main.height == 0);
    CHECK(layout.rows == 0);
    CHECK(layout.fullRows == 0);
    CHECK(layoutWindow(-5, -5, {10, 20}, 0, 0).main == Rect{0, 0, 0, 0});
}
