// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/style.hpp>

namespace modal::ui
{

namespace
{

constexpr std::array<std::string_view, kRoleCount> kRoleNames{
    "background",  "text",      "dim",       "accent",    "warning",  "message", "cursor",
    "cursor-text", "selection", "playhead",  "bar",       "bar-text", "panel",   "chrome",
    "mode",        "mode-text", "grid-line", "beat-line", "bar-line", "note",    "marker",
    "density-1",   "density-2", "density-3", "density-4",
};

std::optional<int> hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return std::nullopt;
}

} // namespace

std::string_view roleName(Role role)
{
    return kRoleNames[static_cast<std::size_t>(role)];
}

std::optional<Role> roleFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kRoleCount; ++i)
    {
        if (kRoleNames[i] == name)
        {
            return static_cast<Role>(i);
        }
    }
    return std::nullopt;
}

std::optional<Colour> parseColour(std::string_view text)
{
    if (text.size() != 7 || text[0] != '#')
    {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto high = hexDigit(text[1 + 2 * i]);
        const auto low = hexDigit(text[2 + 2 * i]);
        if (!high || !low)
        {
            return std::nullopt;
        }
        parts[i] = static_cast<std::uint8_t>(*high * 16 + *low);
    }
    return Colour{parts[0], parts[1], parts[2]};
}

std::string formatColour(Colour colour)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text = "#";
    for (const std::uint8_t part : {colour.r, colour.g, colour.b})
    {
        text += kDigits[part >> 4];
        text += kDigits[part & 0xf];
    }
    return text;
}

Colour Theme::foreground(const Style& style) const
{
    return (*this)[(style.attributes & kReverse) != 0 ? style.background : style.foreground];
}

Colour Theme::background(const Style& style) const
{
    return (*this)[(style.attributes & kReverse) != 0 ? style.foreground : style.background];
}

} // namespace modal::ui
