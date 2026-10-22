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
// - Tempo changes and time signatures from any track go to the conductor, which every track
//   follows, and markers from any track, and cue points in a track with no channel events,
//   become bookmarks. The song is one section, "Section 1". The first track's name is the song
//   title in a type 0 file, and in a type 1 file whose first track has no channel events
//   (the usual conductor track).
// - Tempos outside 1-1000 BPM are clamped.
SmfImportResult importSmf(std::span<const std::uint8_t> bytes);

SmfImportResult importSmfFile(const std::filesystem::path& path);

enum class SmfType : std::uint8_t
{
    Type0, // one track
    Type1, // a conductor track, then one track per Modal track
};

struct SmfExportResult
{
    std::vector<std::uint8_t> bytes;   // the file, empty if it couldn't be written
    std::string error;                 // why, when there's no file
    std::vector<std::string> warnings; // problems worked around, e.g. a clamped tempo
};

// Exports a Standard MIDI File at the song's resolution.
//
// - The first track holds the title (as its name), the tempo map, the conductor's meter,
//   each section's start as a marker (unless there's only one) and bookmarks as cue
//   points. In a type 1 file each Modal track follows, with its name and a
//   channel prefix; in a type 0 file, everything is in the one track and track names are
//   left out.
// - Events are written as stored, on the track's channel: transpose, velocity and time
//   offsets and mute and solo aren't applied. Port names aren't written.
// - At each tick come note-offs, then other events in stored order, then note-ons, then the
//   note-offs of zero-length notes. Each track ends at its last event or
//   note end, and the first track at the latest of all.
// - Tempos slower than an SMF can hold (about 3.58 BPM) are clamped.
SmfExportResult exportSmf(const Song& song, SmfType type = SmfType::Type1);

// Exports to a file, writing a temporary file beside it first and then renaming it, so a
// failed write never leaves half a file. The result's bytes are what was written.
SmfExportResult exportSmfFile(const Song& song, const std::filesystem::path& path,
                              SmfType type = SmfType::Type1);

} // namespace modal::core
