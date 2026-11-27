// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/song_file.hpp>
#include <modal/ui/clipboard.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr std::string_view kHeader = "modal-clip 1";

// The kind of copy, as the header names it.
constexpr std::string_view kEvents = "events";
constexpr std::string_view kSections = "sections";
constexpr std::string_view kTracks = "tracks";

core::Song eventsSong(const EventRegister& reg, core::Ppqn ppqn)
{
    // One section of 4/4, whole bars long enough for every event, note end and ramp, each
    // track with the bars it was taken with as its own meter.
    core::Tick end = std::max<core::Tick>(registerSpan(reg), 1);
    const auto count = std::max(reg.tracks.size(), reg.ramps.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        for (const auto& event : i < reg.tracks.size() ? reg.tracks[i] : std::vector<core::Event>{})
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            end = std::max(end, event.tick + std::max<core::Tick>(note ? note->duration : 0, 1));
        }
        for (const auto& ramp :
             i < reg.ramps.size() ? reg.ramps[i] : std::vector<core::ControllerRamp>{})
        {
            end = std::max(end, ramp.end() + 1);
        }
    }
    const core::MeterMap conductor{ppqn};
    const auto bar = conductor.barStart(2);
    const auto length = (end + bar - 1) / bar * bar;
    core::Section section{"Copy", length, conductor, core::kDefaultTempo};
    std::vector<core::Track> tracks;
    for (std::size_t i = 0; i < count; ++i)
    {
        const core::TrackId id{i + 1};
        tracks.emplace_back(id);
        core::TrackContent content;
        if (i < reg.tracks.size() && !reg.tracks[i].empty())
        {
            core::EventList events;
            for (const auto& event : reg.tracks[i])
            {
                events.insert(event);
            }
            content = core::plainPart(std::move(events), length);
        }
        if (i < reg.ramps.size())
        {
            content.ramps = reg.ramps[i];
        }
        if (i < reg.meters.size())
        {
            content.meter = reg.meters[i];
        }
        if (!content.empty())
        {
            section.setContent(id, std::move(content));
        }
    }
    return core::Song{ppqn, std::move(tracks), {std::move(section)}};
}

core::Song sectionsSong(const EventRegister& reg, core::Ppqn ppqn)
{
    // The sections as they were, but for their marks, which a put leaves behind anyway, and the
    // tracks they were taken from, for a put to match by name.
    std::vector<core::Track> tracks;
    for (const auto& track : reg.fromTracks)
    {
        tracks.push_back(*track);
    }
    std::vector<core::Section> sections;
    for (const auto& from : reg.sections)
    {
        const auto tempo = from->tempo();
        auto& section =
            sections.emplace_back(from->name(), from->length(), from->meter(),
                                  tempo.empty() ? core::kDefaultTempo : tempo.front().tempo);
        section.setColour(from->colour());
        for (std::size_t k = 1; k < tempo.size(); ++k)
        {
            section.addTempo(tempo[k]);
        }
        for (const auto& label : from->labels())
        {
            section.addLabel(label);
        }
        for (const auto& track : tracks)
        {
            if (const auto* content = from->content(track.id()))
            {
                section.setContent(track.id(), *content);
            }
        }
    }
    return core::Song{ppqn, std::move(tracks), std::move(sections)};
}

std::optional<core::Song> tracksSong(const EventRegister& reg)
{
    // Copies taken together share their sections; each section's tempo isn't part of a copy.
    const auto& first = reg.wholeTracks.front();
    std::vector<core::Track> tracks;
    std::vector<core::Section> sections;
    for (const auto& part : first.parts)
    {
        sections.emplace_back(part.section, part.length, part.conductor, core::kDefaultTempo);
    }
    for (std::size_t c = 0; c < reg.wholeTracks.size(); ++c)
    {
        const auto& copy = reg.wholeTracks[c];
        if (copy.ppqn != first.ppqn || copy.parts.size() != first.parts.size())
        {
            return std::nullopt;
        }
        const core::TrackId id{c + 1};
        tracks.push_back(copy.settings.withId(id));
        for (std::size_t k = 0; k < copy.parts.size(); ++k)
        {
            const auto& part = copy.parts[k];
            if (part.section != first.parts[k].section || part.length != first.parts[k].length)
            {
                return std::nullopt;
            }
            if (!part.content.empty())
            {
                sections[k].setContent(id, part.content);
            }
        }
    }
    if (sections.empty())
    {
        return std::nullopt;
    }
    return core::Song{first.ppqn, std::move(tracks), std::move(sections)};
}

std::optional<std::int64_t> number(std::string_view text)
{
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::string> clipboardText(const EventRegister& reg, core::Ppqn ppqn)
{
    if (reg.empty())
    {
        return std::nullopt;
    }
    std::string header{kHeader};
    std::optional<core::Song> song;
    if (!reg.wholeTracks.empty())
    {
        header += " " + std::string{kTracks};
        song = tracksSong(reg);
    }
    else if (!reg.sections.empty())
    {
        header += " " + std::string{kSections};
        song = sectionsSong(reg, ppqn);
    }
    else
    {
        if (std::ranges::any_of(reg.tracks,
                                [](const auto& events)
                                {
                                    return std::ranges::any_of(events, [](const auto& event)
                                                               { return event.tick < 0; });
                                }))
        {
            return std::nullopt;
        }
        header += " " + std::string{kEvents} + " ppqn=" + std::to_string(ppqn.value) +
                  " span=" + std::to_string(reg.span);
        if (reg.bars)
        {
            header += " bars";
        }
        if (!reg.meters.empty())
        {
            header += " meters=" + std::to_string(reg.meters.size());
        }
        if (reg.pitch)
        {
            header += " pitch=" + std::to_string(*reg.pitch);
        }
        song = eventsSong(reg, ppqn);
    }
    if (!song)
    {
        return std::nullopt;
    }
    return header + "\n" + core::writeSong(*song);
}

bool isClipboardCopy(std::string_view text)
{
    return text.starts_with(kHeader) &&
           (text.size() == kHeader.size() || text[kHeader.size()] == ' ' ||
            text[kHeader.size()] == '\n' || text[kHeader.size()] == '\r');
}

ClipboardRead readClipboard(std::string_view text, core::Ppqn ppqn)
{
    if (!isClipboardCopy(text))
    {
        return {.reg = std::nullopt, .error = "it isn't a copy from Modal"};
    }
    const auto lineEnd = text.find('\n');
    if (lineEnd == std::string_view::npos)
    {
        return {.reg = std::nullopt, .error = "it holds nothing after its first line"};
    }
    auto headerLine = text.substr(0, lineEnd);
    if (headerLine.ends_with('\r'))
    {
        headerLine.remove_suffix(1);
    }

    // The header's words: the kind, then settings as name=value or a name alone.
    std::string_view kind;
    std::map<std::string, std::string, std::less<>> settings;
    std::size_t at = kHeader.size();
    while (at < headerLine.size())
    {
        const auto start = headerLine.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto stop = std::min(headerLine.find(' ', start), headerLine.size());
        const auto word = headerLine.substr(start, stop - start);
        at = stop;
        if (kind.empty())
        {
            kind = word;
            continue;
        }
        const auto equals = word.find('=');
        settings.emplace(std::string{word.substr(0, equals)},
                         equals == std::string_view::npos ? std::string{}
                                                          : std::string{word.substr(equals + 1)});
    }
    if (kind != kEvents && kind != kSections && kind != kTracks)
    {
        return {.reg = std::nullopt, .error = "its first line doesn't say what kind of copy it is"};
    }

    auto read = core::readSong(text.substr(lineEnd + 1), ppqn);
    if (!read.song)
    {
        return {.reg = std::nullopt, .error = read.error};
    }
    const auto& song = *read.song;
    EventRegister reg;
    if (kind == kTracks)
    {
        for (const auto& track : song.tracks())
        {
            if (auto copy = song.copyTrack(track->id()))
            {
                reg.wholeTracks.push_back(std::move(*copy));
            }
        }
        return {.reg = std::move(reg), .error = {}};
    }
    if (kind == kSections)
    {
        reg.sections.assign(song.sections().begin(), song.sections().end());
        takeSectionsFrom(reg, song);
        return {.reg = std::move(reg), .error = {}};
    }

    // Events: the span, given at the resolution the copy was made at, rescaled as the song was.
    const auto setting = [&](std::string_view name) -> std::optional<std::int64_t>
    {
        const auto it = settings.find(name);
        return it == settings.end() ? std::nullopt : number(it->second);
    };
    const auto from = setting("ppqn");
    const auto span = setting("span");
    if (!from || *from <= 0 || !span)
    {
        return {.reg = std::nullopt, .error = "its first line doesn't give its ppqn and span"};
    }
    reg.span = (2 * *span * ppqn.value + *from) / (2 * *from);
    reg.bars = settings.contains("bars");
    if (const auto pitch = setting("pitch"); pitch && *pitch <= 127)
    {
        reg.pitch = static_cast<std::uint8_t>(*pitch);
    }
    const auto meters = static_cast<std::size_t>(setting("meters").value_or(0));
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto id = tracks[i]->id();
        const auto events = song.sourceEvents(id).all();
        reg.tracks.emplace_back(events.begin(), events.end());
        const auto ramps = song.ramps(id);
        reg.ramps.emplace_back(ramps.begin(), ramps.end());
    }
    // Each track's bars over the copy, as taking them keeps them.
    const auto length = registerSpan(reg);
    for (std::size_t i = 0; i < tracks.size() && i < meters; ++i)
    {
        const auto& meter = song.meter(tracks[i]->id());
        core::MeterMap bars{ppqn, meter.changes().front().signature};
        for (const auto& change : meter.changes())
        {
            if (change.tick > 0 && change.tick < length)
            {
                bars.set(change.tick, change.signature);
            }
        }
        reg.meters.push_back(std::move(bars));
    }
    return {.reg = std::move(reg), .error = {}};
}

} // namespace modal::ui
