#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
    Cursor,  // the cursor's background
    CursorText,
    Selection, // the background of what's selected
    Bar,       // the background of the status line and other bars
    BarText,
    Mode, // the mode indicator's background, such as NORMAL
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

// A colour for every role.
class Theme
{
public:
    // The built-in dark theme, until themes can be loaded.
    static Theme dark();

    Colour operator[](Role role) const { return colours_[static_cast<std::size_t>(role)]; }
    void set(Role role, Colour colour) { colours_[static_cast<std::size_t>(role)] = colour; }

    // The colours a style's text and background are drawn in, with reverse applied.
    Colour foreground(const Style& style) const;
    Colour background(const Style& style) const;

private:
    std::array<Colour, kRoleCount> colours_{};
};

} // namespace modal::ui
