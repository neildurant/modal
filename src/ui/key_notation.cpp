// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/frame.hpp>
#include <modal/ui/key_notation.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace modal::ui
{

namespace
{

struct KeyName
{
    std::string_view name;
    NamedKey key;
};

// The names keys are written with, first, then other names read for them.
constexpr std::array<KeyName, 44> kKeyNames{{
    {"leader", NamedKey::Leader},
    {"Esc", NamedKey::Escape},
    {"Tab", NamedKey::Tab},
    {"CR", NamedKey::Enter},
    {"BS", NamedKey::Backspace},
    {"Del", NamedKey::Delete},
    {"Insert", NamedKey::Insert},
    {"Up", NamedKey::Up},
    {"Down", NamedKey::Down},
    {"Left", NamedKey::Left},
    {"Right", NamedKey::Right},
    {"Home", NamedKey::Home},
    {"End", NamedKey::End},
    {"PageUp", NamedKey::PageUp},
    {"PageDown", NamedKey::PageDown},
    {"F1", NamedKey::F1},
    {"F2", NamedKey::F2},
    {"F3", NamedKey::F3},
    {"F4", NamedKey::F4},
    {"F5", NamedKey::F5},
    {"F6", NamedKey::F6},
    {"F7", NamedKey::F7},
    {"F8", NamedKey::F8},
    {"F9", NamedKey::F9},
    {"F10", NamedKey::F10},
    {"F11", NamedKey::F11},
    {"F12", NamedKey::F12},
    {"kPlus", NamedKey::KeypadPlus},
    {"kMinus", NamedKey::KeypadMinus},
    {"k0", NamedKey::Keypad0},
    {"k1", NamedKey::Keypad1},
    {"k2", NamedKey::Keypad2},
    {"k3", NamedKey::Keypad3},
    {"k4", NamedKey::Keypad4},
    {"k5", NamedKey::Keypad5},
    {"k6", NamedKey::Keypad6},
    {"k7", NamedKey::Keypad7},
    {"k8", NamedKey::Keypad8},
    {"k9", NamedKey::Keypad9},
    {"Escape", NamedKey::Escape},
    {"Enter", NamedKey::Enter},
    {"Return", NamedKey::Enter},
    {"Ins", NamedKey::Insert},
    {"Delete", NamedKey::Delete},
}};

char32_t lower(char32_t c)
{
    return c >= U'A' && c <= U'Z' ? c - U'A' + U'a' : c;
}

char32_t upper(char32_t c)
{
    return c >= U'a' && c <= U'z' ? c - U'a' + U'A' : c;
}

bool isLetter(char32_t c)
{
    return lower(c) >= U'a' && lower(c) <= U'z';
}

bool sameName(std::u32string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (lower(a[i]) != lower(static_cast<char32_t>(static_cast<unsigned char>(b[i]))))
        {
            return false;
        }
    }
    return true;
}

// The key written between angle brackets, or why it can't be read.
std::variant<Key, std::string> parseBracketed(std::u32string_view token)
{
    const auto whole = "<" + encodeUtf8(token) + ">";
    Modifiers modifiers = kNoModifiers;
    while (token.size() > 2 && token[1] == U'-')
    {
        switch (lower(token[0]))
        {
        case U'c':
            modifiers |= kCtrl;
            break;
        case U'a':
        case U'm':
            modifiers |= kAlt;
            break;
        case U's':
            modifiers |= kShift;
            break;
        default:
            return "unknown modifier in " + whole;
        }
        token.remove_prefix(2);
    }

    std::optional<char32_t> character;
    if (token.size() == 1)
    {
        character = token[0];
    }
    else if (sameName(token, "lt"))
    {
        character = U'<';
    }
    else if (sameName(token, "Space"))
    {
        character = U' ';
    }
    if (!character)
    {
        for (const auto& name : kKeyNames)
        {
            if (sameName(token, name.name))
            {
                return Key::of(name.key, modifiers);
            }
        }
        return "unknown key " + whole;
    }

    const char32_t c = *character;
    if ((modifiers & (kCtrl | kAlt)) != 0)
    {
        return Key::typed(isLetter(c) ? lower(c) : c, modifiers);
    }
    if (modifiers == kShift)
    {
        if (c == U' ')
        {
            return Key::typed(c, modifiers); // Shift-Space, a key of its own: the same space
        }
        if (!isLetter(c))
        {
            return "Shift with " + encodeUtf8(std::u32string(1, c)) +
                   " is written as the character it types, in " + whole;
        }
        return Key::typed(upper(c));
    }
    return Key::typed(c);
}

std::string modifierText(Modifiers modifiers)
{
    std::string text;
    text += (modifiers & kCtrl) != 0 ? "C-" : "";
    text += (modifiers & kAlt) != 0 ? "A-" : "";
    text += (modifiers & kShift) != 0 ? "S-" : "";
    return text;
}

} // namespace

std::variant<std::vector<Key>, std::string> parseKeys(std::string_view text)
{
    const auto characters = decodeUtf8(text);
    const std::u32string_view rest{characters};
    std::vector<Key> keys;
    for (std::size_t i = 0; i < rest.size(); ++i)
    {
        if (rest[i] != U'<')
        {
            keys.push_back(Key::typed(rest[i]));
            continue;
        }
        const auto close = rest.find(U'>', i + 2); // so <>> is >
        if (close == std::u32string_view::npos)
        {
            return std::string{"no closing > after <; write < as <lt>"};
        }
        auto key = parseBracketed(rest.substr(i + 1, close - i - 1));
        if (auto* error = std::get_if<std::string>(&key))
        {
            return std::move(*error);
        }
        keys.push_back(std::get<Key>(key));
        i = close;
    }
    if (keys.empty())
    {
        return std::string{"no keys"};
    }
    return keys;
}

std::string formatKey(const Key& key)
{
    if (key.named != NamedKey::None)
    {
        for (const auto& name : kKeyNames)
        {
            if (name.key == key.named)
            {
                return "<" + modifierText(key.modifiers) + std::string{name.name} + ">";
            }
        }
        return "<?>";
    }
    std::string character;
    if (key.character == U'<')
    {
        character = "lt";
    }
    else if (key.character == U' ')
    {
        character = "Space";
    }
    else
    {
        character = encodeUtf8(std::u32string(1, key.character));
    }
    const bool bracketed =
        key.modifiers != kNoModifiers || key.character == U'<' || key.character == U' ';
    return bracketed ? "<" + modifierText(key.modifiers) + character + ">" : character;
}

std::string formatKeys(const std::vector<Key>& keys)
{
    std::string text;
    for (const auto& key : keys)
    {
        text += formatKey(key);
    }
    return text;
}

} // namespace modal::ui
