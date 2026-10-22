// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace modal::ui
{

// Keys without a character of their own.
enum class NamedKey : std::uint8_t
{
    None, // a character key
    Escape,
    Tab,
    Enter,
    Backspace,
    Delete,
    Insert,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    // The numeric keypad's, with Ctrl or Alt; without, they type their characters.
    KeypadPlus,
    KeypadMinus,
    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
};

// Modifier keys held with a key, which combine.
using Modifiers = std::uint8_t;
inline constexpr Modifiers kNoModifiers = 0;
inline constexpr Modifiers kCtrl = 1;
inline constexpr Modifiers kAlt = 2;
inline constexpr Modifiers kShift = 4;

// A key press, as the views see it. A character key is the character it types in the
// keyboard's layout, so Shift-4 on a US keyboard is '$' with no modifiers; Shift shows only
// on named keys and with Ctrl or Alt. With Ctrl or Alt, the character is the key's own,
// unshifted: Ctrl-d is 'd' with kCtrl.
struct Key
{
    char32_t character = 0; // 0 for a named key
    NamedKey named = NamedKey::None;
    Modifiers modifiers = kNoModifiers;

    static constexpr Key typed(char32_t character, Modifiers modifiers = kNoModifiers)
    {
        return {character, NamedKey::None, modifiers};
    }

    static constexpr Key of(NamedKey named, Modifiers modifiers = kNoModifiers)
    {
        return {0, named, modifiers};
    }

    friend constexpr bool operator==(const Key&, const Key&) = default;
};

} // namespace modal::ui
