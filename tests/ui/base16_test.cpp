// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/base16.hpp>
#include <modal/ui/themes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace modal::ui;

TEST_CASE("A base16 scheme reads in tinted-theming's form, with comments and either case")
{
    const auto read = readBase16(R"yaml(system: "base16"
name: "Test"
author: "Someone (https://example.com)"
variant: "dark"
# a comment
palette:
  base00: "#282828" # ----
  base01: "#3c3836"
  base02: "#504945"
  base03: "#665c54"
  base04: "#bdae93"
  base05: "#d5c4a1"
  base06: "#ebdbb2"
  base07: "#fbf1c7"
  base08: "#FB4934"  # red # with a hash
  base09: "#fe8019"
  base0A: "#fabd2f"
  base0B: "#b8bb26"
  base0C: "#8ec07c"
  base0D: "#83a598"
  base0E: "#d3869b"
  base0F: "#d65d0e"
)yaml");
    INFO(read.error);
    REQUIRE(read.scheme);
    CHECK(read.scheme->name == "Test");
    CHECK(read.scheme->author == "Someone (https://example.com)");
    REQUIRE(read.scheme->colours.size() == 16);
    CHECK(read.scheme->colours[0] == Colour{0x28, 0x28, 0x28});
    CHECK(read.scheme->colours[8] == Colour{0xfb, 0x49, 0x34});
    CHECK(read.scheme->colours[15] == Colour{0xd6, 0x5d, 0x0e});
}

TEST_CASE("A base16 scheme reads in the older form, and base24 gives eight more colours")
{
    std::string older = "scheme: \"Old\"\nauthor: \"Someone\"\n";
    std::string base24 = "name: \"Wide\"\n";
    const std::string digits = "0123456789ABCDEF";
    for (int i = 0; i < 24; ++i)
    {
        const auto key = std::string{"base"} + digits[i / 16] + digits[i % 16];
        const auto hex = std::string{"1020"} + digits[i / 16] + digits[i % 16];
        if (i < 16)
        {
            older += key + ": " + hex + "\n";
        }
        base24 += "  " + key + ": \"#" + hex + "\"\n";
    }
    const auto old = readBase16(older);
    REQUIRE(old.scheme);
    CHECK(old.scheme->name == "Old");
    CHECK(old.scheme->colours.size() == 16);
    CHECK(old.scheme->colours[10] == Colour{0x10, 0x20, 0x0a});
    const auto wide = readBase16(base24);
    REQUIRE(wide.scheme);
    REQUIRE(wide.scheme->colours.size() == 24);
    CHECK(wide.scheme->colours[0x12] == Colour{0x10, 0x20, 0x12});
}

TEST_CASE("A base16 scheme that can't be read says why")
{
    CHECK(readBase16("name: \"No colours\"\n").error ==
          "it has no base00, so it isn't a base16 scheme");
    CHECK(readBase16("name: x\nbase00: \"#12345\"\n").error ==
          "line 2: base00 should be a colour, as \"#rrggbb\", not \"#12345\"");
    CHECK(readBase16("name x\n").error == "line 1 isn't a name and a value");
}

TEST_CASE("A scheme as a theme names its scheme and author and gives every role a colour")
{
    Base16Scheme scheme{.name = "Test", .author = "Someone", .colours = {}};
    for (std::uint8_t i = 0; i < 16; ++i)
    {
        scheme.colours.push_back({.r = i, .g = i, .b = i});
    }
    const auto text = base16ThemeText(scheme);
    CHECK(text.starts_with("\" Test, by Someone, as a Modal theme.\n"));
    for (std::size_t i = 0; i < kRoleCount; ++i)
    {
        const auto role = std::string{roleName(static_cast<Role>(i))};
        CHECK(text.find("highlight " + role + " ") != std::string::npos);
    }
    CHECK(text.find("highlight background #000000\n") != std::string::npos);
    CHECK(text.find("highlight note #0b0b0b\n") != std::string::npos);
}

TEST_CASE("The built-in schemes are themes, each its own")
{
    const auto dark = *builtInTheme("dark");
    for (const auto name :
         {"gruvbox-dark-medium", "gruvbox-light-medium", "nord", "dracula", "solarized-dark",
          "solarized-light", "catppuccin-mocha", "catppuccin-latte"})
    {
        CAPTURE(name);
        const auto theme = builtInTheme(name);
        REQUIRE(theme);
        CHECK_FALSE(*theme == dark);
        CHECK(builtInThemeText(name)->find(", by ") != std::string::npos);
    }
}
