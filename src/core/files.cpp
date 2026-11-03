// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.hpp"

#include <cstdio>
#include <fstream>
#include <random>
#include <system_error>

namespace modal::core
{

std::string writeFileAtomically(const std::filesystem::path& path,
                                std::span<const std::uint8_t> bytes)
{
    // A name of its own beside the file, so it can't be one already there or another save's.
    std::random_device random;
    std::filesystem::path temporary;
    do
    {
        char tag[24];
        std::snprintf(tag, sizeof tag, ".%08x%08x.tmp", random(), random());
        temporary = path;
        temporary += tag;
    } while (std::filesystem::exists(temporary));
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return "can't write " + temporary.string();
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "can't write " + path.string() + ": " + error.message();
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
