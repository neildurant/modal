// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/paths.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shlobj.h>
#include <windows.h>

namespace modal::platform
{

std::optional<std::filesystem::path> configDirectory()
{
    PWSTR roaming = nullptr;
    const HRESULT found = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
    std::optional<std::filesystem::path> directory;
    if (SUCCEEDED(found) && roaming != nullptr)
    {
        directory = std::filesystem::path{roaming} / "Modal";
    }
    CoTaskMemFree(roaming); // even on failure
    return directory;
}

} // namespace modal::platform
