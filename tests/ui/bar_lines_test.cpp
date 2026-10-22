// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar_lines.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

// The x positions of the lines in a role.
std::vector<int> positions(const std::vector<Shape>& lines, Role role)
{
    std::vector<int> result;
    for (const auto& line : lines)
    {
        if (line.role == role)
        {
            result.push_back(line.rect.x);
        }
    }
    return result;
}

} // namespace

TEST_CASE("a time scale puts ticks at pixels, rounding down")
{
    const TimeScale scale{.start = 100, .length = 300, .x = 10, .width = 100};
    CHECK(scale.toX(100) == 10);
    CHECK(scale.toX(103) == 11);
    CHECK(scale.toX(102) == 10);
    CHECK(scale.toX(400) == 110);
    CHECK(scale.toX(99) == 9); // before the start
    CHECK(scale.toX(97) == 9);
    CHECK(scale.toX(96) == 8);
}

TEST_CASE("bars of 7/8 are 7/8 as wide as bars of 4/4 on the same time scale")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96}, core::Ppqn{480});
    CAPTURE(ppqn.value);
    // Sixteen quarter notes across 1600 pixels: 100 to a quarter note.
    const TimeScale scale{
        .start = 0, .length = core::NoteFraction{4, 1}.toTicks(ppqn), .x = 0, .width = 1600};
    const core::NoteFraction sixteenth{1, 16};

    const auto common =
        meterLines(core::MeterMap{ppqn, {4, 4}}, scale, 0, 20, sixteenth, CellSize{14, 32});
    CHECK(positions(common, Role::BarLine) == std::vector<int>{0, 400, 800, 1200});
    CHECK(positions(common, Role::BeatLine) ==
          std::vector<int>{100, 200, 300, 500, 600, 700, 900, 1000, 1100, 1300, 1400, 1500});
    CHECK(positions(common, Role::GridLine).size() == 16 * 3);

    const auto seven =
        meterLines(core::MeterMap{ppqn, {7, 8}}, scale, 0, 20, sixteenth, CellSize{14, 32});
    CHECK(positions(seven, Role::BarLine) == std::vector<int>{0, 350, 700, 1050, 1400});
    const auto beats = positions(seven, Role::BeatLine); // every eighth note
    REQUIRE(beats.size() == 32 - 5);
    CHECK(beats.front() == 50);
    CHECK(beats.back() == 1550);
    CHECK(positions(seven, Role::GridLine).size() == 32);

    for (const auto& line : seven)
    {
        CHECK(line.rect.y == 0);
        CHECK(line.rect.height == 20);
        CHECK(line.rect.width == (line.role == Role::BarLine ? 2 : 1));
    }
}

TEST_CASE("bar lines follow meter changes, and grid steps count from each bar line")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    CAPTURE(ppqn.value);
    core::MeterMap meters{ppqn, {4, 4}};
    meters.set(meters.barStart(2), {3, 8});
    meters.set(meters.barStart(3), {4, 4});
    const TimeScale scale{
        .start = 0, .length = core::NoteFraction{3, 1}.toTicks(ppqn), .x = 0, .width = 1200};

    // A triplet-eighth grid, which doesn't divide a 3/8 bar's beats.
    const auto lines =
        meterLines(meters, scale, 0, 10, core::NoteFraction{1, 12}, CellSize{10, 20});
    CHECK(positions(lines, Role::BarLine) == std::vector<int>{0, 400, 550, 950});
    const auto beats = positions(lines, Role::BeatLine);
    CHECK(beats == std::vector<int>{100, 200, 300, 450, 500, 650, 750, 850, 1050, 1150});
    const auto grid = positions(lines, Role::GridLine);
    // Eight in the first bar, then in the 3/8 bar, steps of 33.3 pixels from its bar line at
    // 400, between its beats at 450 and 500.
    REQUIRE(grid.size() >= 10);
    CHECK(std::vector<int>(grid.begin() + 8, grid.begin() + 10) == std::vector<int>{433, 466});
}

TEST_CASE("beat and grid lines too close together are left out, but bar lines never are")
{
    const auto ppqn = core::kPpqn;
    const core::MeterMap meters{ppqn, {4, 4}};
    // 64 bars across 640 pixels: 2.5 pixels to a beat.
    const TimeScale scale{
        .start = 0, .length = core::NoteFraction{64, 1}.toTicks(ppqn), .x = 0, .width = 640};
    const auto lines =
        meterLines(meters, scale, 0, 10, core::NoteFraction{1, 16}, CellSize{10, 20});
    CHECK(positions(lines, Role::BarLine).size() == 64);
    CHECK(positions(lines, Role::BeatLine).empty());
    CHECK(positions(lines, Role::GridLine).empty());
}

TEST_CASE("a time scale starting mid-bar draws only the lines in it")
{
    const auto ppqn = core::kPpqn;
    const core::MeterMap meters{ppqn, {4, 4}};
    const auto quarter = core::NoteFraction{1, 4}.toTicks(ppqn);
    // From beat 3 of bar 1, two bars' worth at 100 pixels to a quarter note.
    const TimeScale scale{.start = 2 * quarter, .length = 8 * quarter, .x = 5, .width = 800};
    const auto lines = meterLines(meters, scale, 0, 10, core::NoteFraction{1, 4}, CellSize{10, 20});
    CHECK(positions(lines, Role::BarLine) == std::vector<int>{205, 605});
    CHECK(positions(lines, Role::BeatLine) == std::vector<int>{5, 105, 305, 405, 505, 705});
    CHECK(positions(lines, Role::GridLine).empty()); // every step is a beat
}

TEST_CASE("lines and the space they need grow with the cell, to keep their proportions")
{
    CHECK(hairline({10, 20}) == 1);
    CHECK(hairline({14, 32}) == 2);
    CHECK(hairline({28, 64}) == 4);
    CHECK(hairline({3, 6}) == 1); // never thinner than a pixel
    CHECK(minLineSpacing({10, 20}) == 4);
    CHECK(minLineSpacing({28, 64}) == 11);

    // Beats eight pixels apart are drawn in small cells, but too crowded in large ones.
    const auto ppqn = core::kPpqn;
    const core::MeterMap meters{ppqn, {4, 4}};
    const TimeScale scale{
        .start = 0, .length = core::NoteFraction{4, 1}.toTicks(ppqn), .x = 0, .width = 128};
    const auto small = meterLines(meters, scale, 0, 10, core::NoteFraction{1, 4}, {10, 20});
    const auto large = meterLines(meters, scale, 0, 10, core::NoteFraction{1, 4}, {28, 64});
    CHECK(positions(small, Role::BeatLine).size() == 12);
    CHECK(positions(large, Role::BeatLine).empty());
    CHECK(positions(large, Role::BarLine).size() == 4);
    CHECK(large[0].rect.width == 4);
}
