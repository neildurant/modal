// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>
#include <modal/ui/remembered.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr std::int64_t kNow = 1'800'000'000; // seconds since 1970
constexpr std::int64_t kHour = 60 * 60;
constexpr std::int64_t kDay = 24 * kHour;

} // namespace

TEST_CASE("commands run rank by how often and how lately")
{
    Remembered remembered;
    CHECK(remembered.rank("zoom-in", kNow) == 0);
    remembered.ran("zoom-in", kNow - 40 * kDay);
    remembered.ran("zoom-in", kNow - 40 * kDay);
    remembered.ran("grid-finer", kNow - 10);
    // Twice, long ago, against once, just now.
    CHECK(remembered.rank("zoom-in", kNow) < remembered.rank("grid-finer", kNow));
    // Run often enough lately, it ranks first again.
    for (int i = 0; i < 3; ++i)
    {
        remembered.ran("zoom-in", kNow - 2 * kHour);
    }
    CHECK(remembered.rank("zoom-in", kNow) > remembered.rank("grid-finer", kNow));

    remembered.forgetCommands();
    CHECK(remembered.rank("zoom-in", kNow) == 0);
}

TEST_CASE("meters typed are remembered, the latest first, as many as kMaxMeters")
{
    Remembered remembered;
    remembered.typedMeter({7, 8});
    remembered.typedMeter({13, 16});
    remembered.typedMeter({7, 8}); // again: first, once
    CHECK(std::vector(remembered.meters().begin(), remembered.meters().end()) ==
          std::vector<core::TimeSignature>{{7, 8}, {13, 16}});
    for (int i = 1; i <= 20; ++i)
    {
        remembered.typedMeter({i, 4});
    }
    CHECK(remembered.meters().size() == Remembered::kMaxMeters);
    CHECK(remembered.meters().front() == core::TimeSignature{20, 4});
}

TEST_CASE("what's remembered is kept as plain text, and read back the same")
{
    Remembered remembered;
    remembered.ran("zoom-in", kNow);
    remembered.ran("zoom-in", kNow + 5);
    remembered.ran("section-split", kNow - kDay);
    remembered.typedMeter({7, 8});
    const auto text = remembered.text();
    CHECK(text.starts_with("# "));
    CHECK(text.find("ran zoom-in 2 1800000005\n") != std::string::npos);
    CHECK(text.find("meter 7/8\n") != std::string::npos);
    CHECK(Remembered::read(text) == remembered);

    // Lines it doesn't know, or that are damaged, are passed over; Windows line ends are fine.
    const auto read = Remembered::read("ran\r\nran x y z\r\nmeter 3/5\r\nfrom the future 1\r\n"
                                       "meter 5/4\r\nran zoom-out 3 100\r\n");
    CHECK(read.meters().size() == 1);
    CHECK(read.rank("zoom-out", 100) > 0);
    CHECK(read.rank("x", 100) == 0);
}

TEST_CASE("macros are remembered in key notation")
{
    Remembered remembered;
    const auto keys = std::get<std::vector<Key>>(parseKeys("3>>j<Space><lt><C-a>"));
    remembered.setMacro(U'a', keys);
    CHECK(remembered.text().find("macro a 3>>j<Space><lt><C-a>\n") != std::string::npos);
    const auto read = Remembered::read(remembered.text());
    REQUIRE(read.macro(U'a') != nullptr);
    CHECK(*read.macro(U'a') == keys);
    CHECK(read == remembered);
    // Lines it can't read are passed over.
    CHECK(Remembered::read("macro A x\nmacro b <Nope>\nmacro c\n").macros().empty());
    remembered.setMacro(U'a', {});
    CHECK(remembered.macro(U'a') == nullptr);
}
