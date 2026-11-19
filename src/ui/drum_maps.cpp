// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/drum_map_file.hpp>
#include <modal/ui/completion.hpp>
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

std::vector<std::uint8_t> matchingDrumPitches(const core::DrumMap& map, std::string_view name)
{
    std::vector<std::uint8_t> pitches;
    if (name.empty())
        return pitches;
    std::vector<std::string> names;
    for (std::size_t i = 0; i < map.notes.size(); ++i)
    {
        if (sameDrumMapName(map.notes[i], name))
            pitches.push_back(static_cast<std::uint8_t>(i));
        else if (!map.notes[i].empty())
            names.push_back(map.notes[i]);
    }
    if (!pitches.empty())
        return pitches;
    const auto found = matching(names, name);
    for (std::size_t i = 0; i < map.notes.size(); ++i)
    {
        if (std::ranges::find(found, map.notes[i]) != found.end())
            pitches.push_back(static_cast<std::uint8_t>(i));
    }
    return pitches;
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
    const auto label = [](const core::DrumMap& map, std::size_t pitch)
    {
        auto name = map.notes[pitch];
        if (map.alwaysShow[pitch])
            name += name.empty() ? "Always show" : " · Always show";
        return name;
    };
    for (std::size_t pitch = 0; pitch < song.notes.size(); ++pitch)
        if (song.notes[pitch] != saved.notes[pitch] ||
            song.alwaysShow[pitch] != saved.alwaysShow[pitch])
            changes.push_back(
                {static_cast<std::uint8_t>(pitch), label(song, pitch), label(saved, pitch)});
    return changes;
}

} // namespace modal::ui
