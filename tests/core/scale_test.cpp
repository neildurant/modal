// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/scale.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using namespace modal::core;

TEST_CASE("a key is read as a tonic and a scale, major if none, and Dm for minor")
{
    const auto scales = builtInScales();
    const auto dorian = parseKey("D dorian", scales);
    REQUIRE(dorian);
    CHECK(dorian->tonic == 2);
    CHECK(dorian->text() == "D dorian");
    CHECK(parseKey("eb", scales)->text() == "Eb major");
    CHECK(parseKey("Dm", scales)->text() == "D minor");
    CHECK(parseKey("F# Aeolian", scales)->text() == "F# minor"); // by its alias
    CHECK(parseKey("C pentatonic", scales)->scale.steps.size() == 5);
    CHECK_FALSE(parseKey("H", scales));
    CHECK_FALSE(parseKey("D nonsense", scales));
    CHECK_FALSE(parseKey("", scales));
}

TEST_CASE("a note's place in a key is the scale's note at or below it, and how far above")
{
    const auto key = *parseKey("D dorian", builtInScales()); // D E F G A B C
    const auto d4 = key.place(62);
    CHECK(key.pitchOf(d4.degree) == 62);
    CHECK(d4.above == 0);
    const auto sharp = key.place(66); // F#4: F4 and one above
    CHECK(key.pitchOf(sharp.degree) == 65);
    CHECK(sharp.above == 1);
    CHECK(key.pitchOf(d4.degree + 7) == 74); // an octave of degrees up
    CHECK(key.pitchOf(d4.degree - 1) == 60); // C4, below D4
    CHECK(key.holds(60));
    CHECK_FALSE(key.holds(61));
    CHECK(key.place(0).degree >= 0); // C-1, the lowest note
}

TEST_CASE("a scale's degrees are read as a modalrc writes them")
{
    const auto hijaz = parseDegrees("1 b2 3 4 5 b6 b7");
    REQUIRE(std::holds_alternative<std::vector<int>>(hijaz));
    CHECK(std::get<std::vector<int>>(hijaz) == std::vector{0, 1, 4, 5, 7, 8, 10});
    CHECK(std::holds_alternative<std::string>(parseDegrees("2 3")));
    CHECK(std::holds_alternative<std::string>(parseDegrees("1 3 2")));
    CHECK(std::holds_alternative<std::string>(parseDegrees("1 x")));
    CHECK(std::holds_alternative<std::string>(parseDegrees("1")));
}
