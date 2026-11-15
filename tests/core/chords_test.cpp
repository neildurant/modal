// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/chords.hpp>
#include <modal/core/pitch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using modal::core::chordName;

namespace
{

// The chord some notes, named as C4 E4 G4, make.
std::optional<std::string> name(std::string_view notes)
{
    std::vector<std::uint8_t> pitches;
    std::size_t at = 0;
    while (at < notes.size())
    {
        const auto end = std::min(notes.find(' ', at), notes.size());
        pitches.push_back(*modal::core::parsePitch(notes.substr(at, end - at)));
        at = end + 1;
    }
    return chordName(pitches);
}

} // namespace

TEST_CASE("chords are named as lead sheets name them")
{
    CHECK(name("C4 E4 G4") == "C");
    CHECK(name("A3 C4 E4") == "Am");
    CHECK(name("B3 D4 F4") == "Bdim");
    CHECK(name("C4 E4 G#4") == "Caug");
    CHECK(name("D4 G4 A4") == "Dsus4");
    CHECK(name("F3 A3 C4 E4") == "Fmaj7");
    CHECK(name("G3 B3 D4 F4") == "G7");
    CHECK(name("A3 C4 E4 G4") == "Am7");
    CHECK(name("B3 D4 F4 A4") == "Bm7b5");
    CHECK(name("C4 D4 E4 G4") == "Cadd9");
    CHECK(name("C4 E4 G4 Bb4 D5") == "C9");
    // Spelled as lead sheets usually are.
    CHECK(name("Bb3 D4 F4") == "Bb");
    CHECK(name("F#3 A#3 C#4") == "F#");
    CHECK(name("Eb3 Gb3 Bb3 Db4") == "Ebm7");
}

TEST_CASE("a chord's root is on its bass where it can be, and a slash names an inversion")
{
    CHECK(name("E3 G3 C4") == "C/E");
    CHECK(name("G2 C4 E4") == "C/G");
    // The same notes, C6 with C in the bass and Am7 with A.
    CHECK(name("C3 E3 G3 A3") == "C6");
    CHECK(name("A2 C3 E3 G3") == "Am7");
    // Doubled notes and wide spacing don't matter.
    CHECK(name("C2 C3 G3 E4 C5") == "C");
    // A seventh with no fifth.
    CHECK(name("G2 F3 B3") == "G7");
}

TEST_CASE("a melody note over a chord is left out when the chord won't name with it")
{
    // A1 E2 G2 with C4 on top is Am7, all of it; with D4 on top, Am over the bass and a melody
    // note.
    CHECK(name("A1 E2 G2 C4") == "Am7");
    CHECK(name("A1 C2 E2 D4") == "Am");
    // Two pitch classes aren't a chord, nor are notes that make none.
    CHECK(name("C3 C4") == std::nullopt);
    CHECK(name("A1 C4") == std::nullopt);
    CHECK(name("C4 C#4 D4") == std::nullopt);
}
