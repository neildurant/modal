// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modal::ui
{

struct Colour
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(Colour, Colour) = default;
};

// What a colour is for. Views name roles, never colours; a theme gives each role a colour.
enum class Role : std::uint8_t
{
    Background, // the window, and anything without a background of its own
    Text,
    Dim,     // secondary text, such as labels and hints
    Accent,  // text to draw the eye
    Warning, // problems and errors
    Message, // a message in the status line, such as the new font size
    Cursor,  // the cursor's background
    CursorText,
    Selection, // the background of what's selected
    Playhead,  // the background of what's playing, such as the event list's row
    Bar,       // the background of the status line and other bars
    BarText,
    Panel,  // the background of the inspector, a shade apart from the bars
    Chrome, // the window around the view, and the gaps between bars
    Mode,   // the mode indicator's background, such as NORMAL
    ModeText,
    GridLine, // grid steps in a timeline
    BeatLine,
    BarLine,
    Note,
    Marker,
    Density1, // how busy a stretch of a track is, as in the arrangement, from sparse to dense
    Density2,
    Density3,
    Density4,
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::Density4) + 1;

// A role's name, as themes and highlight write it: lower case with hyphens, such as
// cursor-text or density-1; and the role of a name, if there is one.
std::string_view roleName(Role role);
std::optional<Role> roleFromName(std::string_view name);

// A colour written as #rrggbb, in either case; and the colour so written, in lower case.
std::optional<Colour> parseColour(std::string_view text);
std::string formatColour(Colour colour);

// Text attributes, which combine.
using Attributes = std::uint8_t;
inline constexpr Attributes kPlain = 0;
inline constexpr Attributes kBold = 1;
inline constexpr Attributes kUnderline = 2;
inline constexpr Attributes kReverse = 4; // foreground and background swapped

struct Style
{
    Role foreground = Role::Text;
    Role background = Role::Background;
    Attributes attributes = kPlain;

    friend bool operator==(const Style&, const Style&) = default;
};

// A colour for every role; black until set. The built-in themes are in themes.hpp.
class Theme
{
public:
    Colour operator[](Role role) const { return colours_[static_cast<std::size_t>(role)]; }
    void set(Role role, Colour colour) { colours_[static_cast<std::size_t>(role)] = colour; }

    // The colours a style's text and background are drawn in, with reverse applied.
    Colour foreground(const Style& style) const;
    Colour background(const Style& style) const;

    friend bool operator==(const Theme&, const Theme&) = default;

private:
    std::array<Colour, kRoleCount> colours_{};
};

} // namespace modal::ui
