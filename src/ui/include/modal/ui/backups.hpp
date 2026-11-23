// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace modal::ui
{

// Numbered backups of song files (decided 23 November 2026), kept in the state directory
// rather than beside the songs: backups/NAME-HASH/ for each song, NAME the file's name without
// its extension and HASH a short hash of its full path, holding 1.modal (the newest) up to the
// number kept, and a `song` file naming the song's path. Each backup keeps the time its version
// was last saved as its file time.

// The folder for a song's backups under the state directory. song is its full path.
std::filesystem::path backupFolder(const std::filesystem::path& state,
                                   const std::filesystem::path& song);

// Before a save replaces song: keeps the file as it is as backup 1, moving older ones up a
// number and keeping at most `most`. Does nothing if `most` is 0 or there's no file. Returns why
// no backup could be made, or an empty string.
std::string backUpSong(const std::filesystem::path& state, const std::filesystem::path& song,
                       std::size_t most);

struct SongBackup
{
    std::filesystem::path file;
    std::size_t number = 0; // 1 for the newest
    std::filesystem::file_time_type saved;
};

// A song's backups, newest first.
std::vector<SongBackup> songBackups(const std::filesystem::path& state,
                                    const std::filesystem::path& song);

} // namespace modal::ui
