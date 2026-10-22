// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>

#include <functional>

namespace modal::test
{

// A track's settings with its events, as tests often build them together.
struct EditTrack : core::Track
{
    core::EventList events;
};

// Lets `edit` change a track's settings and events (from the song's start) together.
inline void editTrack(core::Song& song, core::TrackId id,
                      const std::function<void(EditTrack&)>& edit)
{
    EditTrack track{*song.track(id), song.events(id)};
    edit(track);
    song.updateTrack(id, [&](core::Track& settings) { settings = track; });
    song.updateEvents(id, [&](core::EventList& events) { events = track.events; });
}

} // namespace modal::test
