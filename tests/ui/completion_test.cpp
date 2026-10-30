// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/settings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace modal::ui;

using Strings = std::vector<std::string>;

TEST_CASE("completion matches the start of a choice, or else the start of a word in it")
{
    const Strings commands{"bar-delete", "bar-insert", "event-delete", "go-section-strip", "set"};
    CHECK(matching(commands, "ba") == Strings{"bar-delete", "bar-insert"});
    CHECK(matching(commands, "BA") == Strings{"bar-delete", "bar-insert"});
    CHECK(matching(commands, "del") == Strings{"bar-delete", "event-delete"});
    CHECK(matching(commands, "strip") == Strings{"go-section-strip"});
    CHECK(matching(commands, "s") == Strings{"set"}); // a start before any word's
    CHECK(matching(commands, "zz").empty());
    CHECK(matching(commands, "") == commands);
}

TEST_CASE("completion goes as far as the matches agree")
{
    CHECK(sharedStart(Strings{"bar-delete", "bar-insert"}) == "bar-");
    CHECK(sharedStart(Strings{"Verse", "verse 2"}) == "Verse");
    CHECK(sharedStart(Strings{"set"}) == "set");
    CHECK(sharedStart(Strings{}).empty());
}

TEST_CASE("the word under the cursor, and which word of the line it is")
{
    const std::u32string line = U"set theme=da  font";
    CHECK(wordAt(line, 2).start == 0);
    CHECK(wordAt(line, 2).index == 0);
    const auto theme = wordAt(line, 12);
    CHECK(theme.start == 4);
    CHECK(theme.end == 12);
    CHECK(theme.index == 1);
    const auto between = wordAt(line, 13); // after a space: a new, empty word
    CHECK(between.start == 13);
    CHECK(between.end == 13);
    CHECK(between.index == 2);
    CHECK(wordAt(line, line.size()).index == 2);
    CHECK(wordAt(U"set ", 4).index == 1);
}

TEST_CASE("files and folders complete from a folder, the word's folder before them")
{
    const auto folder = std::filesystem::temp_directory_path() / "modal-completion-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "songs");
    std::ofstream{folder / "songs" / "verse.modal"};
    std::ofstream{folder / "song.mid"};
    std::ofstream{folder / ".hidden"};
    CHECK(fileChoices(folder, "") == Strings{"song.mid", "songs/"});
    CHECK(fileChoices(folder, "songs/v") == Strings{"songs/verse.modal"});
    CHECK(fileChoices(folder, ".h") == Strings{".hidden", "song.mid", "songs/"});
    CHECK(fileChoices(folder, "nowhere/x").empty());
    std::filesystem::remove_all(folder);
}

TEST_CASE("set completes settings' names, and then their values")
{
    const Settings settings;
    const Strings themes{"dark", "light"};
    const auto names = settingChoices(settings, "", themes);
    CHECK(std::ranges::find(names, "theme=") != names.end());
    CHECK(std::ranges::find(names, "rewind") != names.end());
    CHECK(std::ranges::find(names, "norewind") != names.end());
    CHECK(settingChoices(settings, "theme=", themes) == Strings{"theme=dark", "theme=light"});
    CHECK(settingChoices(settings, "rewind=", themes) == Strings{"rewind=on", "rewind=off"});
    CHECK(settingChoices(settings, "fontsize=", themes) == Strings{"fontsize=12"});
    CHECK(settingChoices(settings, "nosuch=", themes).empty());
}
