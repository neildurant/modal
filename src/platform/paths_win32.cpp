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

namespace
{

std::optional<std::filesystem::path> knownFolder(const KNOWNFOLDERID& id)
{
    PWSTR folder = nullptr;
    const HRESULT found = SHGetKnownFolderPath(id, 0, nullptr, &folder);
    std::optional<std::filesystem::path> directory;
    if (SUCCEEDED(found) && folder != nullptr)
    {
        directory = std::filesystem::path{folder} / "Modal";
    }
    CoTaskMemFree(folder); // even on failure
    return directory;
}

} // namespace

std::optional<std::filesystem::path> configDirectory()
{
    return knownFolder(FOLDERID_RoamingAppData);
}

std::optional<std::filesystem::path> stateDirectory()
{
    return knownFolder(FOLDERID_LocalAppData);
}

} // namespace modal::platform
