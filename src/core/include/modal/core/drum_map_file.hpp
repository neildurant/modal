// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/drum_map.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace modal::core
{

// Reusable map copies: modal-drum-map 1, name "Kit", then 36 "Kick" and optional always-show 36.
// Quoting, escaping and whole-line comments follow the native song format.
struct DrumMapReadResult
{
    std::optional<DrumMap> map;
    std::string error;
};

std::string writeDrumMap(const DrumMap& map);
DrumMapReadResult readDrumMap(std::string_view text);

// Writes a temporary file beside the destination first, as song saves do. Choosing the
// configuration directory and asking about overwrites belong to the caller.
std::string writeDrumMapFile(const DrumMap& map, const std::filesystem::path& path);
DrumMapReadResult readDrumMapFile(const std::filesystem::path& path);

} // namespace modal::core
