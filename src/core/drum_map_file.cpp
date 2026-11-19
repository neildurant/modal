// SPDX-License-Identifier: GPL-3.0-or-later

#include "text_format.hpp"

#include <modal/core/drum_map_file.hpp>
#include <modal/core/files.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace modal::core
{

std::string writeDrumMap(const DrumMap& map)
{
    std::string text = "modal-drum-map 1\nname " + textFormat::quotedText(map.name) + "\n";
    for (std::size_t note = 0; note < map.notes.size(); ++note)
    {
        if (!map.notes[note].empty())
        {
            text += std::to_string(note) + " " + textFormat::quotedText(map.notes[note]) + "\n";
        }
    }
    return text;
}

DrumMapReadResult readDrumMap(std::string_view text)
{
    DrumMap map;
    enum class Stage
    {
        Version,
        Name,
        Notes
    };
    auto stage = Stage::Version;
    std::size_t lineNumber = 0;
    const auto fail = [&](const std::string& message) -> DrumMapReadResult
    { return {std::nullopt, "line " + std::to_string(lineNumber) + ": " + message}; };
    std::vector<textFormat::Token> tokens;
    while (!text.empty())
    {
        ++lineNumber;
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '"')
        {
            continue;
        }
        tokens.clear();
        if (const auto error = textFormat::tokenize(line, tokens); !error.empty())
        {
            return fail(error);
        }
        if (stage == Stage::Version)
        {
            if (tokens.size() != 2 || tokens[0].text != "modal-drum-map" || tokens[1].text != "1")
            {
                return fail("not a supported drum map: it should start with modal-drum-map 1");
            }
            stage = Stage::Name;
        }
        else if (stage == Stage::Name)
        {
            if (tokens.size() != 2 || tokens[0].text != "name" || !tokens[1].quoted)
            {
                return fail("a map needs name followed by its name in quotes");
            }
            map.name = tokens[1].text;
            stage = Stage::Notes;
        }
        else
        {
            if (tokens.size() != 2 || !tokens[1].quoted || tokens[1].text.empty())
            {
                return fail("an entry needs a note number and a non-empty name in quotes");
            }
            const auto note = textFormat::parseInteger(tokens[0].text, 0, 127);
            if (!note)
            {
                return fail("a note number should be from 0 to 127");
            }
            auto& name = map.notes[static_cast<std::size_t>(*note)];
            if (!name.empty())
            {
                return fail("there's already a name for note " + tokens[0].text);
            }
            name = tokens[1].text;
        }
    }
    if (stage != Stage::Notes)
    {
        ++lineNumber;
        return fail(stage == Stage::Version ? "not a drum map: it's empty"
                                            : "the map has no name line");
    }
    return {std::move(map), {}};
}

std::string writeDrumMapFile(const DrumMap& map, const std::filesystem::path& path)
{
    const auto text = writeDrumMap(map);
    return writeFileAtomically(path,
                               {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

DrumMapReadResult readDrumMapFile(const std::filesystem::path& path)
{
    DrumMapReadResult result;
    constexpr std::uintmax_t kMaxFileSize = 1024u * 1024;
    const auto bytes = readFile(path, kMaxFileSize, "a drum map", result.error);
    if (!result.error.empty())
    {
        return result;
    }
    result = readDrumMap({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    if (!result.error.empty())
    {
        result.error = textOf(path) + ", " + result.error;
    }
    return result;
}

} // namespace modal::core
