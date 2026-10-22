// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/paths.hpp>

#include <cstdlib>

namespace modal::platform
{

std::optional<std::filesystem::path> configDirectory()
{
    // An empty or relative XDG_CONFIG_HOME is to be ignored, by the XDG spec.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] == '/')
    {
        return std::filesystem::path{xdg} / "modal";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
    {
        return std::filesystem::path{home} / ".config" / "modal";
    }
    return std::nullopt;
}

} // namespace modal::platform
