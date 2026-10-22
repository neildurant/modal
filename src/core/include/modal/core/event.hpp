// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/tick.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace modal::core
{

// Event types. None carries a MIDI channel: the track's channel applies. Data values are
// 7-bit (0-127) unless noted.

struct Note
{
    std::uint8_t pitch;
    std::uint8_t velocity; // 1-127
    Tick duration;         // never negative; 0 for a note-on and note-off at the same tick
    std::uint8_t releaseVelocity = 64;

    friend bool operator==(const Note&, const Note&) = default;
};

struct ControlChange
{
    std::uint8_t controller;
    std::uint8_t value;

    friend bool operator==(const ControlChange&, const ControlChange&) = default;
};

struct ProgramChange
{
    std::uint8_t program;

    friend bool operator==(const ProgramChange&, const ProgramChange&) = default;
};

struct PitchBend
{
    std::int16_t value; // -8192 to 8191, 0 is centre

    static constexpr std::int16_t kMin = -8192;
    static constexpr std::int16_t kMax = 8191;

    friend bool operator==(const PitchBend&, const PitchBend&) = default;
};

struct ChannelPressure
{
    std::uint8_t value;

    friend bool operator==(const ChannelPressure&, const ChannelPressure&) = default;
};

struct PolyPressure
{
    std::uint8_t pitch;
    std::uint8_t value;

    friend bool operator==(const PolyPressure&, const PolyPressure&) = default;
};

// A complete SysEx message, F0 through F7, exactly as sent. The bytes are shared and never
// change, so copying a track doesn't copy them.
struct SysEx
{
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;

    friend bool operator==(const SysEx& a, const SysEx& b)
    {
        return a.bytes == b.bytes || (a.bytes && b.bytes && *a.bytes == *b.bytes);
    }
};

// Text kept with a track's events. Track names and markers aren't stored this way: they
// belong to the track and the song.
struct MetaText
{
    enum class Kind : std::uint8_t
    {
        Text,
        Copyright,
        InstrumentName,
        Lyric,
        CuePoint,
    };

    Kind kind;
    std::shared_ptr<const std::string> text;

    friend bool operator==(const MetaText& a, const MetaText& b)
    {
        return a.kind == b.kind && (a.text == b.text || (a.text && b.text && *a.text == *b.text));
    }
};

using EventData = std::variant<Note, ControlChange, ProgramChange, PitchBend, ChannelPressure,
                               PolyPressure, SysEx, MetaText>;

struct Event
{
    Tick tick;
    EventData data;

    bool isNote() const { return std::holds_alternative<Note>(data); }

    friend bool operator==(const Event&, const Event&) = default;
};

// Whether every field is in range: 7-bit values, note velocity 1-127, pitch bend in range,
// a non-negative tick and duration, and SysEx framed by F0 and F7 with 7-bit data between.
bool isValid(const Event& event);

SysEx makeSysEx(std::vector<std::uint8_t> bytes);
MetaText makeMetaText(MetaText::Kind kind, std::string text);

} // namespace modal::core
