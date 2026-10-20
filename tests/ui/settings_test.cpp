#include <modal/ui/settings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>

using namespace modal::ui;

TEST_CASE("every setting has a unique name, a description and its default to begin with")
{
    const Settings settings;
    std::set<std::string_view> names;
    for (const auto& setting : settings.all())
    {
        CAPTURE(setting.name);
        CHECK(names.insert(setting.name).second);
        CHECK_FALSE(setting.description.empty());
        CHECK(setting.value == setting.defaultValue);
        CHECK(setting.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz") ==
              std::string_view::npos);
    }
    CHECK(settings.integer("timeoutlen") == 500);
    CHECK(settings.number("fontsize") == 12.0);
    CHECK(settings.text("font").empty());
    CHECK(settings.text("theme") == "dark");
    CHECK(settings.cells("minsize") == CellCount{80, 24});
    CHECK(settings.find("nope") == nullptr);
}

TEST_CASE("settings are set from text, checked against their type and range")
{
    Settings settings;
    CHECK(settings.set("timeoutlen", "300").empty());
    CHECK(settings.integer("timeoutlen") == 300);
    CHECK(settings.set("timeoutlen", "3.5") == "timeoutlen must be a whole number from 0 to 10000");
    CHECK(settings.set("timeoutlen", "20000") ==
          "timeoutlen must be a whole number from 0 to 10000");
    CHECK(settings.set("timeoutlen", "") == "timeoutlen must be a whole number from 0 to 10000");
    CHECK(settings.integer("timeoutlen") == 300); // unchanged by the errors

    CHECK(settings.set("fontsize", "10.5").empty());
    CHECK(settings.number("fontsize") == 10.5);
    CHECK(settings.set("fontsize", "huge") == "fontsize must be a number from 4 to 72");
    CHECK(settings.set("fontsize", "100") == "fontsize must be a number from 4 to 72");

    CHECK(settings.set("minsize", "100x30").empty());
    CHECK(settings.cells("minsize") == CellCount{100, 30});
    CHECK(settings.set("minsize", "100") ==
          "minsize must be columns x rows, such as 80x24, each from 20 to 1000");
    CHECK(settings.set("minsize", "10x30") ==
          "minsize must be columns x rows, such as 80x24, each from 20 to 1000");

    CHECK(settings.set("font", "C:/Fonts/Iosevka.ttf").empty());
    CHECK(settings.text("font") == "C:/Fonts/Iosevka.ttf");
    CHECK(settings.set("font", "").empty()); // back to the built-in font

    CHECK(settings.set("fontsiz", "12") == "unknown setting \"fontsiz\"");
}

TEST_CASE("a setting of a few words takes only those, the first by default")
{
    Settings settings;
    CHECK(settings.text("lenformat") == "note");
    CHECK(settings.set("lenformat", "beats").empty());
    CHECK(settings.text("lenformat") == "beats");
    CHECK(settings.set("lenformat", "ticks") == "lenformat must be note or beats");
    CHECK(settings.text("lenformat") == "beats");
    CHECK(settings.integer("undolevels") == 1000);
}

TEST_CASE("values are written as set reads them")
{
    CHECK(formatValue(std::int64_t{500}) == "500");
    CHECK(formatValue(12.0) == "12");
    CHECK(formatValue(10.5) == "10.5");
    CHECK(formatValue(std::string{"dark"}) == "dark");
    CHECK(formatValue(CellCount{80, 24}) == "80x24");

    Settings settings;
    const Settings defaults;
    for (const auto& setting : defaults.all())
    {
        CAPTURE(setting.name);
        CHECK(settings.set(setting.name, formatValue(setting.defaultValue)).empty());
        CHECK(settings.find(setting.name)->value == setting.defaultValue);
    }
}
