#include <modal/ui/block_glyphs.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <initializer_list>
#include <utility>
#include <vector>

using namespace modal::ui;

namespace
{

// How many of the rects cover a pixel.
int coverage(const std::vector<CellRect>& rects, int x, int y)
{
    int count = 0;
    for (const auto& r : rects)
    {
        count += x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height ? 1 : 0;
    }
    return count;
}

// The pixels covered along one edge of the cell, as a pattern to compare between glyphs.
std::vector<bool> edge(const std::vector<CellRect>& rects, int width, int height, char side)
{
    std::vector<bool> covered;
    const bool across = side == 'u' || side == 'd'; // along the top or bottom edge
    const int length = across ? width : height;
    for (int i = 0; i < length; ++i)
    {
        const int x = across ? i : (side == 'l' ? 0 : width - 1);
        const int y = across ? (side == 'u' ? 0 : height - 1) : i;
        covered.push_back(coverage(rects, x, y) > 0);
    }
    return covered;
}

std::pair<int, int> cellSize()
{
    return GENERATE(std::pair{7, 15}, std::pair{10, 21}, std::pair{13, 28}, std::pair{24, 50});
}

} // namespace

TEST_CASE("only box drawing lines and block elements are drawn as rectangles")
{
    CHECK(blockGlyph(U'a', 10, 21).empty());
    CHECK(blockGlyph(U' ', 10, 21).empty());
    CHECK(blockGlyph(U'\u2550', 10, 21).empty()); // ═ double
    CHECK(blockGlyph(U'\u256D', 10, 21).empty()); // ╭ rounded
    CHECK(blockGlyph(U'\u2504', 10, 21).empty()); // ┄ dashed
    CHECK(blockGlyph(U'\u2571', 10, 21).empty()); // ╱ diagonal
    CHECK(blockGlyph(U'\u2588', 0, 21).empty());  // no cell to draw in
    CHECK(blockGlyph(U'\u2588', 10, 21) == std::vector<CellRect>{{0, 0, 10, 21}});
}

TEST_CASE("every rectangle is inside its cell")
{
    const auto [width, height] = cellSize();
    int drawn = 0;
    for (char32_t c = U'\u2500'; c <= U'\u259F'; ++c)
    {
        const auto rects = blockGlyph(c, width, height);
        drawn += rects.empty() ? 0 : 1;
        for (const auto& r : rects)
        {
            CAPTURE(static_cast<int>(c), r.x, r.y, r.width, r.height);
            CHECK(r.width > 0);
            CHECK(r.height > 0);
            CHECK(r.x >= 0);
            CHECK(r.y >= 0);
            CHECK(r.x + r.width <= width);
            CHECK(r.y + r.height <= height);
        }
    }
    CHECK(drawn == 80 + 32); // the lines and every block element
}

TEST_CASE("lines meet their neighbours' lines at the cell edges")
{
    const auto [width, height] = cellSize();
    const auto at = [&](char32_t c, char side)
    { return edge(blockGlyph(c, width, height), width, height, side); };

    // Light and heavy lines leave each edge where the plain lines do, whatever they join.
    const auto lightAcross = at(U'\u2500', 'l'); // ─
    const auto heavyAcross = at(U'\u2501', 'l'); // ━
    const auto lightDown = at(U'\u2502', 'u');   // │
    const auto heavyDown = at(U'\u2503', 'u');   // ┃
    CHECK(at(U'\u2500', 'r') == lightAcross);
    CHECK(at(U'\u2502', 'd') == lightDown);
    for (const char32_t c : {U'\u253C', U'\u2524', U'\u2510', U'\u2518', U'\u2534', U'\u252C'})
    {
        CAPTURE(static_cast<int>(c)); // ┼ ┤ ┐ ┘ ┴ ┬ all have a light left arm
        CHECK(at(c, 'l') == lightAcross);
    }
    for (const char32_t c : {U'\u253C', U'\u251C', U'\u2524', U'\u2514', U'\u2518', U'\u2534'})
    {
        CAPTURE(static_cast<int>(c)); // ┼ ├ ┤ └ ┘ ┴ all have a light up arm
        CHECK(at(c, 'u') == lightDown);
    }
    CHECK(at(U'\u254B', 'r') == heavyAcross); // ╋
    CHECK(at(U'\u254B', 'd') == heavyDown);
    CHECK(at(U'\u257C', 'l') == lightAcross); // ╼ light left, heavy right
    CHECK(at(U'\u257C', 'r') == heavyAcross);
    CHECK(heavyAcross != lightAcross);

    // A corner's two arms join: the centre is covered, with nothing beyond the corner.
    const auto corner = blockGlyph(U'\u250C', width, height); // ┌
    CHECK(coverage(corner, width / 2, height / 2) > 0);
    CHECK(edge(corner, width, height, 'l') == std::vector<bool>(static_cast<std::size_t>(height)));
    CHECK(edge(corner, width, height, 'u') == std::vector<bool>(static_cast<std::size_t>(width)));
}

TEST_CASE("halves and quadrants tile the cell exactly")
{
    const auto [width, height] = cellSize();
    const auto tiles = [&](std::initializer_list<char32_t> characters)
    {
        std::vector<CellRect> all;
        for (const auto c : characters)
        {
            const auto rects = blockGlyph(c, width, height);
            all.insert(all.end(), rects.begin(), rects.end());
        }
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (coverage(all, x, y) != 1)
                {
                    return false;
                }
            }
        }
        return true;
    };
    CHECK(tiles({U'\u2580', U'\u2584'}));                       // ▀ ▄
    CHECK(tiles({U'\u258C', U'\u2590'}));                       // ▌ ▐
    CHECK(tiles({U'\u2598', U'\u259D', U'\u2596', U'\u2597'})); // ▘ ▝ ▖ ▗
    CHECK(tiles({U'\u259A', U'\u259E'}));                       // ▚ ▞
    CHECK(tiles({U'\u2599', U'\u259D'}));                       // ▙ ▝
    CHECK(tiles({U'\u259B', U'\u2597'}));                       // ▛ ▗
    CHECK(tiles({U'\u259C', U'\u2596'}));                       // ▜ ▖
    CHECK(tiles({U'\u259F', U'\u2598'}));                       // ▟ ▘
}

TEST_CASE("eighths grow an eighth at a time, to the whole cell")
{
    const auto [width, height] = cellSize();
    int previous = 0;
    for (char32_t c = U'\u2581'; c <= U'\u2588'; ++c) // ▁ to █, from the bottom
    {
        const auto rects = blockGlyph(c, width, height);
        REQUIRE(rects.size() == 1);
        CHECK(rects[0].y + rects[0].height == height);
        CHECK(rects[0].height >= previous);
        previous = rects[0].height;
    }
    CHECK(previous == height);

    previous = 0;
    for (char32_t c = U'\u258F'; c >= U'\u2589'; --c) // ▏ to ▉, from the left
    {
        const auto rects = blockGlyph(c, width, height);
        REQUIRE(rects.size() == 1);
        CHECK(rects[0].x == 0);
        CHECK(rects[0].width >= previous);
        previous = rects[0].width;
    }
    CHECK(previous < width);
    CHECK(blockGlyph(U'\u2594', width, height)[0].y == 0);        // ▔ upper eighth
    CHECK(blockGlyph(U'\u2595', width, height)[0].x > width / 2); // ▕ right eighth
}

TEST_CASE("shades cover the cell, partly transparent")
{
    CHECK(blockGlyph(U'\u2591', 10, 21) == std::vector<CellRect>{{0, 0, 10, 21, 64}});
    CHECK(blockGlyph(U'\u2592', 10, 21) == std::vector<CellRect>{{0, 0, 10, 21, 128}});
    CHECK(blockGlyph(U'\u2593', 10, 21) == std::vector<CellRect>{{0, 0, 10, 21, 192}});
}
