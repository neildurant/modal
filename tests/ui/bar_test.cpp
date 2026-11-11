// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/view_parts.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};
constexpr BarSpacing kSpacing{5, 20};

Segment segment(std::string text)
{
    return {std::move(text), Style{Role::BarText, Role::Bar, kPlain}};
}

// The text runs' positions and text, to compare.
struct Placed
{
    int x;
    std::u32string text;

    friend bool operator==(const Placed&, const Placed&) = default;
};

std::vector<Placed> placed(const Region& region)
{
    std::vector<Placed> result;
    for (const auto& run : region.text)
    {
        result.push_back({run.x, run.text});
    }
    return result;
}

} // namespace

TEST_CASE("bar groups are anchored to the left, the centre and the right")
{
    const Bar bar{
        {segment("NORMAL"), segment(" song.mid")}, {segment("mid")}, {segment("17:2")}, Role::Bar};
    const auto region = layoutBar(bar, 405, kCell, kSpacing);
    CHECK(region.bounds == Rect{0, 0, 405, 20});
    CHECK(region.background == Role::Bar);
    CHECK(placed(region) == std::vector<Placed>{
                                {5, U"NORMAL"},
                                {65, U" song.mid"},
                                {187, U"mid"},  // (405 - 30) / 2
                                {360, U"17:2"}, // 405 - 5 - 40
                            });
}

TEST_CASE("the right group follows the right edge a pixel at a time")
{
    const Bar bar{{segment("left")}, {}, {segment("right")}, Role::Bar};
    for (int width = 300; width < 320; ++width)
    {
        const auto region = layoutBar(bar, width, kCell, kSpacing);
        REQUIRE(region.text.size() == 2);
        CHECK(region.text[0].x == 5);
        CHECK(region.text[1].x == width - 5 - 50);
    }
}

TEST_CASE("when space runs out, the left group gives way")
{
    const Bar bar{{segment("NORMAL "), segment("a-long-file-name.mid")},
                  {},
                  {segment("17:2:480")},
                  Role::Bar};

    // 260 px: the right group takes 80 px at 175, leaving the left group 175 - 20 - 5 = 150 px,
    // 15 characters: "NORMAL ", then 7 of the name and an ellipsis.
    constexpr char32_t ellipsis = 0x2026;
    auto region = layoutBar(bar, 260, kCell, kSpacing);
    CHECK(placed(region) == std::vector<Placed>{
                                {5, U"NORMAL "},
                                {75, std::u32string{U"a-long-"} + ellipsis},
                                {175, U"17:2:480"},
                            });

    // 200 px: room for 9 characters on the left, so only 1 of the name would show: too few, so
    // it's dropped.
    region = layoutBar(bar, 200, kCell, kSpacing);
    CHECK(placed(region) == std::vector<Placed>{{5, U"NORMAL "}, {115, U"17:2:480"}});

    // 80 px: the right group alone doesn't fit in its 7 characters, so it's cut from its left
    // end, and the left group has no room at all.
    region = layoutBar(bar, 80, kCell, kSpacing);
    CHECK(placed(region) == std::vector<Placed>{{5, ellipsis + std::u32string{U":2:480"}}});
}

TEST_CASE("the centre group shows only if it fits between the others")
{
    const Bar bar{{segment("left")}, {segment("centre")}, {segment("right")}, Role::Bar};
    CHECK(layoutBar(bar, 400, kCell, kSpacing).text.size() == 3);
    const auto narrow = layoutBar(bar, 200, kCell, kSpacing);
    CHECK(placed(narrow) == std::vector<Placed>{{5, U"left"}, {145, U"right"}});
}

TEST_CASE("the left group never runs into the right, however narrow the bar")
{
    const CellSize cell{10, 20};
    const Style label{Role::Dim, Role::Bar, kPlain};
    const Style value{Role::BarText, Role::Bar, kPlain};
    Bar bar;
    bar.left = {{"Track 1:", label}, {" Bass     ", value}, {"Channel:", label},
                {" 10     ", value}, {"Output:", label},    {" Pro-800     ", value}};
    bar.right = {
        {"Section:", label}, {" Chorus 2     ", value}, {"Bars:", label}, {" 36–47", value}};
    for (int columns = 40; columns <= 90; ++columns)
    {
        const auto region = layoutBar(bar, columns * cell.width, cell, BarSpacing::forCell(cell));
        int leftEnd = 0;
        int rightStart = columns * cell.width;
        for (const auto& run : region.text)
        {
            const int end = run.x + static_cast<int>(run.text.size()) * cell.width;
            const bool isRight = run.text.find(U"Section") != std::u32string::npos ||
                                 run.text.find(U"Chorus") != std::u32string::npos ||
                                 run.text.find(U"Bars") != std::u32string::npos ||
                                 run.text.find(U"36") != std::u32string::npos;
            if (isRight)
            {
                rightStart = std::min(rightStart, run.x);
            }
            else
            {
                leftEnd = std::max(leftEnd, end);
            }
        }
        CAPTURE(columns);
        CHECK(leftEnd <= rightStart);
    }
}

TEST_CASE("a view's bar leaves out what may go, the first to go first, until it fits")
{
    const std::vector<BarPair> left{
        {"Track:", "Bass"}, {"Bank:", "1/2", kDropBank}, {"Quantise:", "1/16 100%", kDropQuantise}};
    const std::vector<BarPair> right{{"Section:", "Verse", kDropSection},
                                     {"Bars:", "9-16", kDropBars},
                                     {"Position:", "3:2:000"}};
    const auto text = [&](int columns)
    {
        // The width the text needs, with BarSpacing::forCell's margins and gap.
        const auto region = labelledBar(left, right, {0, 0, columns * kCell.width, kCell.height},
                                        columns * kCell.width + 3 * kCell.width, kCell);
        std::u32string all;
        for (const auto& run : region.text)
            all += run.text;
        return all;
    };
    // Everything: 11 + 3 + 9 + 3 + 19 on the left, 14 + 3 + 10 + 3 + 17 on the right.
    CHECK(text(92) == U"Track: Bass   Bank: 1/2   Quantise: 1/16 100%Section: Verse   Bars: 9-16   "
                      U"Position: 3:2:000");
    // Quantise first, then the bank, then the bars, then the section; never the rest.
    CHECK(text(91).find(U"Quantise") == std::u32string::npos);
    CHECK(text(91).find(U"Bank") != std::u32string::npos);
    CHECK(text(69).find(U"Bank") == std::u32string::npos);
    CHECK(text(69).find(U"Bars") != std::u32string::npos);
    CHECK(text(57).find(U"Bars") == std::u32string::npos);
    CHECK(text(57).find(U"Section") != std::u32string::npos);
    const auto least = text(28);
    CHECK(least.find(U"Section") == std::u32string::npos);
    CHECK(least.find(U"Track: Bass") != std::u32string::npos);
    CHECK(least.find(U"Position: 3:2:000") != std::u32string::npos);
}
