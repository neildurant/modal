// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Reading and writing whole files, for the core's file formats. Private to the core library.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

// A path as UTF-8 text for a message, as the rest of Modal's text is. Converting to the
// system's code page instead, as path::string() does on Windows, can throw, or give text
// that isn't UTF-8.
std::string textOf(const std::filesystem::path& path);

// Writes a file by writing a temporary file beside it first, named at random so it's no file
// there already, and then renaming it, so a failed write never leaves half a file. Returns an
// error message, or an empty string.
std::string writeFileAtomically(const std::filesystem::path& path,
                                std::span<const std::uint8_t> bytes);

// Reads a whole file of at most `maxSize` bytes, or sets `error` (naming the file and saying
// it's larger than a `what` can be, if it's too large).
std::vector<std::uint8_t> readFile(const std::filesystem::path& path, std::uintmax_t maxSize,
                                   const std::string& what, std::string& error);

} // namespace modal::core
