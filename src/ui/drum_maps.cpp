// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/drum_map_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/drum_maps.hpp>

#include <algorithm>
#include <system_error>
#include <utility>

namespace modal::ui
{

bool sameDrumMapName(std::string_view a, std::string_view b)
{
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; };
    return a.size() == b.size() && std::ranges::equal(a, b, {}, lower, lower);
}

std::string SavedDrumMap::label() const
{
    return file ? utf8Text(file->filename()) : "Built-in General MIDI";
}

DrumMapLibrary readDrumMapLibrary(const std::filesystem::path& folder)
{
    DrumMapLibrary library{{{std::nullopt, core::generalMidiDrumMap(), {}}}, {}};
    std::error_code error;
    const bool exists = std::filesystem::exists(folder, error);
    if (!error && !exists)
        return library;
    std::vector<std::filesystem::path> files;
    auto entry = error ? std::filesystem::directory_iterator{}
                       : std::filesystem::directory_iterator{folder, error};
    for (; !error && entry != std::filesystem::directory_iterator{}; entry.increment(error))
    {
        if (sameDrumMapName(utf8Text(entry->path().extension()), ".drum-map") &&
            entry->is_regular_file(error))
            files.push_back(entry->path());
    }
    if (error)
        library.error = "Can't list drum maps in " + utf8Text(folder) + ": " + error.message();
    std::ranges::sort(files);
    for (const auto& file : files)
    {
        auto read = core::readDrumMapFile(file);
        library.maps.push_back({file, std::move(read.map), std::move(read.error)});
    }
    return library;
}

std::vector<std::size_t> matchingDrumMaps(const core::DrumMap& map, const DrumMapLibrary& library)
{
    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < library.maps.size(); ++i)
        if (library.maps[i].map && sameDrumMapName(map.name, library.maps[i].map->name))
            matches.push_back(i);
    return matches;
}

std::vector<DrumMapDifference> drumMapDifferences(const core::DrumMap& song,
                                                  const core::DrumMap& saved)
{
    std::vector<DrumMapDifference> changes;
    if (song.name != saved.name)
        changes.push_back({std::nullopt, song.name, saved.name});
    for (std::size_t pitch = 0; pitch < song.notes.size(); ++pitch)
        if (song.notes[pitch] != saved.notes[pitch])
            changes.push_back(
                {static_cast<std::uint8_t>(pitch), song.notes[pitch], saved.notes[pitch]});
    return changes;
}

} // namespace modal::ui
