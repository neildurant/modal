// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/note_fraction.hpp>

#include <catch2/catch_test_macros.hpp>

using modal::core::NoteFraction;
using modal::core::parseNoteFraction;
using modal::core::Tick;

TEST_CASE("note fractions are kept in lowest terms")
{
    const NoteFraction f{2, 8};
    CHECK(f.numerator() == 1);
    CHECK(f.denominator() == 4);
    CHECK(f == NoteFraction{1, 4});
}

TEST_CASE("note fractions order by length")
{
    CHECK(NoteFraction{1, 8} < NoteFraction{1, 4});
    CHECK(NoteFraction{1, 4}.triplet() < NoteFraction{1, 4});
    CHECK(NoteFraction{1, 4}.dotted() > NoteFraction{1, 4});
    CHECK(NoteFraction{3, 12} == NoteFraction{1, 4});
}

TEST_CASE("dotted and triplet lengths")
{
    CHECK(NoteFraction{1, 8}.dotted() == NoteFraction{3, 16});
    CHECK(NoteFraction{1, 8}.triplet() == NoteFraction{1, 12});
    CHECK(NoteFraction{1, 4}.triplet().dotted() == NoteFraction{1, 4});
}

TEST_CASE("note fractions convert to ticks at any resolution")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick quarter = ppqn.quarter();

    CHECK(NoteFraction{1, 4}.toTicks(ppqn) == quarter);
    CHECK(NoteFraction{1, 1}.toTicks(ppqn) == 4 * quarter);
    CHECK(NoteFraction{1, 8}.toTicks(ppqn) == quarter / 2);
    CHECK(NoteFraction{1, 16}.toTicks(ppqn) == quarter / 4);
    CHECK(NoteFraction{1, 8}.dotted().toTicks(ppqn) == quarter * 3 / 4);
    CHECK(NoteFraction{1, 8}.triplet().toTicks(ppqn) == quarter / 3);
    CHECK(NoteFraction{7, 8}.toTicks(ppqn) == quarter * 7 / 2);

    // Lengths that sum musically also sum in ticks when every part is exact.
    const NoteFraction triplet = NoteFraction{1, 8}.triplet();
    REQUIRE(triplet.isExact(ppqn));
    CHECK(3 * triplet.toTicks(ppqn) == NoteFraction{1, 4}.toTicks(ppqn));
}

TEST_CASE("inexact note fractions round to the nearest tick")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const NoteFraction septuplet{1, 28}; // a sixteenth-note septuplet
    const Tick whole = ppqn.whole();

    INFO("ppqn " << ppqn.value);
    const Tick ticks = septuplet.toTicks(ppqn);
    CHECK(septuplet.isExact(ppqn) == (whole % 28 == 0));
    // Nearest tick: at most half a tick away from the true length.
    CHECK(2 * (ticks * 28 - whole) <= 28);
    CHECK(2 * (whole - ticks * 28) <= 28);
}

TEST_CASE("parsing note fractions")
{
    CHECK(parseNoteFraction("1/16") == NoteFraction{1, 16});
    CHECK(parseNoteFraction("3/8") == NoteFraction{3, 8});
    CHECK(parseNoteFraction("1/8.") == NoteFraction{3, 16});
    CHECK(parseNoteFraction("1/4..") == NoteFraction{7, 16});
    CHECK(parseNoteFraction("1/16t") == NoteFraction{1, 24});
    CHECK(parseNoteFraction("2/4") == NoteFraction{1, 2});
}

TEST_CASE("parsing rejects malformed note fractions")
{
    for (const auto* text :
         {"", "/", "1/", "/4", "1", "0/4", "1/0", "-1/4", "1/-4", "1/4x", "1/4...", "1/4t.", " 1/4",
          "1/4 ", "a/4", "1//4", "1/4/2", "99999999999/4"})
    {
        INFO("text \"" << text << '"');
        CHECK_FALSE(parseNoteFraction(text));
    }
}

TEST_CASE("formatting note fractions uses musical spellings")
{
    using modal::core::toString;
    CHECK(toString(NoteFraction{1, 16}) == "1/16");
    CHECK(toString(NoteFraction{1, 12}) == "1/8t");
    CHECK(toString(NoteFraction{1, 3}) == "1/2t");
    CHECK(toString(NoteFraction{3, 16}) == "1/8.");
    CHECK(toString(NoteFraction{3, 2}) == "1/1.");
    CHECK(toString(NoteFraction{7, 16}) == "1/4..");
    CHECK(toString(NoteFraction{5, 8}) == "5/8");
    CHECK(toString(NoteFraction{1, 9}) == "1/9");
    CHECK(toString(NoteFraction{3, 1}) == "3/1");
    CHECK(toString(NoteFraction{1, 28}) == "1/28");
}

TEST_CASE("formatted note fractions parse back to the same value")
{
    for (const NoteFraction f :
         {NoteFraction{1, 1}, NoteFraction{1, 64}, NoteFraction{1, 12}, NoteFraction{1, 6},
          NoteFraction{3, 8}, NoteFraction{7, 32}, NoteFraction{5, 4}, NoteFraction{1, 9},
          NoteFraction{2, 3}, NoteFraction{1, 20}})
    {
        INFO(f.numerator() << '/' << f.denominator());
        CHECK(parseNoteFraction(modal::core::toString(f)) == f);
    }
}
