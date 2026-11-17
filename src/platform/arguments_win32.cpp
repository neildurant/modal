// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/arguments.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h, whose types it needs.
#include <shellapi.h>

namespace modal::platform
{

namespace
{

std::string utf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0'); // without the final NUL
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

std::vector<std::string> arguments(int argc, char* argv[])
{
    std::vector<std::string> result;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr)
    {
        // As main() has them, which is right for whatever the code page holds.
        for (int i = 1; i < argc; ++i)
        {
            result.emplace_back(argv[i]);
        }
        return result;
    }
    for (int i = 1; i < count; ++i)
    {
        result.push_back(utf8(wide[i]));
    }
    LocalFree(wide);
    return result;
}

} // namespace modal::platform
