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

} // namespace modal::platform
