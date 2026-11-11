// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <string>
#include <vector>

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

TEST_CASE("a message in is shown for the In light in 13 characters, with its channel")
{
    using Bytes = std::vector<std::uint8_t>;
    CHECK(inputText(Bytes{0x90, 61, 127}) == " 1 C#4    127");
    CHECK(inputText(Bytes{0x90, 1, 5}) == " 1 C#-1     5");
    CHECK(inputText(Bytes{0xB9, 64, 127}) == "10 CC64   127");
    CHECK(inputText(Bytes{0xBF, 127, 0}) == "16 CC127    0");
    CHECK(inputText(Bytes{0xC0, 5}) == " 1 Prog     5");
    CHECK(inputText(Bytes{0xD0, 64}) == " 1 Pres    64");
    CHECK(inputText(Bytes{0xA0, 60, 64}) == " 1 PPrs    64");
    CHECK(inputText(Bytes{0xE0, 0x7F, 0x7F}) == " 1 Bend +8191");
    CHECK(inputText(Bytes{0xE0, 0x00, 0x40}) == " 1 Bend     0");
    CHECK(inputText(Bytes{0xE0, 0x00, 0x00}) == " 1 Bend -8192");
    CHECK(inputText(Bytes{}) == std::string(13, ' '));
    for (const auto& bytes : {Bytes{0x90, 61, 127}, Bytes{0xE0, 0, 0}, Bytes{0xBF, 127, 127}})
        CHECK(inputText(bytes).size() == 13);
}
