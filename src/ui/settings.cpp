// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/settings.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <optional>
#include <system_error>

namespace modal::ui
{

namespace
{

std::string numberText(double value)
{
    char text[32];
    const auto result = std::to_chars(std::begin(text), std::end(text), value);
    return {std::begin(text), result.ptr};
}

template <typename T>
std::optional<T> parse(std::string_view text)
{
    T value{};
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

Setting setting(std::string_view name, std::string_view description, SettingValue value,
                double minimum = 0, double maximum = 0)
{
    return {name, description, value, value, minimum, maximum};
}

} // namespace

std::string formatValue(const SettingValue& value)
{
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        return std::to_string(*integer);
    }
    if (const auto* real = std::get_if<double>(&value))
    {
        return numberText(*real);
    }
    if (const auto* cells = std::get_if<CellCount>(&value))
    {
        return std::to_string(cells->columns) + "x" + std::to_string(cells->rows);
    }
    if (const auto* on = std::get_if<bool>(&value))
    {
        return *on ? "on" : "off";
    }
    return std::get<std::string>(value);
}

Settings::Settings()
    : settings_{
          setting("theme",
                  "The colour theme: themes/NAME in the config directory, or a built-in "
                  "theme, dark or light",
                  std::string{"dark"}),
          setting("font",
                  "A TrueType or OpenType font file for the grid, or empty for the "
                  "built-in JetBrains Mono",
                  std::string{}),
          setting("fontsize", "The font's size in points, before the display's scaling", 12.0, 4,
                  72),
          setting("minsize", "The smallest the window can be made, in cells", CellCount{120, 30},
                  20, 1000),
          setting("timeoutlen",
                  "How long to wait, in milliseconds, after keys that are a binding and also "
                  "start a longer one, before running the shorter",
                  kDefaultTimeout, 0, 10000),
          setting("chordwindow",
                  "How far apart, in milliseconds, events can start and still be one chord, "
                  "as a chord's notes played a little apart are",
                  std::int64_t{30}, 0, 1000),
          setting("gridadvance",
                  "Whether O in the piano roll moves the cursor on by the grid step (on) or by "
                  "the note's length (off)",
                  false),
          setting("nudge",
                  "How far, in milliseconds, a nudge moves a note in the piano roll, or "
                  "lengthens or shortens it",
                  std::int64_t{5}, 1, 1000),
          setting("undolevels", "How many changes can be undone",
                  std::int64_t{core::Document::kDefaultUndoLevels}, 0, 100000),
          setting("ownports",
                  "How many MIDI output ports of its own Modal makes, for other programs to "
                  "connect to (where the system lets it: Linux, not Windows)",
                  std::int64_t{1}, 0, 16),
          setting("ownportname", "The name of Modal's own ports; several are numbered after it",
                  std::string{"Modal"}),
          setting("port",
                  "The output port for tracks without one: its name, or part of it, or empty "
                  "for the first port found",
                  std::string{}),
          setting("rewind",
                  "Whether the play-from marker, which Space plays from, stays put when "
                  "playback stops (on), or moves to where it stopped (off)",
                  true),
      }
{
}

const Setting* Settings::find(std::string_view name) const
{
    const auto it = std::ranges::find(settings_, name, &Setting::name);
    return it != settings_.end() ? &*it : nullptr;
}

Setting* Settings::findSetting(std::string_view name)
{
    const auto it = std::ranges::find(settings_, name, &Setting::name);
    return it != settings_.end() ? &*it : nullptr;
}

std::string Settings::set(std::string_view name, std::string_view text)
{
    Setting* setting = findSetting(name);
    if (setting == nullptr)
    {
        return "unknown setting \"" + std::string{name} + "\"";
    }
    const std::string range = numberText(setting->minimum) + " to " + numberText(setting->maximum);
    const auto inRange = [&](double value)
    { return value >= setting->minimum && value <= setting->maximum; };

    if (std::holds_alternative<std::int64_t>(setting->value))
    {
        const auto value = parse<std::int64_t>(text);
        if (!value || !inRange(static_cast<double>(*value)))
        {
            return std::string{name} + " must be a whole number from " + range;
        }
        setting->value = *value;
    }
    else if (std::holds_alternative<double>(setting->value))
    {
        const auto value = parse<double>(text);
        if (!value || !std::isfinite(*value) || !inRange(*value))
        {
            return std::string{name} + " must be a number from " + range;
        }
        setting->value = *value;
    }
    else if (std::holds_alternative<CellCount>(setting->value))
    {
        const auto x = text.find('x');
        const auto columns = parse<int>(text.substr(0, x));
        const auto rows =
            x == std::string_view::npos ? std::nullopt : parse<int>(text.substr(x + 1));
        if (!columns || !rows || !inRange(*columns) || !inRange(*rows))
        {
            return std::string{name} + " must be columns x rows, such as 80x24, each from " + range;
        }
        setting->value = CellCount{*columns, *rows};
    }
    else if (std::holds_alternative<bool>(setting->value))
    {
        if (text != "on" && text != "off")
        {
            return std::string{name} + " must be on or off";
        }
        setting->value = text == "on";
    }
    else
    {
        setting->value = std::string{text};
    }
    return {};
}

std::string Settings::setFlag(std::string_view word)
{
    const bool toggle = word.ends_with('!');
    auto name = word.substr(0, word.size() - (toggle ? 1 : 0));
    const bool off = !toggle && findSetting(name) == nullptr && name.starts_with("no");
    name.remove_prefix(off ? 2 : 0);
    Setting* setting = findSetting(name);
    if (setting == nullptr)
    {
        return "unknown setting \"" + std::string{name} + "\"";
    }
    auto* on = std::get_if<bool>(&setting->value);
    if (on == nullptr)
    {
        return "set needs NAME=VALUE, not " + std::string{word};
    }
    *on = toggle ? !*on : !off;
    return {};
}

std::int64_t Settings::integer(std::string_view name) const
{
    assert(find(name) != nullptr);
    return std::get<std::int64_t>(find(name)->value);
}

double Settings::number(std::string_view name) const
{
    assert(find(name) != nullptr);
    return std::get<double>(find(name)->value);
}

const std::string& Settings::text(std::string_view name) const
{
    assert(find(name) != nullptr);
    return std::get<std::string>(find(name)->value);
}

CellCount Settings::cells(std::string_view name) const
{
    assert(find(name) != nullptr);
    return std::get<CellCount>(find(name)->value);
}

std::vector<std::string> ownPortNames(const Settings& settings)
{
    const auto count = settings.integer("ownports");
    const auto& name = settings.text("ownportname");
    std::vector<std::string> names;
    for (std::int64_t i = 1; i <= count && !name.empty(); ++i)
    {
        names.push_back(count == 1 ? name : name + " " + std::to_string(i));
    }
    return names;
}

bool Settings::flag(std::string_view name) const
{
    assert(find(name) != nullptr);
    return std::get<bool>(find(name)->value);
}

} // namespace modal::ui
