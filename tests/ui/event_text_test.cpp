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

TEST_CASE("lengths show as bars, beats and ticks, in the meter where they start")
{
    const core::Ppqn ppqn = core::kPpqn;
    CHECK(lengthText(252 + 480, ppqn, {6, 8}) == "0:1:252 (≈1/8.)");
    CHECK(lengthText(960 * 4 + 960 + 12, ppqn, {4, 4}) == "1:1:012");
    CHECK(lengthText(0, ppqn, {4, 4}) == "0:0:000");
    CHECK(lengthText(5, core::Ppqn{96}, {4, 4}) == "0:0:05");
}

TEST_CASE("a length close to a common note value shows it in brackets")
{
    const auto ppqn = core::Ppqn{GENERATE(960, 6720)};
    CAPTURE(ppqn.value);
    const auto ticks = [&](core::NoteFraction f) { return f.toTicks(ppqn); };
    const auto value = [&](core::Tick length)
    {
        const auto text = lengthText(length, ppqn, {4, 4});
        const auto open = text.find(" (");
        return open == std::string::npos ? std::string{} : text.substr(open + 1);
    };

    CHECK(value(ticks({1, 8})) == "(1/8)");
    CHECK(value(ticks({3, 16})) == "(1/8.)");
    CHECK(value(ticks({1, 12})) == "(1/8t)");
    CHECK(value(ticks({1, 16})) == "(1/16)");
    CHECK(value(ticks({3, 1})) == "(3/1)");

    // Within 5% of one, close to it.
    CHECK(value(ticks({1, 8}) + 12) == "(≈1/8)");
    CHECK(value(ticks({1, 4}) - ticks({1, 4}) / 20) == "(≈1/4)");
    CHECK(value(ticks({1, 4}) - ticks({1, 4}) / 20 - 1).empty());
    CHECK(value(ticks({23, 128})) == "(≈1/8.)"); // 4% short of 3/16

    // Lengths no musician would name, and smaller values, show none.
    CHECK(value(ticks({13, 48})).empty());
    CHECK(value(ticks({7, 96})).empty());
    CHECK(value(ticks({1, 32})).empty());
    CHECK(value(ticks({1, 24})).empty()); // 1/16t
    CHECK(value(0).empty());
}
