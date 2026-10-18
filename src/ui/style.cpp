#include <modal/ui/style.hpp>

namespace modal::ui
{

Theme Theme::dark()
{
    Theme theme;
    theme.set(Role::Background, {0x1a, 0x1b, 0x26});
    theme.set(Role::Text, {0xc0, 0xca, 0xf5});
    theme.set(Role::Dim, {0x56, 0x5f, 0x89});
    theme.set(Role::Accent, {0x7a, 0xa2, 0xf7});
    theme.set(Role::Warning, {0xf7, 0x76, 0x8e});
    theme.set(Role::Cursor, {0xe0, 0xaf, 0x68});
    theme.set(Role::CursorText, {0x1a, 0x1b, 0x26});
    theme.set(Role::Selection, {0x2e, 0x3c, 0x64});
    theme.set(Role::Bar, {0x24, 0x28, 0x3b});
    theme.set(Role::BarText, {0xa9, 0xb1, 0xd6});
    theme.set(Role::Mode, {0x7a, 0xa2, 0xf7});
    theme.set(Role::ModeText, {0x1a, 0x1b, 0x26});
    theme.set(Role::GridLine, {0x24, 0x28, 0x3b});
    theme.set(Role::BeatLine, {0x34, 0x3a, 0x55});
    theme.set(Role::BarLine, {0x56, 0x5f, 0x89});
    theme.set(Role::Note, {0x9e, 0xce, 0x6a});
    theme.set(Role::Marker, {0xff, 0x9e, 0x64});
    return theme;
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
