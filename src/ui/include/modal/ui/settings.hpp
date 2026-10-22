// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::ui
{

// A size in cells, written as 80x24.
struct CellCount
{
    int columns = 0;
    int rows = 0;

    friend bool operator==(CellCount, CellCount) = default;
};

// A setting's value: a whole number, a number, text, a size in cells, or on or off.
using SettingValue = std::variant<std::int64_t, double, std::string, CellCount, bool>;

// The value written as `set` reads it: 500, 10.5, dark, 80x24, on.
std::string formatValue(const SettingValue& value);

class Settings;

// The names of Modal's own MIDI output ports, as the ownports and ownportname settings ask:
// one is named ownportname alone, several are numbered from 1 after it.
std::vector<std::string> ownPortNames(const Settings& settings);

struct Setting
{
    std::string_view name;
    std::string_view description; // one line, for generated config and later for :set
    SettingValue defaultValue;
    SettingValue value;
    // The range of a number, or of each side of a size in cells.
    double minimum = 0;
    double maximum = 0;
};

// Every setting there is, each with its name, type, default and description, and its value.
// Names are lower case without hyphens, as Vim's options are (decided in milestone 3).
class Settings
{
public:
    Settings();

    // Sets a setting from text, returning why not if there's no such setting or the text
    // isn't a value for it, or else nothing; the value is unchanged then. An on-or-off
    // setting takes on or off.
    std::string set(std::string_view name, std::string_view value);

    // Sets an on-or-off setting from one word, as Vim does: NAME turns it on, noNAME off, and
    // NAME! the other way. Returns why not, or nothing.
    std::string setFlag(std::string_view word);

    // The setting of a name, or null if there's none.
    const Setting* find(std::string_view name) const;

    // All settings, in the order they're listed in generated config. Not of a temporary,
    // which would be gone before the span is used.
    std::span<const Setting> all() const& { return settings_; }
    std::span<const Setting> all() const&& = delete;

    // A setting's value, which must exist and be of the type asked for.
    std::int64_t integer(std::string_view name) const;
    double number(std::string_view name) const;
    const std::string& text(std::string_view name) const;
    CellCount cells(std::string_view name) const;
    bool flag(std::string_view name) const;

private:
    Setting* findSetting(std::string_view name);

    std::vector<Setting> settings_;
};

} // namespace modal::ui
