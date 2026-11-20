// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/track_import.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace modal::ui;

namespace
{

std::vector<std::size_t> tracks(std::string_view text, std::size_t count)
{
    const auto result = parseTrackSelection(text, count);
    REQUIRE(std::holds_alternative<std::vector<std::size_t>>(result));
    return std::get<std::vector<std::size_t>>(result);
}

std::string error(std::string_view text, std::size_t count)
{
    const auto result = parseTrackSelection(text, count);
    REQUIRE(std::holds_alternative<std::string>(result));
    return std::get<std::string>(result);
}

} // namespace

TEST_CASE("a track selection takes numbers, ranges, all and none")
{
    using List = std::vector<std::size_t>;
    CHECK(tracks("1, 3-5, 7", 8) == List{0, 2, 3, 4, 6});
    CHECK(tracks("1,3-5,7", 8) == List{0, 2, 3, 4, 6});
    CHECK(tracks(" 4 2 2 ", 5) == List{1, 3});
    CHECK(tracks("3-3", 5) == List{2});
    CHECK(tracks("ALL", 3) == List{0, 1, 2});
    CHECK(tracks("none", 3).empty());
    CHECK(tracks("", 3).empty());
}

TEST_CASE("a track selection says what's wrong with it")
{
    CHECK(error("9", 5) == "No track 9: the file has 5");
    CHECK(error("0", 5) == "No track 0: the file has 5");
    CHECK(error("2-6", 5) == "No track 6: the file has 5");
    CHECK(error("5-3", 5) == "5-3 runs backwards: write 3-5");
    CHECK(error("1, kick", 5) == "Not a track number or range: kick");
    CHECK(error("1-", 5) == "Not a track number or range: 1-");
}

TEST_CASE("ticked tracks are written as the command line's selection")
{
    using List = std::vector<std::size_t>;
    CHECK(trackSelectionText(List{0, 2, 3, 4, 6}, 8) == "1,3-5,7");
    CHECK(trackSelectionText(List{1}, 8) == "2");
    CHECK(trackSelectionText(List{0, 1}, 8) == "1-2");
    CHECK(trackSelectionText(List{0, 1, 2}, 3) == "all");
    CHECK(tracks(trackSelectionText(List{0, 2, 3, 4, 6}, 8), 8) == List{0, 2, 3, 4, 6});
}

TEST_CASE("a selection is told from the end of a filename")
{
    CHECK(looksLikeTrackSelection("1, 3-5"));
    CHECK(looksLikeTrackSelection("All"));
    CHECK(looksLikeTrackSelection("none"));
    CHECK_FALSE(looksLikeTrackSelection("Groove"));
    CHECK_FALSE(looksLikeTrackSelection("2.mid"));
    CHECK_FALSE(looksLikeTrackSelection(", -"));
    CHECK_FALSE(looksLikeTrackSelection(""));
}
