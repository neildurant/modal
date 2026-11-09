// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/freeze.hpp>
#include <modal/core/playback_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace modal::core
{
namespace
{
bool applies(FreezePart choice, FreezePart part)
{
    return choice == FreezePart::All || choice == part;
}

// Nearest tick in elapsed time, with a tie going to the earlier tick. Comparing distances
// rather than doubling one keeps the calculation within the tempo map's time range.
Tick nearestTick(const TempoMap& tempo, std::chrono::nanoseconds time)
{
    const auto before = tempo.toTick(time);
    const auto since = time - tempo.toTime(before);
    const auto until = tempo.toTime(before + 1) - time;
    return since > until ? before + 1 : before;
}
} // namespace

std::optional<FreezePart> freezePart(std::string_view text)
{
    if (text == "all")
        return FreezePart::All;
    if (text == "transpose")
        return FreezePart::Transpose;
    if (text == "velocity")
        return FreezePart::Velocity;
    if (text == "quantise" || text == "quantize")
        return FreezePart::Quantize;
    if (text == "time" || text == "offset")
        return FreezePart::TimeOffset;
    return std::nullopt;
}

std::string_view freezeName(FreezePart part)
{
    switch (part)
    {
    case FreezePart::All:
        return "All";
    case FreezePart::Transpose:
        return "Transpose";
    case FreezePart::Velocity:
        return "Velocity";
    case FreezePart::Quantize:
        return "Quantise";
    case FreezePart::TimeOffset:
        return "Time offset";
    }
    return {};
}

std::optional<std::string> freezeUnavailable(const Track& track, FreezePart part, bool bypass)
{
    if (part == FreezePart::TimeOffset && track.quantize.on)
    {
        return "Freeze quantise first, or choose All";
    }
    if (applies(part, FreezePart::Quantize) && track.quantize.on && bypass)
    {
        return "Turn off quantise bypass before freezing quantise";
    }
    return std::nullopt;
}

std::variant<FrozenTrack, FreezeRefusal> freezeTrack(const Song& song, TrackId id, FreezePart part,
                                                     bool bypass)
{
    const auto* track = song.track(id);
    const auto refuse = [&](Tick tick, std::string why) -> std::variant<FrozenTrack, FreezeRefusal>
    { return FreezeRefusal{id, tick, std::move(why)}; };
    if (track == nullptr)
        return refuse(0, "No track to freeze");
    if (auto why = freezeUnavailable(*track, part, bypass))
        return refuse(0, std::move(*why));

    FrozenTrack frozen{*track, {}, 0, false};
    std::vector<Event> events;
    if (applies(part, FreezePart::Quantize) && track->quantize.on)
    {
        auto result = playbackQuantized(song, *track);
        if (auto* quantized = std::get_if<QuantizedTrack>(&result))
        {
            frozen.leftInPlace = quantized->leftInPlace;
            for (auto& event : quantized->events)
                events.push_back(std::move(event.event));
        }
        else
            frozen.quantizeFallback = true;
        frozen.settings.quantize.on = false;
    }
    if (events.empty())
    {
        const auto stored = song.events(id).all();
        events.assign(stored.begin(), stored.end());
    }

    const bool timing = applies(part, FreezePart::TimeOffset) && track->timeOffset.amount() != 0;
    const auto& offset = track->timeOffset;
    const auto shifted = [&](Tick tick) -> std::optional<Tick>
    {
        if (!timing)
            return tick;
        if (offset.musical())
        {
            const auto by = offset.amount();
            if ((by < 0 && tick < -by) || (by > 0 && tick > std::numeric_limits<Tick>::max() - by))
                return std::nullopt;
            return tick + by;
        }
        const auto time = offset.at(song.tempo(), tick);
        if (time < std::chrono::nanoseconds{0})
            return std::nullopt;
        return nearestTick(song.tempo(), time);
    };

    // Each pitch's previous note before and after timing conversion, to preserve imported
    // overlaps but refuse a collision introduced by rounding two starts onto one tick.
    struct Previous
    {
        Event before;
        Event after;
    };
    std::array<std::optional<Previous>, 128> previous{};
    for (const auto& original : events)
    {
        auto event = original;
        const auto start = shifted(event.tick);
        if (!start)
            return refuse(original.tick, offset.amount() < 0
                                             ? "An event would start before zero"
                                             : "Time offset exceeds the tick range");
        event.tick = *start;
        if (auto* note = std::get_if<Note>(&event.data))
        {
            if (applies(part, FreezePart::Transpose))
            {
                const auto pitch = playbackPitch(note->pitch, track->transpose);
                if (!pitch)
                    return refuse(original.tick,
                                  "Transpose would put a note outside MIDI pitches 0 to 127");
                note->pitch = *pitch;
            }
            if (applies(part, FreezePart::Velocity))
                note->velocity = playbackVelocity(note->velocity, track->velocityOffset);
            if (note->duration > std::numeric_limits<Tick>::max() - original.tick)
                return refuse(original.tick, "Note end is outside the tick range");
            const auto end = shifted(original.tick + note->duration);
            if (!end)
                return refuse(original.tick,
                              "Time offset would move a note end outside the song's tick range");
            if (note->duration > 0 && *end <= event.tick)
                return refuse(original.tick,
                              "Tick rounding would give a positive note zero length");
            note->duration = *end - event.tick;
            if (timing)
            {
                auto& last = previous[note->pitch];
                if (last)
                {
                    const auto& before = std::get<Note>(last->before.data);
                    const auto& after = std::get<Note>(last->after.data);
                    const bool metBefore = last->before.tick == original.tick ||
                                           last->before.tick + before.duration > original.tick;
                    const bool meetNow = last->after.tick == event.tick ||
                                         last->after.tick + after.duration > event.tick;
                    if (!metBefore && meetNow)
                        return refuse(original.tick,
                                      "Tick rounding would make notes of one pitch collide");
                }
                last = Previous{original, event};
            }
        }
        else if (auto* pressure = std::get_if<PolyPressure>(&event.data);
                 pressure != nullptr && applies(part, FreezePart::Transpose))
        {
            const auto pitch = playbackPitch(pressure->pitch, track->transpose);
            if (!pitch)
                return refuse(
                    original.tick,
                    "Transpose would put polyphonic pressure outside MIDI pitches 0 to 127");
            pressure->pitch = *pitch;
        }
        frozen.events.insert(std::move(event));
    }
    if (applies(part, FreezePart::Transpose))
        frozen.settings.transpose = 0;
    if (applies(part, FreezePart::Velocity))
        frozen.settings.velocityOffset = 0;
    if (applies(part, FreezePart::TimeOffset))
        frozen.settings.timeOffset = {};
    return frozen;
}

} // namespace modal::core
