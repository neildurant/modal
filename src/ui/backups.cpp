// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/backups.hpp>
#include <modal/ui/config.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

namespace modal::ui
{

namespace
{

constexpr std::string_view kExtension = ".modal";

// FNV-1a: small, and the same on every system, so a song's folder stays put between versions.
std::uint64_t hashOf(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto c : text)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::filesystem::path numbered(std::size_t number)
{
    return utf8Path(std::to_string(number) + std::string{kExtension});
}

// A backup's number from its file's name, or nothing for any other file.
std::optional<std::size_t> numberOf(const std::filesystem::path& file)
{
    const auto name = utf8Text(file.filename());
    if (name.size() <= kExtension.size() || !name.ends_with(kExtension) || name.front() == '0')
        return std::nullopt;
    const auto digits = std::string_view{name}.substr(0, name.size() - kExtension.size());
    std::size_t number = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error != std::errc{} || end != digits.data() + digits.size())
        return std::nullopt;
    return number;
}

std::vector<SongBackup> backupsIn(const std::filesystem::path& folder)
{
    std::vector<SongBackup> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it{folder, error}, end; !error && it != end;
         it.increment(error))
    {
        std::error_code ignored;
        if (const auto number = numberOf(it->path()); number && it->is_regular_file(ignored))
            found.push_back({it->path(), *number, it->last_write_time(ignored)});
    }
    std::ranges::sort(found, {}, &SongBackup::number);
    return found;
}

} // namespace

std::filesystem::path backupFolder(const std::filesystem::path& state,
                                   const std::filesystem::path& song)
{
    char hash[13];
    const auto text = utf8Text(song.lexically_normal());
    const auto value = hashOf(text) >> 16; // twelve hex digits are plenty for one person's songs
    for (int i = 11; i >= 0; --i)
        hash[11 - i] = "0123456789abcdef"[(value >> (4 * i)) & 0xF];
    hash[12] = '\0';
    return state / "backups" / utf8Path(utf8Text(song.stem()) + "-" + hash);
}

std::string backUpSong(const std::filesystem::path& state, const std::filesystem::path& song,
                       std::size_t most)
{
    std::error_code error;
    if (most == 0 || !std::filesystem::is_regular_file(song, error))
        return {};
    const auto folder = backupFolder(state, song);
    std::filesystem::create_directories(folder, error);
    if (error)
        return "can't make " + utf8Text(folder) + ": " + error.message();

    // Copied first, so a failed copy leaves the backups there as they were.
    const auto copy = folder / "new.tmp";
    std::filesystem::copy_file(song, copy, std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(copy, ignored);
        return "can't copy it: " + error.message();
    }
    {
        // When that version was saved, which a copy needn't keep.
        std::error_code ignored;
        const auto saved = std::filesystem::last_write_time(song, ignored);
        if (!ignored)
            std::filesystem::last_write_time(copy, saved, ignored);
    }

    auto backups = backupsIn(folder);
    for (auto it = backups.rbegin(); it != backups.rend(); ++it)
    {
        if (it->number >= most)
            std::filesystem::remove(it->file, error);
        else
            std::filesystem::rename(it->file, folder / numbered(it->number + 1), error);
        if (error)
            return "can't move the older backups on: " + error.message();
    }
    std::filesystem::rename(copy, folder / numbered(1), error);
    if (error)
        return "can't keep it: " + error.message();

    // Which song the folder is for, the hash alone not saying.
    std::ofstream{folder / "song", std::ios::binary | std::ios::trunc}
        << utf8Text(song.lexically_normal()) << '\n';
    return {};
}

std::vector<SongBackup> songBackups(const std::filesystem::path& state,
                                    const std::filesystem::path& song)
{
    return backupsIn(backupFolder(state, song));
}

} // namespace modal::ui
