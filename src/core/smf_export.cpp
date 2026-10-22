// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.hpp"

#include <modal/core/smf.hpp>

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace modal::core
{

namespace
{

// The largest delta time a variable-length quantity holds.
constexpr Tick kMaxDelta = 0x0FFF'FFFF;

// The largest tempo a set-tempo event holds, in microseconds per quarter note.
constexpr std::int64_t kMaxSmfTempo = 0xFF'FFFF;

// The largest resolution the header's division holds (the top bit means SMPTE timing).
constexpr std::int32_t kMaxDivision = 0x7FFF;

// The order of events at one tick.
enum class Slot : std::uint8_t
{
    Header,    // track name and channel prefix
    Conductor, // tempo, meter and markers
    NoteOff,
    Other,
    NoteOn,
    ZeroLengthNoteOff, // after its own note-on
};

struct OutEvent
{
    Tick tick;
    Slot slot;
    std::vector<std::uint8_t> bytes; // a channel message, or a whole meta or SysEx event
};

void appendVlq(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    std::uint8_t groups[10];
    int count = 0;
    do
    {
        groups[count++] = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
    } while (value != 0);
    while (count > 1)
    {
        out.push_back(static_cast<std::uint8_t>(groups[--count] | 0x80));
    }
    out.push_back(groups[0]);
}

void appendBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value, int size)
{
    for (int shift = 8 * (size - 1); shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::vector<std::uint8_t> meta(std::uint8_t type, std::string_view data)
{
    std::vector<std::uint8_t> bytes{0xFF, type};
    appendVlq(bytes, data.size());
    bytes.insert(bytes.end(), data.begin(), data.end());
    return bytes;
}

std::vector<std::uint8_t> meta(std::uint8_t type, std::initializer_list<std::uint8_t> data)
{
    return meta(type, std::string_view{reinterpret_cast<const char*>(data.begin()), data.size()});
}

std::uint8_t textType(MetaText::Kind kind)
{
    switch (kind)
    {
    case MetaText::Kind::Text:
        return 0x01;
    case MetaText::Kind::Copyright:
        return 0x02;
    case MetaText::Kind::InstrumentName:
        return 0x04;
    case MetaText::Kind::Lyric:
        return 0x05;
    case MetaText::Kind::CuePoint:
        return 0x07;
    }
    return 0x01;
}

// Adds a track's events, on its channel.
void addTrackEvents(const Track& track, const EventList& events, std::vector<OutEvent>& out)
{
    const auto channel = static_cast<std::uint8_t>(track.channel - 1);
    const auto status = [channel](std::uint8_t type)
    { return static_cast<std::uint8_t>(type | channel); };

    for (const auto& event : events.all())
    {
        const Tick tick = event.tick;
        std::visit(
            [&](const auto& data)
            {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T, Note>)
                {
                    out.push_back({tick, Slot::NoteOn, {status(0x90), data.pitch, data.velocity}});
                    out.push_back({tick + data.duration,
                                   data.duration > 0 ? Slot::NoteOff : Slot::ZeroLengthNoteOff,
                                   {status(0x80), data.pitch, data.releaseVelocity}});
                }
                else if constexpr (std::is_same_v<T, ControlChange>)
                {
                    out.push_back({tick, Slot::Other, {status(0xB0), data.controller, data.value}});
                }
                else if constexpr (std::is_same_v<T, ProgramChange>)
                {
                    out.push_back({tick, Slot::Other, {status(0xC0), data.program}});
                }
                else if constexpr (std::is_same_v<T, PitchBend>)
                {
                    const auto value = static_cast<std::uint16_t>(data.value + 8192);
                    out.push_back({tick,
                                   Slot::Other,
                                   {status(0xE0), static_cast<std::uint8_t>(value & 0x7F),
                                    static_cast<std::uint8_t>(value >> 7)}});
                }
                else if constexpr (std::is_same_v<T, ChannelPressure>)
                {
                    out.push_back({tick, Slot::Other, {status(0xD0), data.value}});
                }
                else if constexpr (std::is_same_v<T, PolyPressure>)
                {
                    out.push_back({tick, Slot::Other, {status(0xA0), data.pitch, data.value}});
                }
                else if constexpr (std::is_same_v<T, SysEx>)
                {
                    // F0, the length, then the rest of the message, F7 included.
                    const auto& sysex = *data.bytes;
                    std::vector<std::uint8_t> bytes{0xF0};
                    appendVlq(bytes, sysex.size() - 1);
                    bytes.insert(bytes.end(), sysex.begin() + 1, sysex.end());
                    out.push_back({tick, Slot::Other, std::move(bytes)});
                }
                else if constexpr (std::is_same_v<T, MetaText>)
                {
                    out.push_back({tick, Slot::Other, meta(textType(data.kind), *data.text)});
                }
            },
            event.data);
    }
}

Tick endOf(const std::vector<OutEvent>& events)
{
    Tick end = 0;
    for (const auto& event : events)
    {
        end = std::max(end, event.tick);
    }
    return end;
}

// Writes one MTrk chunk: the events in tick and slot order, then End of Track. Returns false
// if a delta time is too long for the format.
bool writeTrack(std::vector<OutEvent> events, Tick end, std::vector<std::uint8_t>& out)
{
    std::ranges::stable_sort(events, [](const OutEvent& a, const OutEvent& b)
                             { return std::pair{a.tick, a.slot} < std::pair{b.tick, b.slot}; });

    std::vector<std::uint8_t> data;
    Tick previous = 0;
    std::uint8_t runningStatus = 0;
    const auto delta = [&](Tick tick)
    {
        if (tick - previous > kMaxDelta)
        {
            return false;
        }
        appendVlq(data, static_cast<std::uint64_t>(tick - previous));
        previous = tick;
        return true;
    };

    for (const auto& event : events)
    {
        if (!delta(event.tick))
        {
            return false;
        }
        const auto first = event.bytes.front();
        if (first < 0xF0)
        {
            const auto skip = first == runningStatus ? 1 : 0;
            runningStatus = first;
            data.insert(data.end(), event.bytes.begin() + skip, event.bytes.end());
        }
        else
        {
            runningStatus = 0; // meta and SysEx events cancel running status
            data.insert(data.end(), event.bytes.begin(), event.bytes.end());
        }
    }
    if (!delta(std::max(end, previous)))
    {
        return false;
    }
    data.insert(data.end(), {0xFF, 0x2F, 0x00});

    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    appendBigEndian(out, static_cast<std::uint32_t>(data.size()), 4);
    out.insert(out.end(), data.begin(), data.end());
    return true;
}

} // namespace

SmfExportResult exportSmf(const Song& song, SmfType type)
{
    SmfExportResult result;
    if (song.ppqn().value > kMaxDivision)
    {
        result.error = "a MIDI file can't hold a resolution of " +
                       std::to_string(song.ppqn().value) + " ticks per quarter note";
        return result;
    }

    // The conductor track.
    std::vector<OutEvent> conductor;
    if (!song.title.empty())
    {
        conductor.push_back({0, Slot::Header, meta(0x03, song.title)});
    }
    int clamped = 0;
    for (const auto& change : song.tempo().changes())
    {
        auto micros = change.tempo.microsPerQuarter;
        if (micros > kMaxSmfTempo)
        {
            micros = kMaxSmfTempo;
            ++clamped;
        }
        conductor.push_back({change.tick, Slot::Conductor,
                             meta(0x51, {static_cast<std::uint8_t>(micros >> 16),
                                         static_cast<std::uint8_t>(micros >> 8),
                                         static_cast<std::uint8_t>(micros)})});
    }
    if (clamped > 0)
    {
        result.warnings.push_back("clamped " + std::to_string(clamped) +
                                  " tempo(s) to the slowest a MIDI file can hold");
    }
    for (const auto& change : song.meter().changes())
    {
        const auto& signature = change.signature;
        const auto power = std::countr_zero(static_cast<std::uint32_t>(signature.denominator));
        const auto clocks = std::max(96 / signature.denominator, 1); // MIDI clocks per beat
        conductor.push_back(
            {change.tick, Slot::Conductor,
             meta(0x58, {static_cast<std::uint8_t>(signature.numerator),
                         static_cast<std::uint8_t>(power), static_cast<std::uint8_t>(clocks), 8})});
    }
    // Section starts as markers, so other software sees the structure (unless there's only
    // the one), and bookmarks as cue points, so they can be told apart.
    const auto sections = song.sections();
    if (sections.size() > 1)
    {
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            conductor.push_back(
                {song.sectionStart(i), Slot::Conductor, meta(0x06, sections[i]->name())});
        }
    }
    for (const auto& marker : song.markers())
    {
        conductor.push_back({marker.tick, Slot::Conductor, meta(0x07, marker.name)});
    }

    // The tracks.
    std::vector<std::vector<OutEvent>> tracks;
    for (const auto& track : song.tracks())
    {
        auto& events = tracks.emplace_back();
        if (type == SmfType::Type1)
        {
            if (!track->name.empty())
            {
                events.push_back({0, Slot::Header, meta(0x03, track->name)});
            }
            events.push_back(
                {0, Slot::Header, meta(0x20, {static_cast<std::uint8_t>(track->channel - 1)})});
        }
        addTrackEvents(*track, song.events(track->id()), events);
    }

    Tick end = endOf(conductor);
    for (const auto& events : tracks)
    {
        end = std::max(end, endOf(events));
    }

    std::vector<std::uint8_t> chunks;
    bool fits = true;
    if (type == SmfType::Type0)
    {
        for (auto& events : tracks)
        {
            conductor.insert(conductor.end(), std::make_move_iterator(events.begin()),
                             std::make_move_iterator(events.end()));
        }
        fits = writeTrack(std::move(conductor), end, chunks);
    }
    else
    {
        fits = writeTrack(std::move(conductor), end, chunks);
        for (auto& events : tracks)
        {
            const Tick trackEnd = endOf(events);
            fits = fits && writeTrack(std::move(events), trackEnd, chunks);
        }
    }
    if (!fits)
    {
        result.error = "the song has a gap between events too long for a MIDI file";
        result.warnings.clear();
        return result;
    }

    const auto trackCount = type == SmfType::Type0 ? 1 : 1 + tracks.size();
    auto& bytes = result.bytes;
    bytes.insert(bytes.end(), {'M', 'T', 'h', 'd'});
    appendBigEndian(bytes, 6, 4);
    appendBigEndian(bytes, type == SmfType::Type0 ? 0 : 1, 2);
    appendBigEndian(bytes, static_cast<std::uint32_t>(trackCount), 2);
    appendBigEndian(bytes, static_cast<std::uint32_t>(song.ppqn().value), 2);
    bytes.insert(bytes.end(), chunks.begin(), chunks.end());
    return result;
}

SmfExportResult exportSmfFile(const Song& song, const std::filesystem::path& path, SmfType type)
{
    auto result = exportSmf(song, type);
    if (!result.error.empty())
    {
        return result;
    }

    result.error = writeFileAtomically(path, result.bytes);
    if (!result.error.empty())
    {
        result.bytes.clear();
    }
    return result;
}

} // namespace modal::core
