// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/tick.hpp>
#include <modal/core/track.hpp>
#include <modal/core/transforms.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace modal::core
{

// The pedalling tools (decided October and November 2026), each on a track's events in a
// stretch of time, from up to (not including) to, whatever the view's range takes. Sustain is
// CC 64, sostenuto 66 and soft 67; a pedal is down at 64 or more. Times in milliseconds are
// turned into ticks by the caller, at the tempo where they fall.
inline constexpr int kSustain = 64;

// A switch controller's down span, derived from its stored CC events. The indices belong
// only to the queried event list; no lift means it holds to the effective song end.
struct PedalRegion
{
    Tick start = 0;
    Tick end = 0;
    std::size_t index = 0;
    std::optional<std::size_t> lift;

    friend bool operator==(const PedalRegion&, const PedalRegion&) = default;
};

std::vector<PedalRegion> pedalRegions(const EventList& events, int controller, Tick songEnd);

// The press, its later values and its lift, excluding other controllers and notes.
std::vector<std::size_t> pedalRegionEvents(const EventList& events, int controller,
                                           const PedalRegion& region);

// Musical refusals; the caller supplies the message appropriate to its editing command.
enum class PedalRegionRefusal
{
    AlreadyDown,
    LiftsHere, // a press on the tick another region lifts, which would be ambiguous
    NoRoom,
    BeforeStart,
    PastEnd,
    Collision,
    NoLift,
    EmptyRegion,
};

using PedalRegionResult = std::variant<EventList, PedalRegionRefusal>;

// Insert a press and lift, ending no later than one tick before the next press, and starting
// after any lift, never on its tick: a lift and a press together would be ambiguous, in
// another program if not here, and a pianist's change takes time (decided 16 November 2026).
// Insertion may extend the song, as ordinary event insertion does; songEnd bounds an existing hold.
PedalRegionResult insertPedalRegion(const EventList& events, int controller, Tick start, Tick end,
                                    Tick songEnd);

// Targets come from pedalRegions() on this event list. Every edit preserves unrelated
// events and stored CC values. Nothing is changed if any target would be refused.
EventList erasePedalRegions(const EventList& events, int controller,
                            std::span<const PedalRegion> targets);
PedalRegionResult movePedalRegions(const EventList& events, int controller,
                                   std::span<const PedalRegion> targets, Tick by, Tick songEnd);

// The caller maps each lift to its proposed tick, as grid or elapsed-time steps require.
// A missing lift is refused before asking for its destination. Values cut off by an earlier
// lift are removed; all other values, including the lift's own, are kept.
PedalRegionResult resizePedalRegions(const EventList& events, int controller,
                                     std::span<const PedalRegion> targets,
                                     const std::function<Tick(Tick)>& endAt, Tick songEnd);

// Whether a pedal is down just before a tick, by its last event before it.
bool pedalDown(const EventList& events, int controller, Tick tick);

// Where a change at a tick has the pedal lift just before it and go down just after it.
struct PedalTiming
{
    std::function<Tick(Tick)> lift;
    std::function<Tick(Tick)> press;
};

// The sustain in the stretch replaced: down just after the first change, lifted and pressed
// again around each one after it, and lifted at end, a press that wouldn't come before the
// lift after it left out; a pedal down as the stretch starts lifted
// there, and one down as it ends pressed again there, so what's around it is as it was.
// Re-pedalling at chord changes passes the chords' starts and the last notes' end; pedalling
// per bar or beat, the lines and the stretch's end.
Transformed repedal(const EventList& events, Tick from, Tick to, std::span<const Tick> changes,
                    Tick end, const PedalTiming& timing);

// The performed sustain kept, but each lift within near of where one would be for a change
// moved there, and each press the same, within the stretch; those near no change left where
// they were. Refused if a lift and a press would change places.
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

// Notes the sustain holds into the stretch, those starting before it too, lengthened to where
// it lifts, or to the stretch's end if it doesn't, and the sustain then cleared from the
// stretch as clearPedals() clears it.
Transformed bakePedal(const EventList& events, Tick from, Tick to);

} // namespace modal::core
