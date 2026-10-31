// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The command palette (decided October 2026): a fuzzy search over the commands, for when you
// roughly know what you want but not where it lives or how it's bound.

// A command as the palette searches it: its name, title, description and other names, and
// whether it does something where the cursor is.
struct PaletteItem
{
    std::string name;
    std::string title;
    std::string description;
    std::string aliases;
    bool offered = true;
};

// The items a query matches, by index, best first: the title starting with it, then a word of
// the title or the name starting with it, then its letters in order in the title (vcomp,
// Velocity compress), then found in the description or other names; ignoring case; those that
// do nothing here after the rest, and within each, by title. With no query, every item.
std::vector<std::size_t> paletteMatches(std::span<const PaletteItem> items, std::string_view query);

} // namespace modal::ui
