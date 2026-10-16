#pragma once

#include <modal/core/song.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

struct SmfImportResult
{
    std::optional<Song> song;          // empty if the file couldn't be read
    std::string error;                 // why, when there's no song
    std::vector<std::string> warnings; // problems worked around, e.g. a clamped tempo
};

// Imports a Standard MIDI File, type 0 or 1, at the file's own resolution, rescaled to kPpqn.
//
// - Note-ons and note-offs pair up (first on, first off) into notes with durations; a
//   note-on with velocity 0 is a note-off. Notes still sounding at the end of their track
//   end there.
// - Each file track becomes one track per MIDI channel it uses. Its SysEx and text go to the
//   first of those, or to a track of their own if it has no channel events.
// - Tempo changes and markers from any track go to the song; time signatures from any track
//   apply to every track. The first track's name is the song title in a type 0 file, and in
//   a type 1 file whose first track has no channel events (the usual conductor track).
// - Tempos outside 1-1000 BPM are clamped.
SmfImportResult importSmf(std::span<const std::uint8_t> bytes);

SmfImportResult importSmfFile(const std::filesystem::path& path);

} // namespace modal::core
