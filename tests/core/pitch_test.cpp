// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using modal::core::parsePitch;
using modal::core::pitchName;

TEST_CASE("pitches are named with middle C as C4, and sharps")
{
    CHECK(pitchName(60) == "C4");
    CHECK(pitchName(61) == "C#4");
    CHECK(pitchName(40) == "E2");
    CHECK(pitchName(0) == "C-1");
    CHECK(pitchName(127) == "G9");
}

TEST_CASE("pitch names read back, with flats and either case")
{
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        const auto name = pitchName(static_cast<std::uint8_t>(pitch));
        CAPTURE(name);
        CHECK(parsePitch(name) == pitch);
    }
    CHECK(parsePitch("Bb2") == 46);
    CHECK(parsePitch("e2") == 40);
    CHECK(parsePitch("Cb4") == 59);
    CHECK(parsePitch("B#3") == 60);
}

TEST_CASE("anything else isn't a pitch")
{
    for (const auto* text : {"", "H2", "C", "C#", "C10", "C-2", "Cb-1", "G#9", "C4x", "C 4", "#4"})
    {
        CAPTURE(text);
        CHECK(parsePitch(text) == std::nullopt);
    }
}
