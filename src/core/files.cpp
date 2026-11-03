// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.hpp"

#include <cstdio>
#include <fstream>
#include <random>
#include <system_error>

namespace modal::core
{

namespace
{

// A path as UTF-8 text for a message, which converting to the system's code page could throw.
std::string textOf(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace

std::string writeFileAtomically(const std::filesystem::path& path,
                                std::span<const std::uint8_t> bytes)
{
    // A short name of its own beside the file, whatever the file's name, so it can't be one
    // already there or another save's, nor too long for the file system; and nothing that
    // throws, so a failure is an error to report (found in a code review, November 2026).
    std::random_device random;
    std::filesystem::path temporary;
    std::error_code error;
    for (int tries = 0; tries < 16; ++tries)
    {
        char name[32];
        std::snprintf(name, sizeof name, ".modal-%08x%08x.tmp", random(), random());
        temporary = path.parent_path() / name;
        if (!std::filesystem::exists(temporary, error) && !error)
        {
            break;
        }
    }
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return "can't write " + textOf(path);
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "can't write " + textOf(path) + ": " + error.message();
    }
    return {};
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path, std::uintmax_t maxSize,
                                   const std::string& what, std::string& error)
{
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError)
    {
        error = "can't read " + path.string() + ": " + sizeError.message();
        return {};
    }
    if (size > maxSize)
    {
        error = path.string() + " is too large to be " + what;
        return {};
    }
    std::ifstream stream{path, std::ios::binary};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        error = "can't read " + path.string();
        return {};
    }
    return bytes;
}

} // namespace modal::core
