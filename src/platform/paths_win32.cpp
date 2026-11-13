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
        directory = std::filesystem::path{folder};
    }
    CoTaskMemFree(folder); // even on failure
    return directory;
}

} // namespace

std::optional<std::filesystem::path> configDirectory()
{
    auto folder = knownFolder(FOLDERID_RoamingAppData);
    return folder ? std::optional{*folder / "Modal"} : std::nullopt;
}

std::optional<std::filesystem::path> stateDirectory()
{
    auto folder = knownFolder(FOLDERID_LocalAppData);
    return folder ? std::optional{*folder / "Modal"} : std::nullopt;
}

std::optional<std::filesystem::path> homeDirectory()
{
    return knownFolder(FOLDERID_Profile);
}

} // namespace modal::platform
