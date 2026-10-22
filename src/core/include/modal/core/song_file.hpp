// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace modal::core
{

// The native song format: plain text to read, diff and keep in version control, one event a
// line, grouped by section, with every position relative to its section in the bars that
// apply there (see docs/design.md, File formats).
//
//   modal-song 1
//   song "Untitled" ppqn=960
//   track 1 "Bass" ch=1 port="Pro-800"
//
//   section "Intro" bars=4
//     meter 1:1:0 4/4
//     tempo 1:1:0 120
//     marker 3:1:0 "fix this fill"
//     track 1
//       1:1:0 note E1 v=96 len=1/8

struct SongReadResult
{
    std::optional<Song> song; // none if the file couldn't be read
    std::string error;        // why, naming the line, when there's no song
};

// Writes a song in the native format.
std::string writeSong(const Song& song);

// Reads a song at the given resolution; a file at another is rescaled. Anything wrong in the
// file stops it being read, with the line given, so nothing is lost without a word.
SongReadResult readSong(std::string_view text, Ppqn ppqn = kPpqn);

// Writes a song to a file, writing a temporary file beside it first and then renaming it, so
// a failed save never leaves half a file. Returns an error message, or an empty string.
std::string writeSongFile(const Song& song, const std::filesystem::path& path);

SongReadResult readSongFile(const std::filesystem::path& path, Ppqn ppqn = kPpqn);

} // namespace modal::core
