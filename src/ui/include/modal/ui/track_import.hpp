// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::ui
{

// The tracks a selection names when importing tracks from a file (decided 20 November 2026):
// numbers counting from 1 and ranges, separated by commas or spaces (1, 3-5, 7), or all, or
// none. The tracks' indices from 0, in order and without repeats; or what's wrong, naming the
// part at fault.
std::variant<std::vector<std::size_t>, std::string> parseTrackSelection(std::string_view text,
                                                                        std::size_t count);

// A selection as the command line takes it, from tracks' indices from 0, in order: all for
// every one of count tracks, otherwise numbers and ranges, as 1,3-5,7.
std::string trackSelectionText(std::span<const std::size_t> chosen, std::size_t count);

// Whether text has a selection's shape, to tell it from the end of a filename on the command
// line.
bool looksLikeTrackSelection(std::string_view text);

} // namespace modal::ui
