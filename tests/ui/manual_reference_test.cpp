// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

namespace
{

using namespace modal::ui;

// Descriptions in the registries are plain text; do not let punctuation become markup.
std::string prose(std::string_view text)
{
    std::string result;
    for (const char c : text)
    {
        if (std::string_view{"\\`*_[]<>|"}.find(c) != std::string_view::npos)
        {
            result += '\\';
        }
        result += c;
    }
    return result;
}

std::string code(std::string_view text, bool tableChoices = false)
{
    std::string result = "`` ";
    for (const char c : text)
    {
        if (c == '|' && tableChoices)
        {
            result += " / ";
        }
        else
        {
            result += c;
        }
    }
    return result + " ``";
}

std::string number(double value)
{
    const auto integer = static_cast<std::int64_t>(value);
    return value == static_cast<double>(integer) ? std::to_string(integer)
                                                 : formatValue(SettingValue{value});
}

std::string settingsReference()
{
    const Settings settings;
    std::string result = "<!-- Generated from Settings; see tools/manual-references.cmake. -->\n\n"
                         "| Setting | Default | Meaning |\n| :--- | :--- | :--- |\n";
    for (const auto& setting : settings.all())
    {
        const auto value = formatValue(setting.defaultValue);
        result += "| " + code(setting.name) + " | " + (value.empty() ? "(empty)" : code(value)) +
                  " | " + prose(setting.description);
        if (std::holds_alternative<std::int64_t>(setting.defaultValue) ||
            std::holds_alternative<double>(setting.defaultValue) ||
            std::holds_alternative<CellCount>(setting.defaultValue))
        {
            result += ". Range: " + number(setting.minimum) + " to " + number(setting.maximum);
        }
        result += " |\n";
    }
    result += "\n: Application settings and startup defaults. "
              "{#tbl-settings tbl-colwidths=\"[26,12,62]\"}\n";
    return result;
}

std::string answer(std::string_view value)
{
    if (value == "{grid}")
        return "the track's grid";
    if (value == "{lift}")
        return "the pedallift setting";
    if (value == "{press}")
        return "the pedaldown setting";
    if (value == "{first}")
        return "the first note";
    if (value == "{search}")
        return "the current search";
    if (value == "{track}")
        return "a suggested destination track";
    return value.empty() ? "no fixed default" : code(value);
}

std::string transformsReference()
{
    const auto keymap = Workspace::defaultKeymap();
    const auto bindings = keymap.bindings(Mode::Normal);
    std::string result = "<!-- Generated from transformSpecs and the default keymap; "
                         "see tools/manual-references.cmake. -->\n\n";
    for (const auto& spec : Workspace::transformSpecs())
    {
        const auto alias = spec.aliases.substr(0, spec.aliases.find(' '));
        const std::string command = ":" + std::string{alias.empty() ? spec.name : alias};
        result += "## " + prose(command) + " {#ref-" + std::string{spec.name} + "}\n\n";
        result += prose(spec.description) + ".\n\n";
        result += "**Syntax:** " +
                  code(command + (spec.usage.empty() ? "" : " ") + std::string{spec.usage}) +
                  ".\n\n";
        std::string keys;
        for (const auto& [sequence, binding] : bindings)
        {
            if (binding.command == spec.name)
            {
                if (!keys.empty())
                    keys += ", ";
                std::string strokes;
                for (const auto& key : sequence)
                {
                    if (!strokes.empty())
                        strokes += ' ';
                    strokes += key.named == NamedKey::Leader ? "," : formatKey(key);
                }
                keys += code(strokes);
            }
        }
        if (!keys.empty())
            result += "**Choose before a target:** " + keys + ".\n\n";
        if (!spec.settings.empty())
        {
            result += "| Parameter | Enter takes | Form or choices |\n| --- | --- | --- |\n";
            for (const auto& setting : spec.settings)
            {
                std::string choices{setting.form};
                if (choices.empty())
                {
                    for (const auto value : setting.values)
                    {
                        if (!choices.empty())
                            choices += ", ";
                        choices += value;
                    }
                }
                result += "| " + (setting.name.empty() ? "first argument" : code(setting.name)) +
                          " | " + answer(setting.answer) + " | " +
                          (choices.empty() ? "as prompted" : code(choices, true)) + " |\n";
            }
            result += '\n';
        }
        else if (spec.asks != Workspace::TransformSpec::Asks::Nothing)
        {
            result += "**Default:** " + answer(spec.answer) + ".";
            if (!spec.examples.empty())
                result += " **Examples of answers:** " + code(spec.examples) + ".";
            result += "\n\n";
        }
    }
    return result;
}

void checkOrWrite(std::string_view filename, const std::string& expected)
{
    const char* output = std::getenv("MODAL_MANUAL_REFERENCES");
    const auto directory = output ? std::filesystem::path{output}
                                  : std::filesystem::path{MODAL_MANUAL_SOURCE_DIR} / "reference";
    const auto path = directory / filename;
    if (output)
    {
        std::filesystem::create_directories(directory);
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        file << expected;
        REQUIRE(file.good());
    }
    else
    {
        std::ifstream file{path, std::ios::binary};
        INFO("Refresh registry references with tools/manual-references.cmake: " << path.string());
        REQUIRE(file.good());
        const std::string actual{std::istreambuf_iterator<char>{file}, {}};
        CHECK(actual == expected);
    }
}

} // namespace

TEST_CASE("the manual's settings reference matches the application", "[manual-reference]")
{
    checkOrWrite("settings.inc.qmd", settingsReference());
}

TEST_CASE("the manual's transform reference matches the application", "[manual-reference]")
{
    checkOrWrite("transforms.inc.qmd", transformsReference());
}
