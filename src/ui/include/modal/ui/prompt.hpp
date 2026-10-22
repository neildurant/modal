#pragma once

#include <modal/ui/commands.hpp>
#include <modal/ui/config.hpp>

#include <filesystem>
#include <optional>
#include <span>

namespace modal::ui
{

// The : prompt's own commands, besides the config commands (see configCommands) and the file
// commands (see fileCommands): write-config.
std::span<const CommandInfo> promptCommands();

// Gives the registry what runs the config commands and the prompt's own, at the : prompt:
//
//   set, highlight, the map family, source   run by the reader, as in a command file
//   set NAME?...        shows settings' values, as in Vim
//   source              with no file, runs modalrc in the config directory again
//   write-config FILE   writes the config (see configText) to a file, relative to the config
//                       directory; write-config! overwrites it
//
// Errors, including those in files a command runs, come back as a warning: the first, and
// how many more. The registry, reader and config outlive the commands' use of them.
void addPromptCommands(CommandRegistry& registry, ConfigReader& reader, const Config& config,
                       std::optional<std::filesystem::path> configDirectory);

} // namespace modal::ui
