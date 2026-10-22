// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/window_commands.hpp>

#include <array>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 3> kWindowCommands{{
    {.name = "font-bigger", .description = "Make the text a point bigger", .count = true},
    {.name = "font-smaller", .description = "Make the text a point smaller", .count = true},
    {.name = "font-reset", .description = "Make the text the size the config sets"},
}};

} // namespace

std::span<const CommandInfo> windowCommands()
{
    return kWindowCommands;
}

} // namespace modal::ui
