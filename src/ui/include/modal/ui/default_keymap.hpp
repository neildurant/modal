#pragma once

#include <modal/ui/keymap.hpp>

#include <span>
#include <string_view>

namespace modal::ui
{

// A built-in key binding, in notation. Until command files can be read (milestone 3, step
// 6), the defaults are this table; then they become the built-in command file.
struct DefaultBinding
{
    Mode mode;
    std::string_view keys;
    std::string_view command;
    std::string_view hint;
};

std::span<const DefaultBinding> defaultBindings();

// A keymap of the default bindings.
Keymap defaultKeymap();

} // namespace modal::ui
