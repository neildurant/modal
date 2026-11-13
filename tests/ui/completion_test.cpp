// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

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

    // Spaces in quotes are in the word, closed or not.
    const std::u32string path = U"e \"My Songs/ve";
    CHECK(wordAt(path, path.size()).start == 2);
    CHECK(wordAt(path, path.size()).index == 1);
    CHECK(wordAt(U"set font=\"A B\" x", 17).index == 2);
}

TEST_CASE("a word in quotes matches the choices, quoted or not")
{
    const Strings files{"\"My Songs/", "\"notes.modal\""};
    CHECK(matching(files, "My") == Strings{"\"My Songs/"});
    CHECK(matching(files, "\"no") == Strings{"\"notes.modal\""});
}

TEST_CASE("files and folders complete from a folder, the word's folder before them")
{
    const modal::test::TempDirectory temporary{"modal-completion-test"};
    const auto& folder = temporary.path();
    std::filesystem::create_directories(folder / "songs");
    std::ofstream{folder / "songs" / "verse.modal"};
    std::ofstream{folder / "song.mid"};
    std::ofstream{folder / ".hidden"};
    CHECK(fileChoices(folder, "") == Strings{"song.mid", "songs/"});
    CHECK(fileChoices(folder, "songs/v") == Strings{"songs/verse.modal"});
    CHECK(fileChoices(folder, ".h") == Strings{".hidden", "song.mid", "songs/"});
    CHECK(fileChoices(folder, "nowhere/x").empty());

    // From anywhere, typed absolute, or from ~.
    const auto absolute = utf8Text(folder) + "/songs/";
    CHECK(fileChoices("elsewhere", absolute) == Strings{absolute + "verse.modal"});
    CHECK(fileChoices("elsewhere", "~/so", folder) == Strings{"~/song.mid", "~/songs/"});
}

TEST_CASE("files with spaces complete in quotes, a folder left open, and \\ separates on Windows")
{
    const modal::test::TempDirectory temporary{"modal-completion-spaces-test"};
    const auto& folder = temporary.path();
    std::filesystem::create_directories(folder / "My Songs");
    std::ofstream{folder / "My Songs" / "verse.modal"};
    std::ofstream{folder / "notes.modal"};
    CHECK(fileChoices(folder, "") == Strings{"\"My Songs/", "\"notes.modal\""});
    CHECK(fileChoices(folder, "\"My Songs/") == Strings{"\"My Songs/verse.modal\""});
    CHECK(fileChoices(folder, "\"no") == fileChoices(folder, "")); // a quote begun
    if constexpr (std::filesystem::path::preferred_separator == L'\\')
    {
        CHECK(fileChoices(folder, "\"My Songs\\") == Strings{"\"My Songs\\verse.modal\""});
        std::filesystem::create_directories(folder / "My Songs" / "Old");
        CHECK(fileChoices(folder, "\"My Songs\\O") ==
              Strings{"\"My Songs\\Old\\", "\"My Songs\\verse.modal\""});
    }
    else
    {
        CHECK(fileChoices(folder, "My Songs\\") == fileChoices(folder, "")); // a character
    }
}

TEST_CASE("a path typed is in a folder, unless it's absolute or from ~, without its quotes")
{
    const std::filesystem::path folder = utf8Path("songs");
    const std::filesystem::path home = utf8Path("home");
    CHECK(typedPath("verse.modal", folder, home) == folder / "verse.modal");
    CHECK(typedPath("\"My Songs/verse.modal\"", folder, home) ==
          folder / utf8Path("My Songs/verse.modal"));
    CHECK(typedPath("~/verse.modal", folder, home) == home / "verse.modal");
    CHECK(typedPath("~", folder, home) == home);
    CHECK(typedPath("~/x", folder, std::nullopt) == folder / "~/x");
    const auto absolute = std::filesystem::absolute(folder) / "verse.modal";
    CHECK(typedPath(utf8Text(absolute), folder, home) == absolute);
    CHECK(typedRelative("verse.modal"));
    CHECK(typedRelative("\"My Songs/"));
    CHECK_FALSE(typedRelative("~/songs"));
    CHECK_FALSE(typedRelative(utf8Text(absolute)));
    CHECK_FALSE(typedRelative("/songs"));
}

TEST_CASE("set completes settings' names, and then their values")
{
    const Settings settings;
    const Strings themes{"dark", "light"};
    const auto names = settingChoices(settings, "", themes);
    CHECK(std::ranges::find(names, "theme") != names.end());
    CHECK(std::ranges::find(names, "rewind") != names.end());
    CHECK(std::ranges::find(names, "norewind") != names.end());
    CHECK(settingChoices(settings, "theme=", themes) == Strings{"theme=dark", "theme=light"});
    CHECK(settingChoices(settings, "rewind=", themes) == Strings{"rewind=on", "rewind=off"});
    CHECK(settingChoices(settings, "fontsize=", themes) == Strings{"fontsize=12"});
    CHECK(settingChoices(settings, "nosuch=", themes).empty());
    // A value with spaces in quotes, as set takes it.
    Settings named;
    REQUIRE(named.set("ownportname", "My Modal").empty());
    CHECK(settingChoices(named, "ownportname=", themes) == Strings{"ownportname=\"My Modal\""});
    // A name typed whole, and no other's start, goes on to its values.
    CHECK(settingChoices(settings, "fontsize", themes) == Strings{"fontsize=12"});
    CHECK(settingChoices(settings, "font", themes).size() > 1);
}
