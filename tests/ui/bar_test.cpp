#include <modal/ui/bar.hpp>

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
