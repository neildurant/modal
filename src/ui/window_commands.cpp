#include <modal/ui/window_commands.hpp>

#include <array>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 3> kWindowCommands{{
    {"font-bigger", "Make the text a point bigger"},
    {"font-smaller", "Make the text a point smaller"},
    {"font-reset", "Make the text the size the config sets"},
}};

} // namespace

std::span<const CommandInfo> windowCommands()
{
    return kWindowCommands;
}

} // namespace modal::ui
