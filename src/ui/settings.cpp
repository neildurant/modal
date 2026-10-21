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
          setting("undolevels", "How many changes can be undone",
                  std::int64_t{core::Document::kDefaultUndoLevels}, 0, 100000),
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
    else
    {
        setting->value = std::string{text};
    }
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

} // namespace modal::ui
