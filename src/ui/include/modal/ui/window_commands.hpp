#pragma once

#include <modal/ui/keymap.hpp>

#include <span>

namespace modal::ui
{

// The commands of the window around the views, which the program runs itself: the text
// size, for now.
std::span<const CommandInfo> windowCommands();

} // namespace modal::ui
