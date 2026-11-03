// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace modal::test
{

// A directory of a test's own for its files, named at random so tests run at once, or by
// other builds, never share one, and removed afterwards (decided November 2026, after a code
// review).
class TempDirectory
{
public:
    explicit TempDirectory(std::string_view name)
    {
        std::random_device random;
        do
        {
            path_ = std::filesystem::temp_directory_path() /
                    (std::string{name} + "-" + std::to_string(random()));
        } while (std::filesystem::exists(path_));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

    // A path in it, its name in UTF-8.
    std::filesystem::path operator/(std::string_view name) const
    {
        return path_ / std::u8string{reinterpret_cast<const char8_t*>(name.data()), name.size()};
    }

    // Writes a file in it, and any folders on its way.
    std::filesystem::path write(std::string_view name, std::string_view text) const
    {
        const auto file = *this / name;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << text;
        return file;
    }

private:
    std::filesystem::path path_;
};

} // namespace modal::test
