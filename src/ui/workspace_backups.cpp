// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/backups.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/workspace.hpp>

#include <string>
#include <system_error>

namespace modal::ui
{

std::string Workspace::backUp(const std::filesystem::path& file) const
{
    const auto most = settings_ != nullptr ? settings_->integer("backups") : 5;
    if (most <= 0 || !stateFolder_)
        return {};
    // Keyed by the full path, as the recent list is, so a song reached by another route
    // shares its backups.
    std::error_code error;
    const auto absolute = std::filesystem::absolute(file, error);
    return backUpSong(*stateFolder_, (error ? file : absolute).lexically_normal(),
                      static_cast<std::size_t>(most));
}

} // namespace modal::ui
