// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/settings.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// Completion at the : prompt (decided October 2026): choices for the word under the cursor,
// from the kind of thing the command's argument there is, each kind with one source of its
// choices, so the prompt and later the palette and pick lists offer the same.

// The choices among some that a word typed matches, in their order: those starting with it,
// or if none do, those with a word starting with it (del finds bar-delete; words are split at
// - _ space / and .), ignoring case either way.
std::vector<std::string> matching(std::span<const std::string> choices, std::string_view typed);

// How far some choices agree from their start, ignoring case, as the first of them has it.
std::string sharedStart(std::span<const std::string> choices);

// The word a cursor is in or just after, in a line: where it starts and ends, and which word of
// the line it is, the command's name being 0.
struct LineWord
{
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t index = 0;
};
LineWord wordAt(std::u32string_view line, std::size_t cursor);

// The choices for a word that's a file or folder, in a folder: those in the folder the word
// names (songs/ in songs/ve), each with that folder before it and a / after a folder; files
// starting with a dot only if the word's name does.
std::vector<std::string> fileChoices(const std::filesystem::path& folder, std::string_view word);

// The choices for a word of set: each setting's name, NAME= for those with values, or NAME and
// noNAME for those on or off; and after NAME=, the setting's values, its themes for theme, on
// and off, or else its value now.
std::vector<std::string> settingChoices(const Settings& settings, std::string_view word,
                                        std::span<const std::string> themes);

} // namespace modal::ui
