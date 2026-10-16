#include <modal/core/event.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace modal::core;

TEST_CASE("event validity")
{
    CHECK(isValid({0, Note{60, 100, kPpqn.quarter()}}));
    CHECK(isValid({0, Note{127, 127, 0, 127}}));
    CHECK_FALSE(isValid({-1, Note{60, 100, kPpqn.quarter()}}));
    CHECK_FALSE(isValid({0, Note{128, 100, kPpqn.quarter()}}));
    CHECK_FALSE(isValid({0, Note{60, 0, kPpqn.quarter()}}));
    CHECK_FALSE(isValid({0, Note{60, 100, -1}}));

    CHECK(isValid({0, ControlChange{7, 100}}));
    CHECK_FALSE(isValid({0, ControlChange{128, 0}}));
    CHECK(isValid({0, ProgramChange{127}}));
    CHECK_FALSE(isValid({0, ProgramChange{128}}));
    CHECK(isValid({0, PitchBend{PitchBend::kMin}}));
    CHECK(isValid({0, PitchBend{PitchBend::kMax}}));
    CHECK_FALSE(isValid({0, PitchBend{PitchBend::kMax + 1}}));
    CHECK(isValid({0, ChannelPressure{64}}));
    CHECK(isValid({0, PolyPressure{60, 64}}));
    CHECK_FALSE(isValid({0, PolyPressure{60, 200}}));
}

TEST_CASE("SysEx must be framed and 7-bit")
{
    CHECK(isValid({0, makeSysEx({0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7})}));
    CHECK(isValid({0, makeSysEx({0xF0, 0xF7})}));
    CHECK_FALSE(isValid({0, makeSysEx({})}));
    CHECK_FALSE(isValid({0, makeSysEx({0xF0})}));
    CHECK_FALSE(isValid({0, makeSysEx({0xF0, 0x01})}));
    CHECK_FALSE(isValid({0, makeSysEx({0x7E, 0xF7})}));
    CHECK_FALSE(isValid({0, makeSysEx({0xF0, 0x90, 0xF7})}));
    CHECK_FALSE(isValid({0, SysEx{}}));
}

TEST_CASE("SysEx and text compare by content and share their data")
{
    const std::vector<std::uint8_t> bytes{0xF0, 0x41, 0x10, 0xF7};
    const Event a{0, makeSysEx(bytes)};
    const Event b{0, makeSysEx(bytes)};
    const Event c{0, makeSysEx({0xF0, 0x42, 0xF7})};
    CHECK(a == b);
    CHECK(a != c);

    const Event copy = a;
    CHECK(std::get<SysEx>(copy.data).bytes == std::get<SysEx>(a.data).bytes);

    CHECK(Event{0, makeMetaText(MetaText::Kind::Lyric, "la")} ==
          Event{0, makeMetaText(MetaText::Kind::Lyric, "la")});
    CHECK(Event{0, makeMetaText(MetaText::Kind::Lyric, "la")} !=
          Event{0, makeMetaText(MetaText::Kind::Text, "la")});
}

TEST_CASE("events are small")
{
    // Tracks hold many events; keep them to a tick plus a small variant.
    CHECK(sizeof(Event) <= 40);
}
