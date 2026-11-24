// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/armed_range.hpp>
#include <modal/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace modal::core;

namespace
{

// Eight bars of 4/4 in three sections, A (bars 1-2), B (3-5) and C (6-8).
Song threeSections(Ppqn ppqn)
{
    Song song{ppqn};
    const Tick bar = ppqn.whole();
    song.renameSection(0, "A");
    song.splitSection(2 * bar, "B");
    song.splitSection(5 * bar, "C");
    return song;
}

std::optional<ArmedSpan> spanOf(const Song& song, const ArmedRange& range)
{
    return find(song, range).span;
}

} // namespace

TEST_CASE("A section keeps its id through edits, and a new or copied one has its own",
          "[song][armed]")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    auto song = threeSections(ppqn);
    const Tick bar = ppqn.whole();
    const auto id = [&](std::size_t i) { return song.sections()[i]->id(); };
    const auto a = id(0);
    const auto b = id(1);
    const auto c = id(2);
    CHECK(a != b);
    CHECK(b != c);

    const Song before = song;
    song.renameSection(1, "Verse");
    song.resizeSection(1, 2);
    CHECK(id(1) == b);
    CHECK(song.moveSection(1, 1) == 2);
    CHECK(id(2) == b);
    CHECK(id(1) == c);
    song = before; // as undo does
    CHECK(id(1) == b);

    // Bars inserted into B, at its start and in its middle, and removed from it.
    REQUIRE(song.insertBars(2 * bar, 1).done);
    CHECK(id(1) == b);
    REQUIRE(song.insertBars(4 * bar, 1).done);
    CHECK(id(1) == b);
    REQUIRE(song.removeBars(2 * bar, 1).done); // from its start, the rest staying B
    CHECK(id(1) == b);
    REQUIRE(song.removeBars(3 * bar, 1).done); // from its middle
    CHECK(id(1) == b);
    CHECK(song.sections().size() == 3);

    // Split off, the first part stays the section; copied in, a section is new.
    song.splitSection(3 * bar, "B2");
    CHECK(id(1) == b);
    CHECK(id(2) != b);
    const std::vector<std::shared_ptr<const Section>> copy{song.sections()[0]};
    song.insertSections(1, copy);
    CHECK(id(0) == a);
    CHECK(id(1) != a);

    // Joined, it's the first.
    song.joinSections(0);
    CHECK(id(0) == a);
}

TEST_CASE("An armed section follows its section, and is gone with it", "[song][armed]")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    auto song = threeSections(ppqn);
    const Tick bar = ppqn.whole();
    const auto range = ArmedRange::section(song, 1);
    CHECK(spanOf(song, range) == ArmedSpan{2 * bar, 5 * bar});

    song.resizeSection(0, 1); // A a bar longer, so B moves along
    CHECK(spanOf(song, range) == ArmedSpan{3 * bar, 6 * bar});
    song.renameSection(1, "Verse");
    song.resizeSection(1, -1);
    CHECK(spanOf(song, range) == ArmedSpan{3 * bar, 5 * bar});
    song.moveSection(1, -1);
    CHECK(spanOf(song, range) == ArmedSpan{0, 2 * bar});

    song.removeSections(0, 1);
    const auto found = find(song, range);
    CHECK_FALSE(found.span);
    CHECK(found.gone == "its section is gone");
}

TEST_CASE("Armed labels follow their labels, by section and name", "[song][armed]")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    auto song = threeSections(ppqn);
    const Tick bar = ppqn.whole();
    song.setLabel(bar, "in");                // in A
    song.setLabel(6 * bar + bar / 2, "out"); // in C
    const auto range = ArmedRange::labels(song, 0, 1);
    CHECK(spanOf(song, range) == ArmedSpan{bar, 6 * bar + bar / 2});

    song.resizeSection(1, 1); // B grows, so C and its label move along
    CHECK(spanOf(song, range) == ArmedSpan{bar, 7 * bar + bar / 2});

    // Moved within its section, a label is followed.
    song.removeLabel(bar);
    song.setLabel(bar / 2, "in");
    CHECK(spanOf(song, range) == ArmedSpan{bar / 2, 7 * bar + bar / 2});

    // Its end before its start.
    auto swapped = song;
    swapped.moveSection(2, -2);
    CHECK(find(swapped, range).gone == "out is no longer after in");

    song.removeLabel(7 * bar + bar / 2);
    CHECK(find(song, range).gone == "the label out is gone");
}

TEST_CASE("An armed span of time stays in time, cut short at the song's end", "[song][armed]")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    auto song = threeSections(ppqn);
    const Tick bar = ppqn.whole();
    const auto range = ArmedRange::time(3 * bar, 7 * bar);
    CHECK(spanOf(song, range) == ArmedSpan{3 * bar, 7 * bar});
    song.insertBars(0, 1);
    CHECK(spanOf(song, range) == ArmedSpan{3 * bar, 7 * bar});
    song.removeSections(2, 1); // C goes: the song ends at bar 6
    CHECK(spanOf(song, range) == ArmedSpan{3 * bar, 6 * bar});
    song.removeSections(1, 1);
    CHECK(find(song, range).gone == "the song now ends before it");
}
