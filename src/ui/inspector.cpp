#include <modal/core/controllers.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/inspector.hpp>

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace modal::ui
{

namespace
{

std::string pitchText(std::uint8_t pitch)
{
    return core::pitchName(pitch) + " (" + std::to_string(pitch) + ")";
}

std::string padded(std::int64_t value, std::size_t digits)
{
    auto text = std::to_string(value);
    return std::string(digits > text.size() ? digits - text.size() : 0, '0') + text;
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes)
{
    constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string text;
    for (const auto byte : bytes)
    {
        text += text.empty() ? "" : " ";
        text += kHex[byte >> 4];
        text += kHex[byte & 0xF];
    }
    return text;
}

// Microseconds as milliseconds, with only the decimals it takes: 500 ms, 594.059 ms.
std::string millisText(std::int64_t micros)
{
    auto text = std::to_string(micros / 1000) + "." + padded(micros % 1000, 3);
    while (text.back() == '0')
    {
        text.pop_back();
    }
    if (text.back() == '.')
    {
        text.pop_back();
    }
    return text + " ms";
}

std::string barsText(std::int64_t bars)
{
    return std::to_string(bars) + (bars == 1 ? " bar" : " bars");
}

const char* textKind(core::MetaText::Kind kind)
{
    using Kind = core::MetaText::Kind;
    switch (kind)
    {
    case Kind::Text:
        return "Text";
    case Kind::Copyright:
        return "Copyright";
    case Kind::InstrumentName:
        return "Instrument";
    case Kind::Lyric:
        return "Lyric";
    case Kind::CuePoint:
        return "Cue";
    }
    return "Text";
}

} // namespace

std::string songTime(std::chrono::nanoseconds time)
{
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
    const auto seconds = millis / 1000;
    const auto minutes = seconds / 60;
    const auto hours = minutes / 60;
    const auto rest = padded(seconds % 60, 2) + "." + padded(millis % 1000, 3);
    return hours > 0 ? std::to_string(hours) + ":" + padded(minutes % 60, 2) + ":" + rest
                     : std::to_string(minutes) + ":" + rest;
}

Inspection inspect(const EventList& list, std::size_t row)
{
    const auto& song = list.song();
    const auto& meter = list.meter();
    const auto& r = list.rows()[row];
    Inspection result;
    auto& fields = result.fields;
    const auto add = [&](std::string heading, std::string value)
    { fields.push_back({std::move(heading), std::move(value)}); };

    add("Position", positionText(meter, r.tick));
    add("Time", songTime(song.tempo().toTime(r.tick)));

    switch (r.kind)
    {
    case RowKind::Section:
    {
        result.kind = "Section";
        const auto first = meter.toBarBeatTick(song.sectionStart(r.index)).bar;
        const auto last = meter.toBarBeatTick(song.sectionStart(r.index + 1) - 1).bar;
        const auto length = song.tempo().toTime(song.sectionStart(r.index + 1)) -
                            song.tempo().toTime(song.sectionStart(r.index));
        add("Section", song.sections()[r.index]->name());
        add("Bars", std::to_string(first) + "–" + std::to_string(last));
        add("Length", barsText(last - first + 1) + ", " + songTime(length));
        return result;
    }
    case RowKind::Tempo:
    {
        result.kind = "Tempo";
        const auto tempo = song.tempo().changes()[r.index].tempo;
        add("Tempo", core::bpmText(tempo) + " BPM");
        add("Per quarter", millisText(tempo.microsPerQuarter));
        return result;
    }
    case RowKind::Meter:
    {
        result.kind = "Meter";
        const auto signature = meter.changes()[r.index].signature;
        add("Meter",
            std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator));
        return result;
    }
    case RowKind::Marker:
        result.kind = "Marker";
        add("Marker", song.markers()[r.index].name);
        return result;
    case RowKind::End:
        result.kind = "End";
        add("Song", barsText(meter.toBarBeatTick(std::max<core::Tick>(r.tick - 1, 0)).bar) + ", " +
                        songTime(song.tempo().toTime(r.tick)));
        return result;
    case RowKind::Event:
        break;
    }

    const auto id = *list.track();
    const auto& event = song.events(id)[r.index];
    std::visit(
        [&](const auto& data)
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, core::Note>)
            {
                result.kind = "Note";
                add("Pitch", pitchText(data.pitch));
                add("Velocity", std::to_string(data.velocity));
                add("Length",
                    lengthText(data.duration, song.ppqn(), meter.signatureAt(event.tick)));
                add("Ends", positionText(meter, event.tick + data.duration));
                add("Release", std::to_string(data.releaseVelocity));
            }
            else if constexpr (std::is_same_v<T, core::ControlChange>)
            {
                result.kind = "CC";
                const auto name = core::controllerName(data.controller);
                add("Controller",
                    std::to_string(data.controller) + (name.empty() ? "" : " " + name));
                add("Value", std::to_string(data.value));
            }
            else if constexpr (std::is_same_v<T, core::ProgramChange>)
            {
                result.kind = "Program";
                add("Program", std::to_string(data.program));
            }
            else if constexpr (std::is_same_v<T, core::PitchBend>)
            {
                result.kind = "Bend";
                add("Bend", std::to_string(data.value));
            }
            else if constexpr (std::is_same_v<T, core::ChannelPressure>)
            {
                result.kind = "Pressure";
                add("Pressure", std::to_string(data.value));
            }
            else if constexpr (std::is_same_v<T, core::PolyPressure>)
            {
                result.kind = "Poly pressure";
                add("Pitch", pitchText(data.pitch));
                add("Pressure", std::to_string(data.value));
            }
            else if constexpr (std::is_same_v<T, core::SysEx>)
            {
                result.kind = "SysEx";
                add("Bytes", std::to_string(data.bytes->size()));
                add("SysEx", hexBytes(*data.bytes));
            }
            else
            {
                result.kind = textKind(data.kind);
                add("Text", *data.text);
            }
        },
        event.data);
    return result;
}

} // namespace modal::ui
