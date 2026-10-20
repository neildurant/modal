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

// Writes a file by writing a temporary file beside it first and then renaming it, so a failed
// write never leaves half a file. Returns an error message, or an empty string.
std::string writeFileAtomically(const std::filesystem::path& path,
                                std::span<const std::uint8_t> bytes);

// Reads a whole file of at most `maxSize` bytes, or sets `error` (naming the file and saying
// it's larger than a `what` can be, if it's too large).
std::vector<std::uint8_t> readFile(const std::filesystem::path& path, std::uintmax_t maxSize,
                                   const std::string& what, std::string& error);

} // namespace modal::core
