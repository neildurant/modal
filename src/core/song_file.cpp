#include "files.hpp"

#include <modal/core/note_fraction.hpp>
#include <modal/core/pitch.hpp>
#include <modal/core/song_file.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace modal::core
{

namespace
{

constexpr int kVersion = 1;
constexpr std::uintmax_t kMaxFileSize = 256u * 1024 * 1024;
constexpr std::int64_t kMaxPpqn = std::int64_t{1} << 20;

// A note length is written as a note fraction when its denominator divides this (down to
// 1/256 notes and their triplets), and otherwise in ticks.
constexpr std::int64_t kFractionDenominators = 768;

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

// Writing.

// The length of the UTF-8 sequence starting at `i`, or 0 if there isn't a valid one.
std::size_t utf8Length(std::string_view text, std::size_t i)
{
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 0;
    if (lead < 0x80)
    {
        length = 1;
    }
    else if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
    }
    if (length == 0 || i + length > text.size())
    {
        return 0;
    }
    for (std::size_t k = 1; k < length; ++k)
    {
        if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
        {
            return 0;
        }
    }
    return length;
}

// Text in double quotes, with \" and \\, and \xNN for control characters and for bytes that
// aren't UTF-8, as an imported file's raw text may have.
std::string quotedText(std::string_view text)
{
    std::string out = "\"";
    for (std::size_t i = 0; i < text.size();)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        const auto length = utf8Length(text, i);
        if (byte == '"' || byte == '\\')
        {
            out += '\\';
            out += text[i++];
        }
        else if (length == 0 || byte < 0x20 || byte == 0x7F)
        {
            out += "\\x";
            out += kHexDigits[byte >> 4];
            out += kHexDigits[byte & 0xF];
            ++i;
        }
        else
        {
            out += text.substr(i, length);
            i += length;
        }
    }
    out += '"';
    return out;
}

std::string positionText(const MeterMap& meter, Tick tick)
{
    const auto position = meter.toBarBeatTick(tick);
    return std::to_string(position.bar) + ":" + std::to_string(position.beat) + ":" +
           std::to_string(position.tick);
}

std::string signatureText(TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

std::string signedText(std::int64_t value)
{
    return (value > 0 ? "+" : "") + std::to_string(value);
}

std::string lengthText(Tick duration, Ppqn ppqn)
{
    if (duration > 0)
    {
        const NoteFraction fraction{duration, ppqn.whole()};
        if (kFractionDenominators % fraction.denominator() == 0)
        {
            return toString(fraction);
        }
    }
    return std::to_string(duration);
}

constexpr std::array<std::pair<MetaText::Kind, std::string_view>, 5> kTextKinds{{
    {MetaText::Kind::Text, "text"},
    {MetaText::Kind::Copyright, "copyright"},
    {MetaText::Kind::InstrumentName, "instrument"},
    {MetaText::Kind::Lyric, "lyric"},
    {MetaText::Kind::CuePoint, "cue"},
}};

struct EventText
{
    Ppqn ppqn;

    std::string operator()(const Note& note) const
    {
        std::string text = "note " + pitchName(note.pitch) + " v=" + std::to_string(note.velocity) +
                           " len=" + lengthText(note.duration, ppqn);
        if (note.releaseVelocity != 64)
        {
            text += " rel=" + std::to_string(note.releaseVelocity);
        }
        return text;
    }
    std::string operator()(const ControlChange& cc) const
    {
        return "cc " + std::to_string(cc.controller) + " " + std::to_string(cc.value);
    }
    std::string operator()(const ProgramChange& program) const
    {
        return "program " + std::to_string(program.program);
    }
    std::string operator()(const PitchBend& bend) const
    {
        return "bend " + std::to_string(bend.value);
    }
    std::string operator()(const ChannelPressure& pressure) const
    {
        return "pressure " + std::to_string(pressure.value);
    }
    std::string operator()(const PolyPressure& pressure) const
    {
        return "polypressure " + pitchName(pressure.pitch) + " " + std::to_string(pressure.value);
    }
    std::string operator()(const SysEx& sysex) const
    {
        std::string text = "sysex";
        for (const auto byte : *sysex.bytes)
        {
            text += ' ';
            text += kHexDigits[byte >> 4];
            text += kHexDigits[byte & 0xF];
        }
        return text;
    }
    std::string operator()(const MetaText& meta) const
    {
        const auto it = std::ranges::find(kTextKinds, meta.kind,
                                          &std::pair<MetaText::Kind, std::string_view>::first);
        return std::string{it->second} + " " + quotedText(*meta.text);
    }
};

constexpr std::array<std::string_view, 3> kWhens{"off", "recording", "always"};
constexpr std::array<std::string_view, 3> kModes{"none", "countin", "preroll"};

// Reading.

struct Token
{
    std::string text;
    bool quoted = false; // the token starts with a quote, so it's text, never a setting
};

// Splits a line into tokens at spaces and tabs, reading quoted text with its escapes, also
// as part of a token (port="My Port"). Returns an error message for a broken quote.
std::string tokenize(std::string_view line, std::vector<Token>& tokens)
{
    const auto hex = [](char c) -> int
    {
        const auto at = kHexDigits.find(static_cast<char>(c >= 'a' ? c - 'a' + 'A' : c));
        return at == std::string_view::npos ? -1 : static_cast<int>(at);
    };
    std::size_t i = 0;
    for (;;)
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        {
            ++i;
        }
        if (i == line.size())
        {
            return {};
        }
        Token token;
        token.quoted = line[i] == '"';
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
        {
            if (line[i] != '"')
            {
                token.text += line[i++];
                continue;
            }
            ++i;
            for (;;)
            {
                if (i == line.size())
                {
                    return "a quote isn't closed";
                }
                const char c = line[i++];
                if (c == '"')
                {
                    break;
                }
                if (c != '\\')
                {
                    token.text += c;
                    continue;
                }
                const char escaped = i < line.size() ? line[i++] : '\0';
                if (escaped == '"' || escaped == '\\')
                {
                    token.text += escaped;
                }
                else if (escaped == 'x' && i + 2 <= line.size() && hex(line[i]) >= 0 &&
                         hex(line[i + 1]) >= 0)
                {
                    token.text += static_cast<char>(hex(line[i]) * 16 + hex(line[i + 1]));
                    i += 2;
                }
                else
                {
                    return "unknown escape in quoted text";
                }
            }
        }
        tokens.push_back(std::move(token));
    }
}

std::optional<std::int64_t> parseInteger(std::string_view text, std::int64_t min, std::int64_t max)
{
    if (!text.empty() && text[0] == '+')
    {
        text.remove_prefix(1);
    }
    std::int64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end || value < min || value > max)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<BarBeatTick> parsePosition(std::string_view text)
{
    const auto first = text.find(':');
    const auto second = first == std::string_view::npos ? first : text.find(':', first + 1);
    if (second == std::string_view::npos)
    {
        return std::nullopt;
    }
    constexpr auto kMax = std::numeric_limits<std::int32_t>::max();
    const auto bar = parseInteger(text.substr(0, first), 1, kMax);
    const auto beat = parseInteger(text.substr(first + 1, second - first - 1), 1, kMax);
    const auto tick = parseInteger(text.substr(second + 1), 0, kMax);
    if (!bar || !beat || !tick || text.find('+') != std::string_view::npos)
    {
        return std::nullopt;
    }
    return BarBeatTick{*bar, *beat, *tick};
}

// A setting's name and value, as in ch=10; a token without = is a flag, with no value.
struct Setting
{
    std::string_view name;
    std::optional<std::string_view> value;
};

Setting settingOf(const Token& token)
{
    const std::string_view text = token.text;
    const auto equals = text.find('=');
    if (equals == std::string_view::npos)
    {
        return {text, std::nullopt};
    }
    return {text.substr(0, equals), text.substr(equals + 1)};
}

class Reader
{
public:
    SongReadResult read(std::string_view text);

private:
    bool fail(const std::string& message) { return failAt(line_, message); }
    bool failAt(int line, const std::string& message)
    {
        if (error_.empty())
        {
            error_ = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }

    bool readLine(std::span<const Token> tokens);
    bool readVersion(std::span<const Token> tokens);
    bool readSong(std::span<const Token> tokens);
    bool readMetronome(std::span<const Token> tokens);
    bool readCountIn(std::span<const Token> tokens);
    bool readTrack(std::span<const Token> tokens);
    bool readSection(std::span<const Token> tokens);
    bool readMeter(std::span<const Token> tokens);
    bool readTempo(std::span<const Token> tokens);
    bool readMarker(std::span<const Token> tokens);
    bool readBlock(std::span<const Token> tokens);
    bool readEvent(std::span<const Token> tokens);
    std::optional<EventData> eventData(std::span<const Token> tokens);

    // Ends the conductor's part of the section, ready for the tracks.
    bool closeConductor();
    // Ends the track block being read, and the section.
    bool closeBlock();
    bool closeSection();

    std::optional<Tick> tickAt(const MeterMap& meter, std::string_view text);
    std::optional<TimeSignature> signature(std::string_view text);
    std::optional<std::uint8_t> byteValue(std::string_view text, std::string_view what);
    std::optional<std::uint8_t> pitch(std::string_view text);
    std::optional<Tick> length(std::string_view text);

    enum class Stage : std::uint8_t
    {
        Version,
        Song,
        Header,
        Sections,
    };

    struct Pending
    {
        int line;
        Tick tick;
    };

    struct OpenSection
    {
        int line = 0;
        std::string name;
        std::optional<std::string> colour;
        std::int64_t bars = 0;
        std::optional<MeterMap> meter;
        bool meterClosed = false; // by a tempo, marker or track line
        std::vector<std::pair<Pending, Tempo>> tempo;
        std::vector<std::pair<Pending, std::string>> markers;
        std::optional<Section> section; // once the conductor's part is complete
        std::set<TrackId> blocks;
    };

    struct OpenBlock
    {
        TrackId id;
        std::optional<MeterMap> meter;
        EventList events;
    };

    int line_ = 0;
    std::string error_;
    Stage stage_ = Stage::Version;
    Ppqn ppqn_ = kPpqn;
    std::string title_;
    MetronomeSettings metronome_;
    CountInSettings countIn_;
    std::vector<Track> tracks_;
    std::vector<Section> sections_;
    std::optional<OpenSection> section_;
    std::optional<OpenBlock> block_;
};

SongReadResult Reader::read(std::string_view text)
{
    SongReadResult result;
    std::vector<Token> tokens;
    while (!text.empty() && error_.empty())
    {
        ++line_;
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '"')
        {
            continue; // a blank line or a comment
        }
        tokens.clear();
        if (const auto broken = tokenize(line, tokens); !broken.empty())
        {
            fail(broken);
            break;
        }
        readLine(tokens);
    }
    if (error_.empty())
    {
        ++line_;
        if (stage_ != Stage::Sections)
        {
            fail(stage_ == Stage::Version ? "not a Modal song: it's empty"
                                          : "the song has no sections");
        }
        else
        {
            closeSection();
        }
    }
    if (!error_.empty())
    {
        result.error = error_;
        return result;
    }
    result.song.emplace(ppqn_, std::move(tracks_), std::move(sections_));
    result.song->title = std::move(title_);
    result.song->metronome = std::move(metronome_);
    result.song->countIn = countIn_;
    return result;
}

bool Reader::readLine(std::span<const Token> tokens)
{
    const auto& word = tokens[0].text;
    if (stage_ == Stage::Version)
    {
        return readVersion(tokens);
    }
    if (stage_ == Stage::Song)
    {
        return word == "song" ? readSong(tokens) : fail("the song line should come second");
    }
    if (!tokens[0].quoted && !word.empty() && word[0] >= '0' && word[0] <= '9')
    {
        return readEvent(tokens);
    }
    if (word == "section")
    {
        return readSection(tokens);
    }
    if (word == "track")
    {
        return stage_ == Stage::Header ? readTrack(tokens) : readBlock(tokens);
    }
    if (word == "metronome" || word == "countin")
    {
        if (stage_ != Stage::Header)
        {
            return fail(word + " comes before the sections");
        }
        return word == "metronome" ? readMetronome(tokens) : readCountIn(tokens);
    }
    if (word == "meter" || word == "tempo" || word == "marker")
    {
        if (!section_)
        {
            return fail(word + " belongs in a section");
        }
        return word == "meter"   ? readMeter(tokens)
               : word == "tempo" ? readTempo(tokens)
                                 : readMarker(tokens);
    }
    return fail("unknown line \"" + word + "\"");
}

bool Reader::readVersion(std::span<const Token> tokens)
{
    if (tokens.size() != 2 || tokens[0].text != "modal-song")
    {
        return fail("not a Modal song: it should start with \"modal-song " +
                    std::to_string(kVersion) + "\"");
    }
    const auto version = parseInteger(tokens[1].text, 1, std::numeric_limits<int>::max());
    if (!version)
    {
        return fail("unknown format version \"" + tokens[1].text + "\"");
    }
    if (*version > kVersion)
    {
        return fail("the song was written by a newer Modal (format " + tokens[1].text + ")");
    }
    stage_ = Stage::Song;
    return true;
}

bool Reader::readSong(std::span<const Token> tokens)
{
    if (tokens.size() < 2 || !tokens[1].quoted)
    {
        return fail("the song line needs a title in quotes");
    }
    title_ = tokens[1].text;
    bool hasPpqn = false;
    for (const auto& token : tokens.subspan(2))
    {
        const auto [name, value] = settingOf(token);
        if (name != "ppqn" || !value)
        {
            return fail("unknown song setting \"" + token.text + "\"");
        }
        const auto ppqn = parseInteger(*value, 1, kMaxPpqn);
        if (!ppqn)
        {
            return fail("ppqn should be a whole number from 1 to " + std::to_string(kMaxPpqn));
        }
        ppqn_ = Ppqn{static_cast<std::int32_t>(*ppqn)};
        hasPpqn = true;
    }
    if (!hasPpqn)
    {
        return fail("the song line needs its ppqn");
    }
    stage_ = Stage::Header;
    return true;
}

bool Reader::readMetronome(std::span<const Token> tokens)
{
    for (const auto& token : tokens.subspan(1))
    {
        const auto [name, value] = settingOf(token);
        if (!value)
        {
            return fail("unknown metronome setting \"" + token.text + "\"");
        }
        if (name == "when")
        {
            const auto it = std::ranges::find(kWhens, *value);
            if (it == kWhens.end())
            {
                return fail("when should be off, recording or always");
            }
            metronome_.when = static_cast<MetronomeSettings::When>(it - kWhens.begin());
        }
        else if (name == "port")
        {
            metronome_.port = std::string{*value};
        }
        else if (name == "ch")
        {
            const auto channel = parseInteger(*value, 1, 16);
            if (!channel)
            {
                return fail("ch should be a channel from 1 to 16");
            }
            metronome_.channel = static_cast<int>(*channel);
        }
        else if (name == "downbeat" || name == "beat")
        {
            const auto note = pitch(*value);
            if (!note)
            {
                return false;
            }
            (name == "downbeat" ? metronome_.downbeatNote : metronome_.beatNote) = *note;
        }
        else if (name == "downbeatvel" || name == "beatvel")
        {
            const auto velocity = parseInteger(*value, 1, 127);
            if (!velocity)
            {
                return fail(std::string{name} + " should be a velocity from 1 to 127");
            }
            (name == "downbeatvel" ? metronome_.downbeatVelocity : metronome_.beatVelocity) =
                static_cast<std::uint8_t>(*velocity);
        }
        else if (name == "len")
        {
            const auto fraction = parseNoteFraction(*value);
            if (!fraction)
            {
                return fail("len should be a note length, such as 1/32");
            }
            metronome_.length = *fraction;
        }
        else
        {
            return fail("unknown metronome setting \"" + std::string{name} + "\"");
        }
    }
    return true;
}

bool Reader::readCountIn(std::span<const Token> tokens)
{
    for (const auto& token : tokens.subspan(1))
    {
        const auto [name, value] = settingOf(token);
        if (!value)
        {
            return fail("unknown count-in setting \"" + token.text + "\"");
        }
        if (name == "mode")
        {
            const auto it = std::ranges::find(kModes, *value);
            if (it == kModes.end())
            {
                return fail("mode should be none, countin or preroll");
            }
            countIn_.mode = static_cast<CountInSettings::Mode>(it - kModes.begin());
            continue;
        }
        int* field = name == "bars"      ? &countIn_.bars
                     : name == "beats"   ? &countIn_.beats
                     : name == "preroll" ? &countIn_.preRollBars
                                         : nullptr;
        if (field == nullptr)
        {
            return fail("unknown count-in setting \"" + std::string{name} + "\"");
        }
        const auto number = parseInteger(*value, 0, 999);
        if (!number)
        {
            return fail(std::string{name} + " should be a whole number from 0 to 999");
        }
        *field = static_cast<int>(*number);
    }
    return true;
}

bool Reader::readTrack(std::span<const Token> tokens)
{
    if (tokens.size() < 3 || !tokens[2].quoted)
    {
        return fail("a track line needs its number and a name in quotes");
    }
    const auto number = parseInteger(tokens[1].text, 1, std::numeric_limits<std::int64_t>::max());
    if (!number)
    {
        return fail("a track's number should be a whole number from 1");
    }
    const TrackId id{static_cast<std::uint64_t>(*number)};
    if (std::ranges::any_of(tracks_, [id](const Track& track) { return track.id() == id; }))
    {
        return fail("there's already a track " + tokens[1].text);
    }
    Track track{id};
    track.name = tokens[2].text;
    for (const auto& token : tokens.subspan(3))
    {
        const auto [name, value] = settingOf(token);
        const auto signedValue = [&](std::int64_t limit, std::string_view suffix)
        {
            auto text = *value;
            if (text.ends_with(suffix))
            {
                text.remove_suffix(suffix.size());
            }
            return parseInteger(text, -limit, limit);
        };
        if (!value && (name == "mute" || name == "solo" || name == "arm"))
        {
            (name == "mute" ? track.mute : name == "solo" ? track.solo : track.arm) = true;
        }
        else if (value && name == "ch")
        {
            const auto channel = parseInteger(*value, Track::kMinChannel, Track::kMaxChannel);
            if (!channel)
            {
                return fail("ch should be a channel from 1 to 16");
            }
            track.channel = static_cast<int>(*channel);
        }
        else if (value && name == "port")
        {
            track.port = std::string{*value};
        }
        else if (value && (name == "thru" || name == "chase"))
        {
            if (*value != "on" && *value != "off")
            {
                return fail(std::string{name} + " should be on or off");
            }
            (name == "thru" ? track.thru : track.chaseNotes) = *value == "on";
        }
        else if (value && (name == "transpose" || name == "velocity"))
        {
            const auto limit =
                name == "transpose" ? Track::kMaxTranspose : Track::kMaxVelocityOffset;
            const auto amount = signedValue(limit, "");
            if (!amount)
            {
                return fail(std::string{name} + " should be from -" + std::to_string(limit) +
                            " to +" + std::to_string(limit));
            }
            (name == "transpose" ? track.transpose : track.velocityOffset) =
                static_cast<int>(*amount);
        }
        else if (value && name == "offset")
        {
            const auto amount = signedValue(Track::kMaxTimeOffset.count(), "ms");
            if (!amount || !value->ends_with("ms"))
            {
                return fail("offset should be from -1000ms to +1000ms");
            }
            track.timeOffset = std::chrono::milliseconds{*amount};
        }
        else
        {
            return fail("unknown track setting \"" + token.text + "\"");
        }
    }
    tracks_.push_back(std::move(track));
    return true;
}

bool Reader::readSection(std::span<const Token> tokens)
{
    if (section_ && !closeSection())
    {
        return false;
    }
    if (tokens.size() < 2 || !tokens[1].quoted)
    {
        return fail("a section line needs a name in quotes");
    }
    OpenSection section;
    section.line = line_;
    section.name = tokens[1].text;
    for (const auto& token : tokens.subspan(2))
    {
        const auto [name, value] = settingOf(token);
        if (value && name == "bars")
        {
            const auto bars = parseInteger(*value, 1, std::numeric_limits<std::int32_t>::max());
            if (!bars)
            {
                return fail("bars should be a whole number from 1");
            }
            section.bars = *bars;
        }
        else if (value && name == "colour")
        {
            section.colour = std::string{*value};
        }
        else
        {
            return fail("unknown section setting \"" + token.text + "\"");
        }
    }
    if (section.bars == 0)
    {
        return fail("a section line needs its length, as bars=N");
    }
    if (std::ranges::any_of(sections_, [&](const Section& s) { return s.name() == section.name; }))
    {
        return fail("there's already a section \"" + section.name + "\"");
    }
    section_ = std::move(section);
    stage_ = Stage::Sections;
    return true;
}

bool Reader::readMeter(std::span<const Token> tokens)
{
    if (tokens.size() != 3)
    {
        return fail("a meter line is a position and a time signature, as meter 1:1:0 4/4");
    }
    const auto newSignature = signature(tokens[2].text);
    if (!newSignature)
    {
        return false;
    }

    auto& meter = block_ ? block_->meter : section_->meter;
    if (block_ ? !block_->events.empty() : section_->meterClosed)
    {
        return fail("meter lines come first");
    }
    if (!meter)
    {
        if (tokens[1].text != "1:1:0")
        {
            return fail("the first meter line should be at 1:1:0");
        }
        meter.emplace(ppqn_, *newSignature);
        return true;
    }
    const auto tick = tickAt(*meter, tokens[1].text);
    if (!tick)
    {
        return false;
    }
    if (*tick <= meter->changes().back().tick)
    {
        return fail("meter lines should be in order");
    }
    if (meter->set(*tick, *newSignature) != *tick)
    {
        return fail("a meter can't change there");
    }
    return true;
}

bool Reader::readTempo(std::span<const Token> tokens)
{
    if (block_ || section_->section)
    {
        return fail("tempo lines come before the section's tracks");
    }
    if (tokens.size() != 3)
    {
        return fail("a tempo line is a position and the BPM, as tempo 1:1:0 120");
    }
    if (!section_->meter)
    {
        return fail("the section's meter comes before its tempo");
    }
    section_->meterClosed = true;
    const auto tick = tickAt(*section_->meter, tokens[1].text);
    if (!tick)
    {
        return false;
    }
    double bpm = 0;
    const auto& text = tokens[2].text;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), bpm);
    if (error != std::errc{} || end != text.data() + text.size() || !(bpm >= 1 && bpm <= 1000))
    {
        return fail("a tempo should be from 1 to 1000 BPM");
    }
    section_->tempo.push_back({{line_, *tick}, Tempo::fromBpm(bpm)});
    return true;
}

bool Reader::readMarker(std::span<const Token> tokens)
{
    if (block_ || section_->section)
    {
        return fail("marker lines come before the section's tracks");
    }
    if (tokens.size() != 3 || !tokens[2].quoted)
    {
        return fail("a marker line is a position and a name in quotes");
    }
    if (!section_->meter)
    {
        return fail("the section's meter comes before its markers");
    }
    section_->meterClosed = true;
    const auto tick = tickAt(*section_->meter, tokens[1].text);
    if (!tick)
    {
        return false;
    }
    section_->markers.push_back({{line_, *tick}, tokens[2].text});
    return true;
}

bool Reader::readBlock(std::span<const Token> tokens)
{
    if (!closeConductor() || !closeBlock())
    {
        return false;
    }
    if (tokens.size() != 2)
    {
        return fail("a track line in a section is just its number");
    }
    const auto number = parseInteger(tokens[1].text, 1, std::numeric_limits<std::int64_t>::max());
    const auto it = std::ranges::find_if(
        tracks_, [&](const Track& track)
        { return number && track.id().value == static_cast<std::uint64_t>(*number); });
    if (it == tracks_.end())
    {
        return fail("there's no track " + tokens[1].text);
    }
    if (!section_->blocks.insert(it->id()).second)
    {
        return fail("track " + tokens[1].text + " comes twice in this section");
    }
    block_.emplace(OpenBlock{it->id(), std::nullopt, {}});
    return true;
}

bool Reader::readEvent(std::span<const Token> tokens)
{
    if (!block_)
    {
        return fail("an event belongs in a track, in a section");
    }
    if (tokens.size() < 2)
    {
        return fail("an event needs its type after its position");
    }
    const auto& meter = block_->meter ? *block_->meter : section_->section->meter();
    const auto tick = tickAt(meter, tokens[0].text);
    if (!tick)
    {
        return false;
    }
    if (*tick >= section_->section->length())
    {
        return fail("the event is past the end of the section");
    }
    auto data = eventData(tokens.subspan(1));
    if (!data)
    {
        return false;
    }
    Event event{*tick, std::move(*data)};
    if (!isValid(event))
    {
        return fail("the event has a value out of range");
    }
    block_->events.insert(std::move(event));
    return true;
}

std::optional<EventData> Reader::eventData(std::span<const Token> tokens)
{
    const auto& type = tokens[0].text;
    const auto values = tokens.subspan(1);
    const auto count = [&](std::size_t n, std::string_view form)
    {
        if (values.size() != n)
        {
            fail("a " + type + " event is " + std::string{form});
            return false;
        }
        return true;
    };

    if (type == "note")
    {
        if (values.empty())
        {
            fail("a note needs its pitch");
            return std::nullopt;
        }
        const auto notePitch = pitch(values[0].text);
        if (!notePitch)
        {
            return std::nullopt;
        }
        std::optional<std::uint8_t> velocity;
        std::optional<Tick> duration;
        std::uint8_t release = 64;
        for (const auto& token : values.subspan(1))
        {
            const auto [name, value] = settingOf(token);
            if (value && name == "v")
            {
                const auto v = parseInteger(*value, 1, 127);
                if (!v)
                {
                    fail("a note's velocity should be from 1 to 127");
                    return std::nullopt;
                }
                velocity = static_cast<std::uint8_t>(*v);
            }
            else if (value && name == "len")
            {
                duration = length(*value);
                if (!duration)
                {
                    return std::nullopt;
                }
            }
            else if (value && name == "rel")
            {
                const auto r = byteValue(*value, "a release velocity");
                if (!r)
                {
                    return std::nullopt;
                }
                release = *r;
            }
            else
            {
                fail("unknown note setting \"" + token.text + "\"");
                return std::nullopt;
            }
        }
        if (!velocity || !duration)
        {
            fail("a note needs its velocity and length, as v=100 len=1/8");
            return std::nullopt;
        }
        return Note{*notePitch, *velocity, *duration, release};
    }
    if (type == "cc")
    {
        if (!count(2, "a controller and a value"))
        {
            return std::nullopt;
        }
        const auto controller = byteValue(values[0].text, "a controller");
        const auto value = controller ? byteValue(values[1].text, "a value") : std::nullopt;
        return value ? std::optional<EventData>{ControlChange{*controller, *value}} : std::nullopt;
    }
    if (type == "program" || type == "pressure")
    {
        if (!count(1, "a value"))
        {
            return std::nullopt;
        }
        const auto value = byteValue(values[0].text, "a value");
        if (!value)
        {
            return std::nullopt;
        }
        return type == "program" ? EventData{ProgramChange{*value}}
                                 : EventData{ChannelPressure{*value}};
    }
    if (type == "bend")
    {
        if (!count(1, "a value from -8192 to 8191"))
        {
            return std::nullopt;
        }
        const auto value = parseInteger(values[0].text, PitchBend::kMin, PitchBend::kMax);
        if (!value)
        {
            fail("a bend should be from -8192 to 8191");
            return std::nullopt;
        }
        return PitchBend{static_cast<std::int16_t>(*value)};
    }
    if (type == "polypressure")
    {
        if (!count(2, "a pitch and a value"))
        {
            return std::nullopt;
        }
        const auto notePitch = pitch(values[0].text);
        const auto value = notePitch ? byteValue(values[1].text, "a value") : std::nullopt;
        return value ? std::optional<EventData>{PolyPressure{*notePitch, *value}} : std::nullopt;
    }
    if (type == "sysex")
    {
        std::vector<std::uint8_t> bytes;
        for (const auto& token : values)
        {
            unsigned value = 0;
            const auto& text = token.text;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value, 16);
            if (text.size() != 2 || error != std::errc{} || end != text.data() + text.size())
            {
                fail("SysEx is bytes in hex, as F0 7E 7F 09 01 F7");
                return std::nullopt;
            }
            bytes.push_back(static_cast<std::uint8_t>(value));
        }
        return makeSysEx(std::move(bytes));
    }
    const auto kind = std::ranges::find(kTextKinds, std::string_view{type},
                                        &std::pair<MetaText::Kind, std::string_view>::second);
    if (kind != kTextKinds.end())
    {
        if (values.size() != 1 || !values[0].quoted)
        {
            fail("a " + type + " event is text in quotes");
            return std::nullopt;
        }
        return makeMetaText(kind->first, values[0].text);
    }
    fail("unknown event type \"" + type + "\"");
    return std::nullopt;
}

bool Reader::closeConductor()
{
    auto& open = *section_;
    if (open.section)
    {
        return true;
    }
    if (!open.meter)
    {
        return failAt(open.line, "the section has no meter");
    }
    std::ranges::stable_sort(open.tempo, {}, [](const auto& t) { return t.first.tick; });
    std::ranges::stable_sort(open.markers, {}, [](const auto& m) { return m.first.tick; });
    if (open.tempo.empty() || open.tempo.front().first.tick != 0)
    {
        return failAt(open.line, "the section has no tempo at 1:1:0");
    }

    const Tick length = open.meter->barStart(open.bars + 1);
    Section section{open.name, length, std::move(*open.meter), open.tempo.front().second};
    section.setColour(std::move(open.colour));
    for (std::size_t i = 1; i < open.tempo.size(); ++i)
    {
        const auto& [at, tempo] = open.tempo[i];
        if (at.tick >= length)
        {
            return failAt(at.line, "the tempo change is past the end of the section");
        }
        if (at.tick == open.tempo[i - 1].first.tick)
        {
            return failAt(at.line, "there's already a tempo change there");
        }
        section.addTempo({at.tick, tempo});
    }
    for (std::size_t i = 0; i < open.markers.size(); ++i)
    {
        const auto& [at, name] = open.markers[i];
        if (at.tick >= length)
        {
            return failAt(at.line, "the marker is past the end of the section");
        }
        if (i > 0 && at.tick == open.markers[i - 1].first.tick)
        {
            return failAt(at.line, "there's already a marker there");
        }
        if (std::ranges::any_of(section.markers(), [&](const Marker& m) { return m.name == name; }))
        {
            return failAt(at.line, "there's already a marker \"" + name + "\" in this section");
        }
        section.addMarker({at.tick, name});
    }
    open.section = std::move(section);
    return true;
}

bool Reader::closeBlock()
{
    if (block_)
    {
        section_->section->setContent(block_->id,
                                      {std::move(block_->meter), std::move(block_->events)});
        block_.reset();
    }
    return true;
}

bool Reader::closeSection()
{
    if (!closeConductor() || !closeBlock())
    {
        return false;
    }
    sections_.push_back(std::move(*section_->section));
    section_.reset();
    return true;
}

std::optional<Tick> Reader::tickAt(const MeterMap& meter, std::string_view text)
{
    const auto position = parsePosition(text);
    if (!position)
    {
        fail("a position should be bar:beat:tick, as 3:1:0");
        return std::nullopt;
    }
    const Tick start = meter.barStart(position->bar);
    const auto signature = meter.signatureAt(start);
    if (position->beat > signature.numerator)
    {
        fail("bar " + std::to_string(position->bar) + " has only " +
             std::to_string(signature.numerator) + " beats");
        return std::nullopt;
    }
    const Tick beat = signature.beatLength().toTicks(ppqn_);
    if (position->tick >= beat)
    {
        fail("a beat in bar " + std::to_string(position->bar) + " has only " +
             std::to_string(beat) + " ticks");
        return std::nullopt;
    }
    return start + (position->beat - 1) * beat + position->tick;
}

std::optional<TimeSignature> Reader::signature(std::string_view text)
{
    const auto slash = text.find('/');
    const auto numerator = parseInteger(text.substr(0, slash), 1, TimeSignature::kMaxNumerator);
    const auto denominator =
        slash == std::string_view::npos
            ? std::nullopt
            : parseInteger(text.substr(slash + 1), 1, TimeSignature::kMaxDenominator);
    const TimeSignature result{static_cast<std::int32_t>(numerator.value_or(0)),
                               static_cast<std::int32_t>(denominator.value_or(0))};
    if (!numerator || !denominator || !result.isValid() || text.find('+') != std::string_view::npos)
    {
        fail("a time signature should be as 4/4, up to 255 over a power of two up to 64");
        return std::nullopt;
    }
    if (!result.beatLength().isExact(ppqn_))
    {
        fail("a " + std::string{text} + " beat isn't a whole number of ticks at " +
             std::to_string(ppqn_.value) + " PPQN");
        return std::nullopt;
    }
    return result;
}

std::optional<std::uint8_t> Reader::byteValue(std::string_view text, std::string_view what)
{
    const auto value = parseInteger(text, 0, 127);
    if (!value)
    {
        fail(std::string{what} + " should be from 0 to 127");
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::optional<std::uint8_t> Reader::pitch(std::string_view text)
{
    const auto result = parsePitch(text);
    if (!result)
    {
        fail("\"" + std::string{text} + "\" isn't a pitch from C-1 to G9");
    }
    return result;
}

std::optional<Tick> Reader::length(std::string_view text)
{
    if (text.find('/') != std::string_view::npos)
    {
        const auto fraction = parseNoteFraction(text);
        if (!fraction || !fraction->isExact(ppqn_))
        {
            fail("\"" + std::string{text} + "\" isn't a length at " + std::to_string(ppqn_.value) +
                 " PPQN");
            return std::nullopt;
        }
        return fraction->toTicks(ppqn_);
    }
    const auto ticks = parseInteger(text, 0, std::numeric_limits<std::int32_t>::max());
    if (!ticks || text[0] == '+')
    {
        fail("a length should be a note length, as 1/8, or ticks");
        return std::nullopt;
    }
    return *ticks;
}

// A song's sections at another resolution. Bar lines, and so section boundaries, land
// exactly; positions within them round to the nearest tick, and a note's end rounds
// separately from its start, so notes that touched still touch.
std::optional<Song> rescaled(const Song& song, Ppqn to, std::string& error)
{
    const Ppqn from = song.ppqn();
    const auto scale = [&](Tick tick)
    { return (2 * tick * to.value + from.value) / (2 * from.value); };
    const auto meterAt = [&](const MeterMap& meter) -> std::optional<MeterMap>
    {
        MeterMap result{to, meter.changes().front().signature};
        for (const auto& change : meter.changes())
        {
            if (!change.signature.beatLength().isExact(to))
            {
                error = "a " + signatureText(change.signature) +
                        " beat isn't a whole number of ticks at " + std::to_string(to.value) +
                        " PPQN";
                return std::nullopt;
            }
            result.set(scale(change.tick), change.signature);
        }
        return result;
    };

    std::vector<Track> tracks;
    for (const auto& track : song.tracks())
    {
        tracks.push_back(*track);
    }
    std::vector<Section> sections;
    for (const auto& old : song.sections())
    {
        auto meter = meterAt(old->meter());
        if (!meter)
        {
            return std::nullopt;
        }
        const Tick length = scale(old->length());
        Section section{old->name(), length, std::move(*meter), old->tempo().front().tempo};
        section.setColour(old->colour());
        for (const auto& change : old->tempo().subspan(1))
        {
            const Tick tick = scale(change.tick);
            if (tick > section.tempo().back().tick)
            {
                section.addTempo({tick, change.tempo});
            }
        }
        for (const auto& marker : old->markers())
        {
            const Tick tick = scale(marker.tick);
            if (!section.markers().empty() && tick <= section.markers().back().tick)
            {
                error = "two markers fall on one tick at " + std::to_string(to.value) + " PPQN";
                return std::nullopt;
            }
            section.addMarker({tick, marker.name});
        }
        for (const auto& track : song.tracks())
        {
            const auto* content = old->content(track->id());
            if (content == nullptr)
            {
                continue;
            }
            TrackContent scaled;
            if (content->meter)
            {
                scaled.meter = meterAt(*content->meter);
                if (!scaled.meter)
                {
                    return std::nullopt;
                }
            }
            for (auto event : content->events.all())
            {
                const Tick start = event.tick;
                event.tick = std::min(scale(start), length - 1);
                if (auto* note = std::get_if<Note>(&event.data))
                {
                    note->duration = scale(start + note->duration) - scale(start);
                }
                scaled.events.insert(std::move(event));
            }
            section.setContent(track->id(), std::move(scaled));
        }
        sections.push_back(std::move(section));
    }
    Song result{to, std::move(tracks), std::move(sections)};
    result.title = song.title;
    result.metronome = song.metronome;
    result.countIn = song.countIn;
    return result;
}

} // namespace

std::string writeSong(const Song& song)
{
    const auto ppqn = song.ppqn();
    std::string out;
    const auto line = [&](int indent, const std::string& text)
    {
        out.append(static_cast<std::size_t>(indent) * 2, ' ');
        out += text;
        out += '\n';
    };

    line(0, "modal-song " + std::to_string(kVersion));
    line(0, "song " + quotedText(song.title) + " ppqn=" + std::to_string(ppqn.value));

    const auto& metronome = song.metronome;
    std::string metronomeLine =
        "metronome when=" + std::string{kWhens[static_cast<std::size_t>(metronome.when)]} +
        " ch=" + std::to_string(metronome.channel) +
        " downbeat=" + pitchName(metronome.downbeatNote) +
        " downbeatvel=" + std::to_string(metronome.downbeatVelocity) +
        " beat=" + pitchName(metronome.beatNote) +
        " beatvel=" + std::to_string(metronome.beatVelocity) + " len=" + toString(metronome.length);
    if (!metronome.port.empty())
    {
        metronomeLine += " port=" + quotedText(metronome.port);
    }
    line(0, metronomeLine);
    const auto& countIn = song.countIn;
    line(0, "countin mode=" + std::string{kModes[static_cast<std::size_t>(countIn.mode)]} +
                " bars=" + std::to_string(countIn.bars) + " beats=" +
                std::to_string(countIn.beats) + " preroll=" + std::to_string(countIn.preRollBars));

    if (!song.tracks().empty())
    {
        line(0, "");
    }
    for (const auto& track : song.tracks())
    {
        std::string text = "track " + std::to_string(track->id().value) + " " +
                           quotedText(track->name) + " ch=" + std::to_string(track->channel);
        if (!track->port.empty())
        {
            text += " port=" + quotedText(track->port);
        }
        text += track->mute ? " mute" : "";
        text += track->solo ? " solo" : "";
        text += track->arm ? " arm" : "";
        text += track->thru ? "" : " thru=off";
        text += track->chaseNotes ? "" : " chase=off";
        if (track->transpose != 0)
        {
            text += " transpose=" + signedText(track->transpose);
        }
        if (track->velocityOffset != 0)
        {
            text += " velocity=" + signedText(track->velocityOffset);
        }
        if (track->timeOffset.count() != 0)
        {
            text += " offset=" + signedText(track->timeOffset.count()) + "ms";
        }
        line(0, text);
    }

    const EventText eventText{ppqn};
    for (const auto& section : song.sections())
    {
        const auto& meter = section->meter();
        const auto bars = meter.toBarBeatTick(section->length() - 1).bar;
        std::string text =
            "section " + quotedText(section->name()) + " bars=" + std::to_string(bars);
        if (section->colour())
        {
            text += " colour=" + quotedText(*section->colour());
        }
        line(0, "");
        line(0, text);
        for (const auto& change : meter.changes())
        {
            line(1, "meter " + positionText(meter, change.tick) + " " +
                        signatureText(change.signature));
        }
        for (const auto& change : section->tempo())
        {
            line(1, "tempo " + positionText(meter, change.tick) + " " + bpmText(change.tempo));
        }
        for (const auto& marker : section->markers())
        {
            line(1, "marker " + positionText(meter, marker.tick) + " " + quotedText(marker.name));
        }
        for (const auto& track : song.tracks())
        {
            const auto* content = section->content(track->id());
            if (content == nullptr)
            {
                continue;
            }
            line(1, "track " + std::to_string(track->id().value));
            const auto& trackMeter = content->meter ? *content->meter : meter;
            if (content->meter)
            {
                for (const auto& change : content->meter->changes())
                {
                    line(2, "meter " + positionText(trackMeter, change.tick) + " " +
                                signatureText(change.signature));
                }
            }
            for (const auto& event : content->events.all())
            {
                line(2, positionText(trackMeter, event.tick) + " " +
                            std::visit(eventText, event.data));
            }
        }
    }
    return out;
}

SongReadResult readSong(std::string_view text, Ppqn ppqn)
{
    auto result = Reader{}.read(text);
    if (result.song && result.song->ppqn() != ppqn)
    {
        auto song = rescaled(*result.song, ppqn, result.error);
        result.song = std::move(song);
    }
    return result;
}

std::string writeSongFile(const Song& song, const std::filesystem::path& path)
{
    const auto text = writeSong(song);
    return writeFileAtomically(path,
                               {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

SongReadResult readSongFile(const std::filesystem::path& path, Ppqn ppqn)
{
    SongReadResult result;
    const auto bytes = readFile(path, kMaxFileSize, "a song", result.error);
    if (!result.error.empty())
    {
        return result;
    }
    result = readSong({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, ppqn);
    if (!result.error.empty())
    {
        result.error = path.string() + ", " + result.error;
    }
    return result;
}

} // namespace modal::core
