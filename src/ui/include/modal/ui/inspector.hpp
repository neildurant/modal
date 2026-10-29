// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>
#include <modal/ui/event_list.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace modal::ui
{

// One of the inspector's fields: a heading, shown above its value; whether e can edit it, and
// the text editing starts from if it isn't the value up to any " (" after it; and whether it's
// set aside at the right, its value dim, as the time is, which follows from the position.
struct InspectorField
{
    std::string heading;
    std::string value;
    bool editable = false;
    std::string editText = {};
    bool aside = false;

    friend bool operator==(const InspectorField&, const InspectorField&) = default;
};

// What the inspector shows for a row (decided in milestone 4): the kind of row, and every
// property of what's there, including what the row can't show: a note's end, a controller's
// name, text and SysEx whole, and the time from the song's start; the track and channel
// are in the view's bar, until a view shows several tracks at once. On a row that isn't an
// event, what the row is: a section's bars and length, a tempo, a meter,
// a marker, or at the end the song's length.
struct Inspection
{
    std::string kind; // Note, CC, Section, End and so on
    std::vector<InspectorField> fields;
};

Inspection inspect(const EventList& list, std::size_t row);

// A note's fields after its position and time: its pitch, velocity, length, end and release,
// as the event list's inspector and the piano roll's show them.
void addNoteFields(Inspection& inspection, const core::Song& song, const core::MeterMap& meter,
                   core::Tick tick, const core::Note& note);

// Marks the fields of an event that e can edit: those an event list column holds, and a
// note's end (see setInspectorField).
void markEditable(Inspection& inspection);

// A pitch as its name and number: E1 (28).
std::string pitchText(std::uint8_t pitch);

// A time from the song's start, as minutes, seconds and milliseconds, 1:02.250, with hours
// before them once there are any, 1:00:02.250.
std::string songTime(std::chrono::nanoseconds time);

} // namespace modal::ui
