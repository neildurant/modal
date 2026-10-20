#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/event_text.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::ui
{

// What a row of the event list shows.
enum class RowKind : std::uint8_t
{
    Section, // a section's start
    Meter,
    Tempo,
    Marker,
    Event, // one of the track's events
    End,   // the end of the song, last
};

struct EventRow
{
    RowKind kind;
    core::Tick tick;   // from the song's start
    std::size_t index; // the event's in the track's events, or the section's, change's or marker's
    bool dim;          // the conductor's, shown in a track's list to find the way by; it's
                       // changed only in the conductor's own list (decided in milestone 4)
};

// The list's columns, left to right.
enum class Column : std::uint8_t
{
    Position,
    Event,
    Data,    // a note's pitch, a controller, a program, or a name, text or bytes
    Value,   // a velocity or value
    Length,  // a note's
    Release, // a note's release velocity
};

inline constexpr std::size_t kColumnCount = 6;

// A row's text in each column, and the columns that are fields the cursor can be on: all
// those with text but the event's type.
struct RowText
{
    std::array<std::string, kColumnCount> cells;
    std::vector<Column> fields;

    const std::string& operator[](Column column) const
    {
        return cells[static_cast<std::size_t>(column)];
    }
};

// One track's event list, or the conductor's: a row for each of the track's events, with
// the conductor's section starts, meter and tempo changes and markers among them, dimmed,
// and the end of the song last (decided in milestone 4). The conductor's list has its own
// rows, not dimmed, and no events. At the same tick, a section comes first, then meter,
// tempo and markers, then events in the order they're sent. Meters are the track's own
// where it has them, and then not dimmed.
class EventList
{
public:
    // A track's list, or with no track, the conductor's.
    EventList(core::Song song, std::optional<core::TrackId> track);

    const core::Song& song() const { return song_; }
    const std::optional<core::TrackId>& track() const { return track_; }

    // The bars positions are in: the track's, or the conductor's.
    const core::MeterMap& meter() const;

    std::span<const EventRow> rows() const { return rows_; }
    RowText text(std::size_t row) const;

    // The first row at or after a tick, or the end row.
    std::size_t rowAt(core::Tick tick) const;

private:
    core::Song song_;
    std::optional<core::TrackId> track_;
    std::vector<EventRow> rows_;
};

} // namespace modal::ui
