// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/ui/backups.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace modal::ui;

namespace
{

std::string contents(const std::filesystem::path& file)
{
    std::ifstream stream{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, {}};
}

void write(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream{file, std::ios::binary | std::ios::trunc} << text;
}

} // namespace

TEST_CASE("backups keep each version replaced, newest first, to a limit")
{
    modal::test::TempDirectory temporary{"modal-backups-test"};
    const auto state = temporary.path() / "State";
    const auto song = temporary.path() / "Songs" / "Morning.modal";
    std::filesystem::create_directories(song.parent_path());

    // No file yet: nothing to keep.
    CHECK(backUpSong(state, song, 3).empty());
    CHECK(songBackups(state, song).empty());

    for (const auto* version : {"one", "two", "three", "four"})
    {
        write(song, version);
        CHECK(backUpSong(state, song, 3).empty());
    }
    const auto kept = songBackups(state, song);
    REQUIRE(kept.size() == 3);
    CHECK(kept[0].number == 1);
    CHECK(contents(kept[0].file) == "four");
    CHECK(contents(kept[1].file) == "three");
    CHECK(contents(kept[2].file) == "two");

    // The folder names the song, and says which one it's for.
    const auto folder = backupFolder(state, song);
    CHECK(folder.parent_path() == state / "backups");
    CHECK(folder.filename().string().starts_with("Morning-"));
    CHECK(contents(folder / "song").starts_with(song.lexically_normal().string()));
    CHECK(backupFolder(state, temporary.path() / "Other" / "Morning.modal") != folder);

    // A lower limit drops the oldest at the next backup, and 0 keeps nothing more.
    write(song, "five");
    CHECK(backUpSong(state, song, 2).empty());
    REQUIRE(songBackups(state, song).size() == 2);
    CHECK(contents(songBackups(state, song)[1].file) == "four");
    write(song, "six");
    CHECK(backUpSong(state, song, 0).empty());
    CHECK(contents(songBackups(state, song)[0].file) == "five");
}

TEST_CASE("a backup keeps the time its version was saved, and other files are passed over")
{
    modal::test::TempDirectory temporary{"modal-backups-test"};
    const auto state = temporary.path() / "State";
    const auto song = temporary.path() / "Groove.modal";
    write(song, "saved last week");
    const auto then = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24 * 7};
    std::filesystem::last_write_time(song, then);
    REQUIRE(backUpSong(state, song, 5).empty());
    const auto kept = songBackups(state, song);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].saved == std::filesystem::last_write_time(song));

    write(backupFolder(state, song) / "notes.txt", "mine");
    write(backupFolder(state, song) / "07.modal", "not numbered as Modal numbers them");
    CHECK(songBackups(state, song).size() == 1);
}

TEST_CASE("a backup that can't be made says why")
{
    modal::test::TempDirectory temporary{"modal-backups-test"};
    const auto song = temporary.path() / "Groove.modal";
    write(song, "a song");
    // The state directory is a file, so no folder can be made in it.
    write(temporary.path() / "State", "in the way");
    CHECK(backUpSong(temporary.path() / "State", song, 5).starts_with("can't make "));
}
