// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/command_line.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace modal::ui;

namespace
{

void type(CommandLine& line, std::u32string_view text)
{
    for (const char32_t c : text)
    {
        line.insert(c);
    }
}

} // namespace

TEST_CASE("the command line is edited at its cursor")
{
    CommandLine line;
    line.open();
    type(line, U"set fontsize=14");
    CHECK(line.cursor() == 15);
    line.left();
    line.left();
    CHECK(line.backspace()); // the =
    type(line, U"=2");
    CHECK(line.text() == U"set fontsize=214");
    CHECK(line.erase());
    CHECK(line.text() == U"set fontsize=24");
    line.end();
    CHECK_FALSE(line.erase());
    line.start();
    CHECK_FALSE(line.backspace());
    line.left(); // stays at the start
    CHECK(line.cursor() == 0);
    type(line, U"é");
    CHECK(line.take() == "\xC3\xA9set fontsize=24"); // as UTF-8
    CHECK(line.text().empty());

    type(line, U"abc");
    line.clear();
    CHECK(line.text().empty());
    CHECK(line.cursor() == 0);
}

TEST_CASE("lines run are recalled, newest first, back to the line being typed")
{
    CommandLine line;
    line.open();
    type(line, U"set theme=light");
    line.take();
    line.open();
    type(line, U"set fontsize=14");
    line.take();
    line.open();
    type(line, U"set fontsize=14"); // the same again isn't kept twice
    line.take();
    line.open();
    line.take(); // nor an empty line

    line.open();
    type(line, U"hi");
    CHECK(line.older());
    CHECK(line.text() == U"set fontsize=14");
    CHECK(line.cursor() == 15);
    CHECK(line.older());
    CHECK(line.text() == U"set theme=light");
    CHECK_FALSE(line.older());
    CHECK(line.newer());
    CHECK(line.newer());
    CHECK(line.text() == U"hi"); // the line being typed
    CHECK_FALSE(line.newer());

    // A recalled line, edited and run, joins the history; a new prompt starts empty.
    CHECK(line.older());
    type(line, U"6");
    CHECK(line.take() == "set fontsize=146");
    line.open();
    CHECK(line.text().empty());
    CHECK(line.older());
    CHECK(line.text() == U"set fontsize=146");
}
