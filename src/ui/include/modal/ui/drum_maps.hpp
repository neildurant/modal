// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/drum_map.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

struct SavedDrumMap
{
    std::optional<std::filesystem::path> file; // no file for the built-in General MIDI map
    std::optional<core::DrumMap> map;
    std::string error;

    std::string label() const;
};

// A snapshot of reusable files, read on opening a song or chooser and after a file action,
// rather than during drawing. Unreadable entries stay visible so their errors can be reviewed.
struct DrumMapLibrary
{
    std::vector<SavedDrumMap> maps;
    std::string error; // a problem listing the directory; missing directories are fine
};

DrumMapLibrary readDrumMapLibrary(const std::filesystem::path& folder);
bool sameDrumMapName(std::string_view a, std::string_view b);
std::vector<std::size_t> matchingDrumMaps(const core::DrumMap& map, const DrumMapLibrary& library);

struct DrumMapDifference
{
    std::optional<std::uint8_t> pitch; // no pitch for a difference in the map's own name
    std::string song;
    std::string saved;
};
std::vector<DrumMapDifference> drumMapDifferences(const core::DrumMap& song,
                                                  const core::DrumMap& saved);

} // namespace modal::ui
