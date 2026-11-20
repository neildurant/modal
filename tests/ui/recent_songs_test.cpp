// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/recent_songs.hpp>
#include <modal/ui/remembered.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace modal::ui;

TEST_CASE("when a recent song was used reads as today, yesterday, or a date")
{
    using namespace std::chrono;
    const auto at = [](int y, unsigned m, unsigned d, int h, int min)
    { return local_days{year{y} / month{m} / day{d}} + hours{h} + minutes{min}; };
    const auto now = at(2026, 11, 20, 18, 5);
    CHECK(usedText(at(2026, 11, 20, 17, 42), now) == "today 17:42");
    CHECK(usedText(at(2026, 11, 20, 0, 3), now) == "today 00:03");
    CHECK(usedText(at(2026, 11, 19, 9, 10), now) == "yesterday 09:10");
    CHECK(usedText(at(2026, 11, 16, 9, 10), now) == "Mon 16 Nov");
    CHECK(usedText(at(2026, 1, 2, 9, 10), now) == "Fri 2 Jan");
    CHECK(usedText(at(2025, 3, 2, 9, 10), now) == "2 Mar 2025");
}

TEST_CASE("recent songs are kept newest first, to a limit, and read back with spaces")
{
    Remembered remembered;
    remembered.usedSong("/songs/a.modal", 100, 3);
    remembered.usedSong("/songs/b c.modal", 200, 3);
    remembered.usedSong("/songs/d.modal", 300, 3);
    remembered.usedSong("/songs/a.modal", 400, 3); // used again: to the top
    remembered.usedSong("/songs/e.modal", 500, 3); // over the limit: the oldest goes
    REQUIRE(remembered.songs().size() == 3);
    CHECK(remembered.songs()[0].path == "/songs/e.modal");
    CHECK(remembered.songs()[1].path == "/songs/a.modal");
    CHECK(remembered.songs()[1].used == 400);
    CHECK(remembered.songs()[2].path == "/songs/d.modal");

    remembered.usedSong("/songs/b c.modal", 600, 3);
    const auto read = Remembered::read(remembered.text());
    CHECK(read == remembered);
    CHECK(read.songs()[0].path == "/songs/b c.modal");

    CHECK(remembered.forgetSong("/songs/a.modal"));
    CHECK_FALSE(remembered.forgetSong("/songs/a.modal"));
    CHECK(remembered.songs().size() == 2);
}
