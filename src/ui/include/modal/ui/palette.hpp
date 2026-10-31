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

// A command as the palette searches it: its name, title, description and other names, whether
// it does something where the cursor is, its keys, as typed (zh, <C-p>, ,?), and how highly it
// ranks by how often and how lately it's been run from the palette.
struct PaletteItem
{
    std::string name;
    std::string title;
    std::string description;
    std::string aliases;
    bool offered = true;
    std::vector<std::string> keys = {};
    double rank = 0;
};

// The items a query matches, by index, best first: a key that's exactly it (zh, J), then the
// title starting with it, then a word of the title or the name starting with it, then a key
// starting with it, then its letters in order in the title (vcomp, Velocity compress), then
// found in the description or other names. Keys match as typed, the rest ignoring case. Those
// that do nothing here come after the rest, and within each, the highest ranked first, then by
// title. With no query, every item.
std::vector<std::size_t> paletteMatches(std::span<const PaletteItem> items, std::string_view query);

} // namespace modal::ui
