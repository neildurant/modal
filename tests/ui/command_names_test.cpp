// SPDX-License-Identifier: GPL-3.0-or-later

// The views run commands by comparing the command's name with names written out in the code,
// where a misspelt one compiles and simply never runs. This reads the code and checks each
// name compared is a command there is (added after a code review, November 2026).

#include <modal/ui/commands.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

using namespace modal::ui;

TEST_CASE("every command name the views compare with is a command there is")
{
    CommandRegistry commands;
    commands.add(Workspace::commands());
    commands.add(windowCommands());
    commands.add(configCommands());
    commands.add(promptCommands());
    commands.add(fileCommands());

    const std::regex compared{R"re(\bcommand\s*(?:==|!=)\s*"([a-z0-9-]+)")re"};
    std::set<std::string> names;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator{MODAL_UI_SOURCE_DIR})
    {
        if (entry.path().extension() != ".cpp")
        {
            continue;
        }
        ++files;
        std::ifstream in{entry.path(), std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        for (std::sregex_iterator match{text.begin(), text.end(), compared}, end; match != end;
             ++match)
        {
            names.insert((*match)[1].str());
        }
    }
    REQUIRE(files > 20);         // the code was found
    REQUIRE(names.size() > 150); // and read as expected
    // Names a view gives itself, to run part of a command again, which nothing binds or lists.
    const std::set<std::string> internal{"lane-value-set-zero"};
    for (const auto& name : names)
    {
        CAPTURE(name);
        CHECK((commands.find(name) != nullptr || internal.contains(name)));
    }
}
