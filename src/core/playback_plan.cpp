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

constexpr std::uint8_t kBankSelectMsb = 0;
constexpr std::uint8_t kBankSelectLsb = 32;
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

// Fills in the message for one stored event, or returns false to leave it out.
struct Planner
{
    const Track& track;
    const TempoMap& tempo;
    std::uint8_t channel;
    TimeOffset offset;
    PlannedEvent& planned;

    bool operator()(const Note& note) const
    {
        const auto pitch = playbackPitch(note.pitch, track.transpose);
        if (!pitch)
        {
            return false;
        }
        const auto velocity = playbackVelocity(note.velocity, track.velocityOffset);
        planned.message = message(kNoteOn, channel, *pitch, velocity);
        planned.endTick = planned.tick + note.duration;
        planned.noteOffTime = offset.at(tempo, planned.endTick);
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
        const auto pitch = playbackPitch(pressure.pitch, track.transpose);
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

static_assert(kChaseSlots == kFirstChannelModeController + 3);

// The messages for a track's setup: bank before program, as instruments expect.
std::vector<ShortMessage> setupMessages(const ProgramSetup& setup, std::uint8_t channel)
{
    std::vector<ShortMessage> messages;
    if (setup.bankMsb)
    {
        messages.push_back(message(kControlChange, channel, kBankSelectMsb, *setup.bankMsb));
    }
    if (setup.bankLsb)
    {
        messages.push_back(message(kControlChange, channel, kBankSelectLsb, *setup.bankLsb));
    }
    if (setup.program)
    {
        messages.push_back(message(kProgramChange, channel, *setup.program));
    }
    return messages;
}

} // namespace

std::optional<std::size_t> chaseSlot(const PlannedEvent& event) noexcept
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
    plan.song = song.identity();

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
        const auto offset = track.timeOffset;
        plan.tracks.push_back({track.id(),
                               *destination,
                               channel,
                               track.chaseNotes,
                               offset,
                               setupMessages(track.programSetup, channel),
                               {},
                               std::nullopt,
                               {}});
        plan.leadIn = std::max(plan.leadIn, -offset.at(plan.tempo, 0));
        if (offset.musical() && offset.amount() < 0)
        {
            // The delay across a fixed tick window can peak at either edge of a tempo
            // segment, even on an empty armed track recording from later in the song.
            for (const auto& change : plan.tempo.changes())
            {
                for (const auto tick : {change.tick, change.tick - offset.amount()})
                    plan.leadIn = std::max(plan.leadIn,
                                           plan.tempo.toTime(tick) - offset.at(plan.tempo, tick));
            }
        }

        // Where each event plays: quantised if the track is, as the quantise transform would
        // leave it; or else as stored, as it is if it can't be, which is reported.
        const auto& stored = song.events(track.id());
        std::vector<QuantizedEvent> played;
        const auto& quantize = track.quantize;
        if (quantize.on && !options.bypassQuantize)
        {
            auto result = playbackQuantized(song, track);
            if (auto* quantized = std::get_if<QuantizedTrack>(&result))
            {
                if (quantized->leftInPlace > 0)
                {
                    plan.quantizeWarnings.push_back(
                        {track.id(), quantized->firstLeft, quantized->leftInPlace});
                }
                played = std::move(quantized->events);
                plan.tracks.back().quantize = quantize;
                plan.tracks.back().quantize->grid = quantize.grid.value_or(track.grid);
            }
            else
            {
                plan.quantizeWarnings.push_back(
                    {track.id(), std::get<Refusal>(result).event.tick, 0});
            }
        }
        if (!plan.tracks.back().quantize)
        {
            played.reserve(stored.size());
            for (const auto& event : stored.all())
            {
                played.push_back({event, event.tick});
            }
        }

        std::size_t dropped = 0;
        for (const auto& [event, at] : played)
        {
            PlannedEvent planned{};
            planned.time = offset.at(plan.tempo, event.tick);
            planned.tick = event.tick;
            planned.stored = at;
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
    std::map<TrackId, std::size_t> trackIndex;
    for (std::size_t i = 0; i < plan.tracks.size(); ++i)
    {
        trackIndex.emplace(plan.tracks[i].id, i);
    }
    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        plan.tracks[trackIndex.at(plan.events[i].track)].events.push_back(i);
    }
    for (auto& track : plan.tracks)
    {
        track.byStored = track.events;
        std::ranges::stable_sort(track.byStored, {},
                                 [&](std::size_t i) { return plan.events[i].stored; });
    }
    return plan;
}

std::vector<PlannedEvent> chase(const PlaybackPlan& plan, Tick start)
{
    struct TrackChase
    {
        std::array<std::optional<std::size_t>, kChaseSlots> last{};
        std::vector<std::size_t> sounding;
    };

    std::vector<TrackChase> chases(plan.tracks.size());

    for (std::size_t t = 0; t < plan.tracks.size(); ++t)
    {
        for (const auto i : plan.tracks[t].events)
        {
            const auto& event = plan.events[i];
            if (event.tick >= start)
            {
                continue; // played normally; a later event may still be before the start
            }
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
    }

    const nanoseconds startTime = plan.tempo.toTime(start) - plan.leadIn;
    std::vector<PlannedEvent> result;
    for (std::size_t t = 0; t < plan.tracks.size(); ++t)
    {
        const auto& track = plan.tracks[t];
        const auto& trackChase = chases[t];
        for (const auto& setup : track.setup)
        {
            PlannedEvent event{};
            event.time = startTime;
            event.tick = start;
            event.track = track.id;
            event.destination = track.destination;
            event.message = setup;
            result.push_back(std::move(event));
        }
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
