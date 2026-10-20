#include <modal/core/pitch.hpp>
#include <modal/ui/event_list.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace modal::ui
{

namespace
{

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

std::string pitchText(std::uint8_t pitch)
{
    return core::pitchName(pitch) + " (" + std::to_string(pitch) + ")";
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

std::string_view textKindName(core::MetaText::Kind kind)
{
    using Kind = core::MetaText::Kind;
    switch (kind)
    {
    case Kind::Text:
        return "text";
    case Kind::Copyright:
        return "copyright";
    case Kind::InstrumentName:
        return "instrument";
    case Kind::Lyric:
        return "lyric";
    case Kind::CuePoint:
        return "cue";
    }
    return "text";
}

} // namespace

EventList::EventList(core::Song song, std::optional<core::TrackId> track)
    : song_(std::move(song))
    , track_(track)
{
    const bool conductor = !track_;
    const auto add = [this](RowKind kind, core::Tick tick, std::size_t index, bool dim)
    { rows_.push_back({kind, tick, index, dim}); };

    for (std::size_t i = 0; i < song_.sections().size(); ++i)
    {
        add(RowKind::Section, song_.sectionStart(i), i, !conductor);
    }
    const auto changes = meter().changes();
    for (std::size_t i = 0; i < changes.size(); ++i)
    {
        bool own = false;
        if (track_)
        {
            const auto& section = *song_.sections()[song_.sectionAt(changes[i].tick)];
            const auto* content = section.content(*track_);
            own = content != nullptr && content->meter.has_value();
        }
        add(RowKind::Meter, changes[i].tick, i, !conductor && !own);
    }
    const auto tempo = song_.tempo().changes();
    for (std::size_t i = 0; i < tempo.size(); ++i)
    {
        add(RowKind::Tempo, tempo[i].tick, i, !conductor);
    }
    const auto markers = song_.markers();
    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        add(RowKind::Marker, markers[i].tick, i, !conductor);
    }
    if (track_)
    {
        const auto events = song_.events(*track_).all();
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            add(RowKind::Event, events[i].tick, i, false);
        }
    }
    std::ranges::stable_sort(rows_, [](const EventRow& a, const EventRow& b)
                             { return std::pair{a.tick, a.kind} < std::pair{b.tick, b.kind}; });
    add(RowKind::End, song_.end(), 0, false);
}

const core::MeterMap& EventList::meter() const
{
    return track_ ? song_.meter(*track_) : song_.meter();
}

RowText EventList::text(std::size_t row) const
{
    const auto& r = rows_[row];
    RowText text;
    const auto set = [&](Column column, std::string value)
    { text.cells[static_cast<std::size_t>(column)] = std::move(value); };

    set(Column::Position, positionText(meter(), r.tick));
    switch (r.kind)
    {
    case RowKind::Section:
        set(Column::Event, "section");
        set(Column::Data, song_.sections()[r.index]->name());
        break;
    case RowKind::Meter:
        set(Column::Event, "meter");
        set(Column::Data, signatureText(meter().changes()[r.index].signature));
        break;
    case RowKind::Tempo:
        set(Column::Event, "tempo");
        set(Column::Data, core::bpmText(song_.tempo().changes()[r.index].tempo));
        break;
    case RowKind::Marker:
        set(Column::Event, "marker");
        set(Column::Data, song_.markers()[r.index].name);
        break;
    case RowKind::End:
        set(Column::Event, "end");
        break;
    case RowKind::Event:
    {
        const auto& event = song_.events(*track_)[r.index];
        const auto signature = meter().signatureAt(event.tick);
        std::visit(
            [&](const auto& data)
            {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T, core::Note>)
                {
                    set(Column::Event, "note");
                    set(Column::Data, pitchText(data.pitch));
                    set(Column::Value, std::to_string(data.velocity));
                    set(Column::Length, lengthText(data.duration, song_.ppqn(), signature));
                    set(Column::Release, std::to_string(data.releaseVelocity));
                }
                else if constexpr (std::is_same_v<T, core::ControlChange>)
                {
                    set(Column::Event, "cc");
                    set(Column::Data, std::to_string(data.controller));
                    set(Column::Value, std::to_string(data.value));
                }
                else if constexpr (std::is_same_v<T, core::ProgramChange>)
                {
                    set(Column::Event, "program");
                    set(Column::Data, std::to_string(data.program));
                }
                else if constexpr (std::is_same_v<T, core::PitchBend>)
                {
                    set(Column::Event, "bend");
                    set(Column::Value, std::to_string(data.value));
                }
                else if constexpr (std::is_same_v<T, core::ChannelPressure>)
                {
                    set(Column::Event, "pressure");
                    set(Column::Value, std::to_string(data.value));
                }
                else if constexpr (std::is_same_v<T, core::PolyPressure>)
                {
                    set(Column::Event, "polypressure");
                    set(Column::Data, pitchText(data.pitch));
                    set(Column::Value, std::to_string(data.value));
                }
                else if constexpr (std::is_same_v<T, core::SysEx>)
                {
                    set(Column::Event, "sysex");
                    set(Column::Data, hexBytes(*data.bytes));
                }
                else
                {
                    set(Column::Event, std::string{textKindName(data.kind)});
                    set(Column::Data, *data.text);
                }
            },
            event.data);
        break;
    }
    }

    for (std::size_t c = 0; c < kColumnCount; ++c)
    {
        const auto column = static_cast<Column>(c);
        if (column != Column::Event && !text.cells[c].empty())
        {
            text.fields.push_back(column);
        }
    }
    return text;
}

std::size_t EventList::rowAt(core::Tick tick) const
{
    const auto it = std::ranges::lower_bound(rows_, tick, {}, &EventRow::tick);
    return std::min(static_cast<std::size_t>(it - rows_.begin()), rows_.size() - 1);
}

} // namespace modal::ui
