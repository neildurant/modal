// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/playback_plan.hpp>

#include <algorithm>
#include <map>
#include <utility>
#include <variant>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kPolyPressure = 0xA0;
constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kProgramChange = 0xC0;
constexpr std::uint8_t kChannelPressure = 0xD0;
constexpr std::uint8_t kPitchBend = 0xE0;

constexpr std::uint8_t kFirstChannelModeController = 120;

ShortMessage message(std::uint8_t status, std::uint8_t channel, std::uint8_t data1)
{
    return {{static_cast<std::uint8_t>(status | channel), data1, 0}, 2};
}

ShortMessage message(std::uint8_t status, std::uint8_t channel, std::uint8_t data1,
                     std::uint8_t data2)
{
    return {{static_cast<std::uint8_t>(status | channel), data1, data2}, 3};
}

std::optional<std::uint8_t> transposed(std::uint8_t pitch, int transpose)
{
    const int result = pitch + transpose;
    if (result < 0 || result > 127)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(result);
}

// Fills in the message for one stored event, or returns false to leave it out.
struct Planner
{
    const Track& track;
    const TempoMap& tempo;
    std::uint8_t channel;
    nanoseconds offset;
    PlannedEvent& planned;

    bool operator()(const Note& note) const
    {
        const auto pitch = transposed(note.pitch, track.transpose);
        if (!pitch)
        {
            return false;
        }
        const auto velocity =
            static_cast<std::uint8_t>(std::clamp(note.velocity + track.velocityOffset, 1, 127));
        planned.message = message(kNoteOn, channel, *pitch, velocity);
        planned.endTick = planned.tick + note.duration;
        planned.noteOffTime = tempo.toTime(planned.endTick) + offset;
        planned.releaseVelocity = note.releaseVelocity;
        return true;
    }

    bool operator()(const ControlChange& cc) const
    {
        planned.message = message(kControlChange, channel, cc.controller, cc.value);
        return true;
    }

    bool operator()(const ProgramChange& pc) const
    {
        planned.message = message(kProgramChange, channel, pc.program);
        return true;
    }

    bool operator()(const PitchBend& bend) const
    {
        const int value = bend.value - PitchBend::kMin; // 0-16383
        planned.message = message(kPitchBend, channel, static_cast<std::uint8_t>(value & 0x7F),
                                  static_cast<std::uint8_t>(value >> 7));
        return true;
    }

    bool operator()(const ChannelPressure& pressure) const
    {
        planned.message = message(kChannelPressure, channel, pressure.value);
        return true;
    }

    bool operator()(const PolyPressure& pressure) const
    {
        const auto pitch = transposed(pressure.pitch, track.transpose);
        if (!pitch)
        {
            return false;
        }
        planned.message = message(kPolyPressure, channel, *pitch, pressure.value);
        return true;
    }

    bool operator()(const SysEx& sysex) const
    {
        planned.sysex = sysex.bytes;
        return true;
    }

    bool operator()(const MetaText&) const { return false; }
};

// Which chased value an event sets, so later ones replace earlier ones: one slot per
// controller below the channel mode messages, then program, pitch bend and pressure.
std::optional<std::size_t> chaseSlot(const PlannedEvent& event)
{
    if (event.sysex || event.message.size == 0)
    {
        return std::nullopt;
    }
    const auto status = static_cast<std::uint8_t>(event.message.bytes[0] & 0xF0);
    switch (status)
    {
    case kControlChange:
        if (event.message.bytes[1] >= kFirstChannelModeController)
        {
            return std::nullopt;
        }
        return event.message.bytes[1];
    case kProgramChange:
        return kFirstChannelModeController;
    case kPitchBend:
        return kFirstChannelModeController + 1;
    case kChannelPressure:
        return kFirstChannelModeController + 2;
    default:
        return std::nullopt;
    }
}

constexpr std::size_t kChaseSlots = kFirstChannelModeController + 3;

} // namespace

std::size_t PlaybackPlan::firstAtOrAfter(nanoseconds time) const
{
    const auto it = std::ranges::lower_bound(events, time, {}, &PlannedEvent::time);
    return static_cast<std::size_t>(it - events.begin());
}

const PlannedTrack* PlaybackPlan::track(TrackId id) const
{
    const auto it = std::ranges::find(tracks, id, &PlannedTrack::id);
    return it == tracks.end() ? nullptr : &*it;
}

PlaybackPlan compilePlan(const Song& song, const DestinationResolver& resolveDestination,
                         const PlanOptions& options)
{
    PlaybackPlan plan{song.tempo()};

    const auto& metronome = song.metronome;
    using When = MetronomeSettings::When;
    const bool clicks =
        metronome.when == When::Always || (metronome.when == When::Recording && options.recording);
    if (clicks && options.metronomeDestination && metronome.isValid())
    {
        plan.metronome = PlannedMetronome{
            *options.metronomeDestination,
            static_cast<std::uint8_t>(metronome.channel - 1),
            metronome.downbeatNote,
            metronome.downbeatVelocity,
            metronome.beatNote,
            metronome.beatVelocity,
            std::max(metronome.length.toTicks(song.ppqn()), Tick{1}),
            song.meter(),
            options.recording,
        };
    }

    const auto tracks = song.tracks();
    const bool anySolo = std::ranges::any_of(tracks, [](const auto& t) { return t->solo; });

    for (const auto& trackPtr : tracks)
    {
        const Track& track = *trackPtr;
        if (track.arm && track.thru)
        {
            if (const auto destination = resolveDestination(track))
            {
                plan.thru.push_back({track.id(), *destination,
                                     static_cast<std::uint8_t>(track.channel - 1), track.transpose,
                                     track.velocityOffset});
            }
        }

        const bool audible = anySolo ? track.solo : !track.mute;
        if (!audible)
        {
            continue;
        }
        const auto destination = resolveDestination(track);
        if (!destination)
        {
            plan.unrouted.push_back(track.id());
            continue;
        }

        const auto channel = static_cast<std::uint8_t>(track.channel - 1);
        const nanoseconds offset = track.timeOffset;
        plan.tracks.push_back({track.id(), *destination, channel, track.chaseNotes, offset});
        plan.leadIn = std::max(plan.leadIn, -offset);

        std::size_t dropped = 0;
        for (const auto& event : song.events(track.id()).all())
        {
            PlannedEvent planned{};
            planned.time = plan.tempo.toTime(event.tick) + offset;
            planned.tick = event.tick;
            planned.track = track.id();
            planned.destination = *destination;
            if (std::visit(Planner{track, plan.tempo, channel, offset, planned}, event.data))
            {
                plan.events.push_back(std::move(planned));
            }
            else if (std::holds_alternative<Note>(event.data) ||
                     std::holds_alternative<PolyPressure>(event.data))
            {
                ++dropped;
            }
        }
        if (dropped > 0)
        {
            plan.transposeWarnings.push_back({track.id(), dropped});
        }
    }

    std::ranges::stable_sort(plan.events, {}, &PlannedEvent::time);
    return plan;
}

std::vector<PlannedEvent> chase(const PlaybackPlan& plan, Tick start)
{
    struct TrackChase
    {
        std::array<std::optional<std::size_t>, kChaseSlots> last{};
        std::vector<std::size_t> sounding;
    };

    std::map<TrackId, std::size_t> trackIndex;
    for (std::size_t i = 0; i < plan.tracks.size(); ++i)
    {
        trackIndex.emplace(plan.tracks[i].id, i);
    }
    std::vector<TrackChase> chases(plan.tracks.size());

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& event = plan.events[i];
        if (event.tick >= start)
        {
            continue; // played normally; a later event may still be before the start
        }
        const auto t = trackIndex.at(event.track);
        if (event.isNoteOn())
        {
            if (plan.tracks[t].chaseNotes && event.endTick > start)
            {
                chases[t].sounding.push_back(i);
            }
        }
        else if (const auto slot = chaseSlot(event))
        {
            chases[t].last[*slot] = i;
        }
    }

    const nanoseconds startTime = plan.tempo.toTime(start) - plan.leadIn;
    std::vector<PlannedEvent> result;
    for (const auto& trackChase : chases)
    {
        std::vector<std::size_t> values;
        for (const auto& index : trackChase.last)
        {
            if (index)
            {
                values.push_back(*index);
            }
        }
        std::ranges::sort(values); // the order they were sent in
        values.insert(values.end(), trackChase.sounding.begin(), trackChase.sounding.end());
        for (const auto index : values)
        {
            PlannedEvent event = plan.events[index];
            event.time = startTime;
            result.push_back(std::move(event));
        }
    }
    return result;
}

} // namespace modal::core
