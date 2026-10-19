#include <modal/core/smf.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>
#include <utility>

namespace modal::core
{

namespace
{

// Reads big-endian values and variable-length quantities, failing at the end of the data
// instead of reading past it.
class Reader
{
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    bool atEnd() const { return position_ >= bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - position_; }

    std::optional<std::uint8_t> peek() const
    {
        return atEnd() ? std::nullopt : std::optional{bytes_[position_]};
    }

    std::optional<std::uint8_t> u8()
    {
        auto value = peek();
        if (value)
        {
            ++position_;
        }
        return value;
    }

    std::optional<std::uint32_t> bigEndian(int size)
    {
        if (remaining() < static_cast<std::size_t>(size))
        {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (int i = 0; i < size; ++i)
        {
            value = value << 8 | bytes_[position_++];
        }
        return value;
    }

    // At most four bytes, as the format requires.
    std::optional<std::uint32_t> vlq()
    {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto byte = u8();
            if (!byte)
            {
                return std::nullopt;
            }
            value = value << 7 | (*byte & 0x7Fu);
            if ((*byte & 0x80u) == 0)
            {
                return value;
            }
        }
        return std::nullopt;
    }

    std::optional<std::span<const std::uint8_t>> take(std::size_t size)
    {
        if (remaining() < size)
        {
            return std::nullopt;
        }
        const auto taken = bytes_.subspan(position_, size);
        position_ += size;
        return taken;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

constexpr int kNoChannel = -1;

// An event at the file's resolution, waiting to be rescaled and given to a track.
struct ParsedEvent
{
    int channel; // 0-15, or kNoChannel for SysEx and text
    Event event;
    Tick end; // for notes: the note-off tick
};

struct ParsedTrack
{
    std::string name;
    std::vector<ParsedEvent> events; // in file order
    std::array<bool, 16> usesChannel{};

    bool hasChannelEvents() const
    {
        return std::ranges::any_of(usesChannel, [](bool used) { return used; });
    }
    bool hasOtherEvents() const
    {
        return std::ranges::any_of(events, [](const auto& e) { return e.channel == kNoChannel; });
    }
};

struct TempoAt
{
    Tick tick;
    std::uint32_t microsPerQuarter;
};

struct MeterAt
{
    Tick tick;
    TimeSignature signature;
};

struct ParsedFile
{
    int format = 0;
    std::int32_t division = 0;
    std::vector<ParsedTrack> tracks;
    std::vector<TempoAt> tempos;
    std::vector<MeterAt> meters;
    std::vector<Marker> markers;
    std::vector<std::string> warnings;
};

std::string text(std::span<const std::uint8_t> bytes)
{
    return {bytes.begin(), bytes.end()};
}

std::optional<MetaText::Kind> textKind(std::uint8_t type)
{
    switch (type)
    {
    case 0x01:
        return MetaText::Kind::Text;
    case 0x02:
        return MetaText::Kind::Copyright;
    case 0x04:
        return MetaText::Kind::InstrumentName;
    case 0x05:
        return MetaText::Kind::Lyric;
    case 0x07:
        return MetaText::Kind::CuePoint;
    default:
        return std::nullopt;
    }
}

bool isData(std::uint8_t byte)
{
    return byte < 0x80;
}

void parseTrack(std::span<const std::uint8_t> data, int number, ParsedFile& file)
{
    const std::string prefix = "track " + std::to_string(number) + ": ";
    Reader reader{data};
    ParsedTrack track;
    Tick tick = 0;
    std::uint8_t runningStatus = 0;

    // Indexes of notes still sounding, per channel and pitch, oldest first.
    std::map<int, std::deque<std::size_t>> sounding;
    std::optional<ParsedEvent> partialSysEx; // an F0 packet awaiting F7 continuations

    int unmatchedNoteOffs = 0;
    int droppedEscapes = 0;
    bool ended = false;
    bool malformed = false;

    const auto noteOff = [&](int channel, std::uint8_t pitch, std::uint8_t releaseVelocity)
    {
        auto& queue = sounding[channel * 128 + pitch];
        if (queue.empty())
        {
            ++unmatchedNoteOffs;
            return;
        }
        auto& parsed = track.events[queue.front()];
        queue.pop_front();
        auto& note = std::get<Note>(parsed.event.data);
        note.duration = tick - parsed.event.tick;
        note.releaseVelocity = releaseVelocity;
        parsed.end = tick;
    };

    const auto addSysEx = [&](Tick at, std::vector<std::uint8_t> bytes)
    {
        track.events.push_back({kNoChannel, {at, makeSysEx(std::move(bytes))}, at});
        if (!isValid(track.events.back().event))
        {
            track.events.pop_back();
            ++droppedEscapes;
        }
    };

    while (!reader.atEnd() && !ended && !malformed)
    {
        const auto delta = reader.vlq();
        const auto first = reader.peek();
        if (!delta || !first)
        {
            malformed = true;
            break;
        }
        tick += *delta;

        std::uint8_t status = runningStatus;
        if (!isData(*first))
        {
            status = *reader.u8();
        }
        else if (runningStatus == 0)
        {
            malformed = true;
            break;
        }

        if (status < 0xF0)
        {
            // Channel message. Meta and SysEx events in between don't cancel running status:
            // the format says they should, but tolerating it misreads no valid file.
            runningStatus = status;
            const int channel = status & 0x0F;
            const int type = status & 0xF0;
            const auto size = (type == 0xC0 || type == 0xD0) ? 1u : 2u;
            const auto bytes = reader.take(size);
            if (!bytes || !std::ranges::all_of(*bytes, isData))
            {
                malformed = true;
                break;
            }
            const auto a = (*bytes)[0];
            const auto b = size == 2 ? (*bytes)[1] : std::uint8_t{0};
            std::optional<EventData> eventData;
            switch (type)
            {
            case 0x80:
                noteOff(channel, a, b);
                break;
            case 0x90:
                if (b == 0)
                {
                    noteOff(channel, a, 64);
                }
                else
                {
                    sounding[channel * 128 + a].push_back(track.events.size());
                    eventData = Note{a, b, 0};
                }
                break;
            case 0xA0:
                eventData = PolyPressure{a, b};
                break;
            case 0xB0:
                eventData = ControlChange{a, b};
                break;
            case 0xC0:
                eventData = ProgramChange{a};
                break;
            case 0xD0:
                eventData = ChannelPressure{a};
                break;
            default: // 0xE0
                eventData = PitchBend{static_cast<std::int16_t>((b << 7 | a) - 8192)};
                break;
            }
            track.usesChannel[static_cast<std::size_t>(channel)] = true;
            if (eventData)
            {
                track.events.push_back({channel, {tick, *eventData}, tick});
            }
        }
        else if (status == 0xFF)
        {
            const auto type = reader.u8();
            const auto length = type ? reader.vlq() : std::nullopt;
            const auto bytes = length ? reader.take(*length) : std::nullopt;
            if (!bytes)
            {
                malformed = true;
                break;
            }
            if (const auto kind = textKind(*type))
            {
                track.events.push_back(
                    {kNoChannel, {tick, makeMetaText(*kind, text(*bytes))}, tick});
            }
            else if (*type == 0x03 && track.name.empty())
            {
                track.name = text(*bytes);
            }
            else if (*type == 0x06)
            {
                file.markers.push_back({tick, text(*bytes)});
            }
            else if (*type == 0x2F)
            {
                ended = true;
            }
            else if (*type == 0x51 && bytes->size() == 3)
            {
                const std::uint32_t micros = std::uint32_t{(*bytes)[0]} << 16 |
                                             std::uint32_t{(*bytes)[1]} << 8 | (*bytes)[2];
                file.tempos.push_back({tick, micros});
            }
            else if (*type == 0x58 && bytes->size() >= 2)
            {
                const auto exponent = (*bytes)[1];
                const TimeSignature signature{(*bytes)[0], exponent < 8 ? 1 << exponent : 0};
                if (signature.isValid())
                {
                    file.meters.push_back({tick, signature});
                }
                else
                {
                    file.warnings.push_back(prefix + "ignored an invalid time signature");
                }
            }
            // Other meta events (sequence number, channel and port prefixes, SMPTE offset, key
            // signature, sequencer-specific) carry nothing Modal stores.
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            const auto length = reader.vlq();
            const auto bytes = length ? reader.take(*length) : std::nullopt;
            if (!bytes)
            {
                malformed = true;
                break;
            }
            const bool terminated = !bytes->empty() && bytes->back() == 0xF7;
            if (status == 0xF0)
            {
                if (partialSysEx)
                {
                    ++droppedEscapes;
                }
                std::vector<std::uint8_t> message{0xF0};
                message.insert(message.end(), bytes->begin(), bytes->end());
                if (terminated)
                {
                    partialSysEx.reset();
                    addSysEx(tick, std::move(message));
                }
                else
                {
                    partialSysEx =
                        ParsedEvent{kNoChannel, {tick, makeSysEx(std::move(message))}, tick};
                }
            }
            else if (partialSysEx)
            {
                // A continuation packet of a split SysEx message.
                auto& sysex = std::get<SysEx>(partialSysEx->event.data);
                auto message = *sysex.bytes;
                message.insert(message.end(), bytes->begin(), bytes->end());
                const Tick start = partialSysEx->event.tick;
                partialSysEx.reset();
                if (terminated)
                {
                    addSysEx(start, std::move(message));
                }
                else
                {
                    partialSysEx =
                        ParsedEvent{kNoChannel, {start, makeSysEx(std::move(message))}, start};
                }
            }
            else if (!bytes->empty() && bytes->front() == 0xF0 && terminated)
            {
                // A complete SysEx message sent as an escape.
                addSysEx(tick, {bytes->begin(), bytes->end()});
            }
            else
            {
                // Other escapes (real-time messages and the like) aren't kept.
                ++droppedEscapes;
            }
        }
        else
        {
            malformed = true;
        }
    }

    if (malformed)
    {
        file.warnings.push_back(prefix + "ended early at malformed or truncated data");
    }
    if (partialSysEx)
    {
        ++droppedEscapes;
    }

    int unterminated = 0;
    for (const auto& [key, queue] : sounding)
    {
        for (const auto index : queue)
        {
            auto& parsed = track.events[index];
            std::get<Note>(parsed.event.data).duration = tick - parsed.event.tick;
            parsed.end = tick;
            ++unterminated;
        }
    }

    if (unmatchedNoteOffs > 0)
    {
        file.warnings.push_back(prefix + "ignored " + std::to_string(unmatchedNoteOffs) +
                                " note-off(s) without a note-on");
    }
    if (unterminated > 0)
    {
        file.warnings.push_back(prefix + std::to_string(unterminated) +
                                " note(s) never ended; they end at the end of the track");
    }
    if (droppedEscapes > 0)
    {
        file.warnings.push_back(prefix + "dropped " + std::to_string(droppedEscapes) +
                                " incomplete SysEx or escape event(s)");
    }

    file.tracks.push_back(std::move(track));
}

std::optional<std::string> parseFile(std::span<const std::uint8_t> bytes, ParsedFile& file)
{
    Reader reader{bytes};
    const auto id = reader.take(4);
    const auto headerLength = reader.bigEndian(4);
    if (!id || std::string_view{reinterpret_cast<const char*>(id->data()), 4} != "MThd" ||
        !headerLength || *headerLength < 6)
    {
        return "not a Standard MIDI File (no MThd header)";
    }
    const auto header = reader.take(*headerLength);
    if (!header)
    {
        return "the MIDI file header is truncated";
    }
    Reader headerReader{*header};
    file.format = static_cast<int>(*headerReader.bigEndian(2));
    const auto trackCount = *headerReader.bigEndian(2);
    const auto division = *headerReader.bigEndian(2);

    if (file.format == 2)
    {
        return "type 2 MIDI files (independent patterns) aren't supported";
    }
    if (file.format > 2)
    {
        return "unknown MIDI file type " + std::to_string(file.format);
    }
    if ((division & 0x8000u) != 0)
    {
        return "MIDI files timed in SMPTE frames aren't supported";
    }
    if (division == 0)
    {
        return "the MIDI file has a resolution of 0 ticks per quarter note";
    }
    file.division = static_cast<std::int32_t>(division);

    while (reader.remaining() >= 8)
    {
        const auto chunkId = *reader.take(4);
        const auto chunkLength = *reader.bigEndian(4);
        auto chunk = reader.take(chunkLength);
        if (!chunk)
        {
            file.warnings.push_back("the last chunk is truncated");
            chunk = reader.take(reader.remaining());
        }
        if (std::string_view{reinterpret_cast<const char*>(chunkId.data()), 4} == "MTrk")
        {
            parseTrack(*chunk, static_cast<int>(file.tracks.size()) + 1, file);
        }
    }

    if (file.tracks.size() != trackCount)
    {
        file.warnings.push_back("the header lists " + std::to_string(trackCount) +
                                " track(s) but the file has " + std::to_string(file.tracks.size()));
    }
    if (file.format == 0 && file.tracks.size() > 1)
    {
        file.warnings.push_back("a type 0 file should have one track; reading all of them");
    }
    return std::nullopt;
}

Song buildSong(ParsedFile& file)
{
    Song song{kPpqn};
    const Tick division = file.division;
    const auto rescale = [division](Tick tick)
    { return (2 * tick * kPpqn.value + division) / (2 * division); };

    // Tempo.
    std::ranges::stable_sort(file.tempos, {}, &TempoAt::tick);
    int clamped = 0;
    song.updateTempo(
        [&](TempoMap& map)
        {
            for (const auto& [tick, micros] : file.tempos)
            {
                Tempo tempo{micros};
                if (!tempo.isValid())
                {
                    tempo.microsPerQuarter = std::clamp<std::int64_t>(
                        micros, Tempo::kMinMicrosPerQuarter, Tempo::kMaxMicrosPerQuarter);
                    ++clamped;
                }
                map.set(rescale(tick), tempo);
            }
        });
    if (clamped > 0)
    {
        file.warnings.push_back("clamped " + std::to_string(clamped) +
                                " tempo(s) to the range 1-1000 BPM");
    }

    if (!file.tracks.empty() && (file.format == 0 || !file.tracks.front().hasChannelEvents()))
    {
        song.title = file.tracks.front().name;
    }

    // Time signatures go to the conductor, which every track follows.
    std::ranges::stable_sort(file.meters, {}, &MeterAt::tick);
    bool meterMoved = false;
    song.updateMeter(
        [&](MeterMap& meter)
        {
            for (const auto& [tick, signature] : file.meters)
            {
                const Tick at = rescale(tick);
                meterMoved = meterMoved || meter.set(at, signature) != at;
            }
        });

    // Cue points in a conductor track (one with no channel events) are bookmarks, as Modal
    // writes them; elsewhere they stay among the track's events.
    for (auto& parsed : file.tracks)
    {
        if (parsed.hasChannelEvents())
        {
            continue;
        }
        std::erase_if(parsed.events,
                      [&](const ParsedEvent& e)
                      {
                          const auto* meta = std::get_if<MetaText>(&e.event.data);
                          if (meta == nullptr || meta->kind != MetaText::Kind::CuePoint)
                          {
                              return false;
                          }
                          file.markers.push_back({e.event.tick, meta->text ? *meta->text : ""});
                          return true;
                      });
    }

    // Tracks: one per channel used by each file track.
    for (const auto& parsed : file.tracks)
    {
        std::vector<int> channels;
        for (int channel = 0; channel < 16; ++channel)
        {
            if (parsed.usesChannel[static_cast<std::size_t>(channel)])
            {
                channels.push_back(channel);
            }
        }
        if (channels.empty())
        {
            if (!parsed.hasOtherEvents())
            {
                continue;
            }
            channels.push_back(0);
        }

        for (const int channel : channels)
        {
            std::string name = parsed.name;
            if (channels.size() > 1)
            {
                const auto label = "ch " + std::to_string(channel + 1);
                name = name.empty() ? "Channel " + std::to_string(channel + 1)
                                    : name + " (" + label + ")";
            }
            const bool takesOtherEvents = channel == channels.front();

            const auto id = song.addTrack(std::move(name));
            song.updateTrack(id, [&](Track& track) { track.channel = channel + 1; });
            song.updateEvents(id,
                              [&](EventList& events)
                              {
                                  for (const auto& e : parsed.events)
                                  {
                                      if (e.channel != channel &&
                                          !(e.channel == kNoChannel && takesOtherEvents))
                                      {
                                          continue;
                                      }
                                      Event event = e.event;
                                      event.tick = rescale(e.event.tick);
                                      if (auto* note = std::get_if<Note>(&event.data))
                                      {
                                          note->duration = rescale(e.end) - event.tick;
                                      }
                                      events.insert(std::move(event));
                                  }
                              });
        }
    }

    // Markers become bookmarks, and the song's one section ends after the last of
    // everything.
    for (const auto& marker : file.markers)
    {
        song.setMarker(rescale(marker.tick), marker.name);
    }
    song.fitToContent();

    if (meterMoved)
    {
        file.warnings.push_back(
            "moved a time signature that fell between beats on to the next beat");
    }
    return song;
}

} // namespace

SmfImportResult importSmf(std::span<const std::uint8_t> bytes)
{
    ParsedFile file;
    SmfImportResult result;
    if (auto error = parseFile(bytes, file))
    {
        result.error = std::move(*error);
        return result;
    }
    result.song = buildSong(file);
    result.warnings = std::move(file.warnings);
    return result;
}

SmfImportResult importSmfFile(const std::filesystem::path& path)
{
    constexpr std::uintmax_t kMaxSize = 256u * 1024 * 1024;

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    SmfImportResult result;
    if (error)
    {
        result.error = "can't read " + path.string() + ": " + error.message();
        return result;
    }
    if (size > kMaxSize)
    {
        result.error = path.string() + " is too large to be a MIDI file";
        return result;
    }

    std::ifstream stream{path, std::ios::binary};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        result.error = "can't read " + path.string();
        return result;
    }
    return importSmf(bytes);
}

} // namespace modal::core
