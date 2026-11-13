// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

namespace modal::platform
{

// The directory for the user's configuration, such as modalrc and themes: on Linux
// $XDG_CONFIG_HOME/modal, or ~/.config/modal; on Windows %APPDATA%\Modal. Nothing if the
// system doesn't say where. It may not exist yet.
std::optional<std::filesystem::path> configDirectory();

// The directory for what Modal remembers between sessions without being told, such as the
// palette's ranking (decided October 2026), apart from what the user writes: on Linux
// $XDG_STATE_HOME/modal, or ~/.local/state/modal; on Windows %LOCALAPPDATA%\Modal. Nothing
// if the system doesn't say where. It may not exist yet.
std::optional<std::filesystem::path> stateDirectory();

// The user's home directory, which ~ stands for in a path typed: on Linux $HOME; on Windows
// the user's profile folder. Nothing if the system doesn't say where.
std::optional<std::filesystem::path> homeDirectory();

} // namespace modal::platform
