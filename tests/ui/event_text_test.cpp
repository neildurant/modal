#include <modal/ui/event_text.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace modal;
using namespace modal::ui;

TEST_CASE("positions pad their ticks to a quarter note's width")
{
    CHECK(tickDigits(core::kPpqn) == 3);
    CHECK(tickDigits(core::Ppqn{6720}) == 4);
    CHECK(tickDigits(core::Ppqn{96}) == 2);

    const core::MeterMap meter{core::kPpqn};
    CHECK(positionText(meter, 0) == "1:1:000");
    CHECK(positionText(meter, 5 * 960 + 40) == "2:2:040");
    CHECK(positionText(core::MeterMap{core::Ppqn{96}}, 97) == "1:2:01");
}

TEST_CASE("lengths show as note values, with ticks over or under when not exact")
{
    const auto ppqn = core::Ppqn{GENERATE(960, 6720)};
    CAPTURE(ppqn.value);
    const auto ticks = [&](core::NoteFraction f) { return f.toTicks(ppqn); };
    const core::TimeSignature common{4, 4};
    const auto note = [&](core::Tick length)
    { return lengthText(length, ppqn, LengthFormat::Note, common); };

    CHECK(note(ticks({1, 8})) == "1/8");
    CHECK(note(ticks({3, 16})) == "1/8.");
    CHECK(note(ticks({1, 12})) == "1/8t");
    CHECK(note(ticks({3, 1})) == "3/1");
    CHECK(note(0) == "0");

    // The nearest common length, and how far off.
    CHECK(note(ticks({1, 8}) + 12) == "1/8 + 12");
    CHECK(note(ticks({1, 4}) - 7) == "1/4 - 7");
    CHECK(note(2 * ppqn.whole() + 5) == "2/1 + 5");
    CHECK(note(1) == "1 tick");
    CHECK(note(3) == "3 ticks");
}

TEST_CASE("lengths show as bars, beats and ticks, in the meter where they start")
{
    const core::Ppqn ppqn = core::kPpqn;
    CHECK(lengthText(252 + 480 * 1, ppqn, LengthFormat::Beats, {6, 8}) == "0:1:252");
    CHECK(lengthText(960 * 4 + 960 + 12, ppqn, LengthFormat::Beats, {4, 4}) == "1:1:012");
    CHECK(lengthText(0, ppqn, LengthFormat::Beats, {4, 4}) == "0:0:000");
}
