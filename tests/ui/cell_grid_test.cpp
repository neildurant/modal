// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/cell_grid.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/themes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace modal::ui;

TEST_CASE("UTF-8 is decoded, with anything malformed replaced")
{
    CHECK(decodeUtf8("") == U"");
    CHECK(decodeUtf8("abc") == U"abc");
    CHECK(decodeUtf8("F\xE2\x99\xAF"
                     "3") == std::u32string{U'F', 0x266F, U'3'});     // F♯3
    CHECK(decodeUtf8("\xF0\x9F\x8E\xB9") == std::u32string{0x1F3B9}); // 🎹

    constexpr char32_t r = 0xFFFD;
    CHECK(decodeUtf8("a\x80"
                     "b") == std::u32string{U'a', r, U'b'});   // stray continuation
    CHECK(decodeUtf8("a\xE2\x99") == std::u32string{U'a', r}); // cut short at the end
    CHECK(decodeUtf8("\xE2\x99"
                     "b") == std::u32string{r, U'b'});          // cut short mid-text
    CHECK(decodeUtf8("\xC0\xAF") == std::u32string{r});         // overlong
    CHECK(decodeUtf8("\xED\xA0\x80") == std::u32string{r});     // a surrogate
    CHECK(decodeUtf8("\xF4\x90\x80\x80") == std::u32string{r}); // beyond U+10FFFF
    CHECK(decodeUtf8("\xFF") == std::u32string{r});
}

TEST_CASE("a theme's reverse swaps a style's colours")
{
    const auto theme = *builtInTheme("dark");
    const Style plain{Role::Text, Role::Selection, kPlain};
    const Style reversed{Role::Text, Role::Selection, kReverse | kBold};
    CHECK(theme.foreground(plain) == theme[Role::Text]);
    CHECK(theme.background(plain) == theme[Role::Selection]);
    CHECK(theme.foreground(reversed) == theme[Role::Selection]);
    CHECK(theme.background(reversed) == theme[Role::Text]);
}

TEST_CASE("text is written into a grid one character to a cell, clipped at its edges")
{
    CellGrid grid{5, 2};
    const Style accent{Role::Accent, Role::Background, kBold};
    CHECK(grid.write(1, 0,
                     "F\xE2\x99\xAF"
                     "3",
                     accent) == 4);
    CHECK(grid.at(1, 0) == Cell{U'F', accent});
    CHECK(grid.at(2, 0) == Cell{0x266F, accent});
    CHECK(grid.at(3, 0) == Cell{U'3', accent});
    CHECK(grid.at(0, 0) == Cell{});

    CHECK(grid.write(3, 1, "abcdef", accent) == 9); // only "ab" fits
    CHECK(grid.at(4, 1).character == U'b');
    CHECK(grid.write(-2, 1, "xyz", accent) == 1); // only "z" lands, in column 0
    CHECK(grid.at(0, 1).character == U'z');
    CHECK(grid.at(9, 9) == Cell{}); // outside: a blank cell

    grid.fill(1, 0, 10, 10, {U'.', {}});
    CHECK(grid.at(0, 0) == Cell{});
    CHECK(grid.at(4, 1).character == U'.');
}

TEST_CASE("a grid becomes a region with a text run for each stretch of one style")
{
    CellGrid grid{6, 2};
    const Style accent{Role::Accent, Role::Background, kPlain};
    grid.write(2, 0, "ab", accent);
    const CellSize cell{10, 20};
    const auto region = gridRegion(grid, {5, 7, 60, 40}, cell, Role::Background);
    CHECK(region.bounds == Rect{5, 7, 60, 40});
    REQUIRE(region.text.size() == 4);
    CHECK(region.text[0] == TextRun{0, 0, U"  ", Style{}});
    CHECK(region.text[1] == TextRun{20, 0, U"ab", accent});
    CHECK(region.text[2] == TextRun{40, 0, U"  ", Style{}});
    CHECK(region.text[3] == TextRun{0, 20, U"      ", Style{}});
}
