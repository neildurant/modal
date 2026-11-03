// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>
#include <modal/core/transforms.hpp>

#include <functional>
#include <span>

namespace modal::core
{

// The pedalling tools (decided October and November 2026), each on a track's events in a
// stretch of time, from up to (not including) to, whatever the view's range takes. Sustain is
// CC 64, sostenuto 66 and soft 67; a pedal is down at 64 or more. Times in milliseconds are
// turned into ticks by the caller, at the tempo where they fall.
inline constexpr int kSustain = 64;

// Whether a pedal is down just before a tick, by its last event before it.
bool pedalDown(const EventList& events, int controller, Tick tick);

// Where a change at a tick has the pedal lift just before it and go down just after it.
struct PedalTiming
{
    std::function<Tick(Tick)> lift;
    std::function<Tick(Tick)> press;
};

// The sustain in the stretch replaced: down just after the first change, lifted and pressed
// again around each one after it, and lifted at end; a pedal down as the stretch starts lifted
// there, and one down as it ends pressed again there, so what's around it is as it was.
// Re-pedalling at chord changes passes the chords' starts and the last notes' end; pedalling
// per bar or beat, the lines and the stretch's end.
Transformed repedal(const EventList& events, Tick from, Tick to, std::span<const Tick> changes,
                    Tick end, const PedalTiming& timing);

// The performed sustain kept, but each lift within near of where one would be for a change
// moved there, and each press the same; those near no change left where they were.
Transformed retimePedal(const EventList& events, Tick from, Tick to, std::span<const Tick> changes,
                        const PedalTiming& timing, const std::function<Tick(Tick)>& near);

// Each pedal's exact repeats removed, and flicker, a lift and a press again within flicker,
// merged, both going; the stretch's first and last events of each pedal always kept.
Transformed cleanPedals(const EventList& events, Tick from, Tick to,
                        const std::function<Tick(Tick)>& flicker);

// The pedals named (sustain, sostenuto, soft) removed from the stretch, lifted at its start if
// down there and pressed again at its end if down there, so what's around it is as it was.
Transformed clearPedals(const EventList& events, Tick from, Tick to,
                        std::span<const int> controllers);

// Every pedal change in the stretch moved by an amount at its tick, later or earlier; refused
// past another change of its pedal or out of the song.
Transformed offsetPedals(const EventList& events, Tick from, Tick to,
                         const std::function<Tick(Tick)>& by, Tick songEnd);

// Notes held by the sustain pedal lengthened to where it lifts, and the sustain then cleared
// from the stretch as clearPedals() clears it.
Transformed bakePedal(const EventList& events, Tick from, Tick to);

} // namespace modal::core
