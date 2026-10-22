#include "support/edit_track.hpp"
#include "support/ppqn.hpp"

#include <modal/core/smf.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using Bytes = std::vector<std::uint8_t>;

namespace
{

Bytes vlq(std::uint64_t value)
{
    Bytes groups;
    do
    {
        groups.insert(groups.begin(), static_cast<std::uint8_t>(value & 0x7F));
        value >>= 7;
    } while (value != 0);
    for (std::size_t i = 0; i + 1 < groups.size(); ++i)
    {
        groups[i] |= 0x80;
    }
    return groups;
}

Bytes concat(std::initializer_list<Bytes> parts)
{
    Bytes result;
    for (const auto& part : parts)
    {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

Bytes chunk(std::string_view id, const Bytes& data)
{
    const auto size = static_cast<std::uint32_t>(data.size());
    Bytes result{id.begin(), id.end()};
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        result.push_back(static_cast<std::uint8_t>(size >> shift));
    }
    std::ranges::copy(data, std::back_inserter(result));
    return result;
}

// The tracks' chunks, after the 14-byte header.
Bytes tracksOf(const Bytes& file)
{
    return {file.begin() + 14, file.end()};
}

TrackId addTrack(Song& song, std::string name, int channel,
                 const std::function<void(EditTrack&)>& edit)
{
    const auto id = song.addTrack(std::move(name));
    editTrack(song, id,
              [&](EditTrack& track)
              {
                  track.channel = channel;
                  edit(track);
              });
    return id;
}

std::vector<Event> eventsOf(const Song& song, const Track& track)
{
    const auto& events = song.events(track.id());
    return {events.all().begin(), events.all().end()};
}

Song reimported(const Bytes& bytes)
{
    auto result = importSmf(bytes);
    INFO("error: " << result.error);
    REQUIRE(result.song);
    CHECK(result.warnings.empty());
    return std::move(*result.song);
}

} // namespace

TEST_CASE("an exported song reads back the same")
{
    const Tick q = kPpqn.quarter();
    Song song{kPpqn};
    song.title = "Song";
    song.updateTempo([&](TempoMap& tempo) { tempo.set(0, Tempo{500'000}); });
    song.updateTempo([&](TempoMap& tempo) { tempo.set(8 * q, Tempo::fromBpm(90)); });
    song.setMarker(4 * q, "Verse");
    song.setMarker(10 * q, "Chorus");
    song.updateMeter([&](MeterMap& meter) { meter.set(4 * q, {3, 4}); });

    addTrack(song, "Piano", 1,
             [&](EditTrack& track)
             {
                 track.events.insert({0, ProgramChange{5}});
                 track.events.insert({0, ControlChange{7, 100}});
                 track.events.insert({0, Note{60, 100, q, 30}});
                 track.events.insert({0, Note{64, 90, 2 * q}});
                 track.events.insert({q, PitchBend{PitchBend::kMin}});
                 track.events.insert({q, PitchBend{PitchBend::kMax}});
                 track.events.insert({q, ChannelPressure{40}});
                 track.events.insert({q, PolyPressure{64, 50}});
                 track.events.insert({2 * q, makeSysEx({0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7})});
                 track.events.insert({2 * q, makeMetaText(MetaText::Kind::Lyric, "la")});
                 track.events.insert({20 * q, Note{72, 100, q}});
             });
    addTrack(song, "Bass", 2,
             [&](EditTrack& track)
             {
                 track.events.insert({0, Note{36, 100, q}});
                 track.events.insert({q, Note{36, 80, q}}); // touching the one before
                 track.events.insert({2 * q, Note{38, 70, 0}});
                 track.events.insert({2 * q, ControlChange{64, 127}});
                 track.events.insert({3 * q, ControlChange{64, 0}});
             });

    const auto result = exportSmf(song);
    REQUIRE(result.error.empty());
    CHECK(result.warnings.empty());
    const Song back = reimported(result.bytes);

    CHECK(back.title == "Song");
    CHECK(back.tempo().changes().size() == 2);
    CHECK(std::ranges::equal(back.tempo().changes(), song.tempo().changes()));
    CHECK(std::ranges::equal(back.markers(), song.markers()));
    REQUIRE(back.tracks().size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& original = *song.tracks()[i];
        const auto& copy = *back.tracks()[i];
        CHECK(copy.name == original.name);
        CHECK(copy.channel == original.channel);
        CHECK(back.meter(copy.id()) == song.meter(original.id()));
        CHECK(eventsOf(back, copy) == eventsOf(song, original));
    }
}

TEST_CASE("sections are written as markers, and bookmarks as cue points")
{
    const Tick bar = kPpqn.whole();
    Song song{kPpqn};
    addTrack(song, "Piano", 1,
             [&](EditTrack& track) { track.events.insert({0, Note{60, 100, bar}}); });
    song.setMarker(bar, "fix this");

    // A song of one section has no structure to show.
    const auto meta = [](std::uint8_t type, std::string_view text)
    {
        Bytes bytes{0xFF, type, static_cast<std::uint8_t>(text.size())};
        bytes.insert(bytes.end(), text.begin(), text.end());
        return bytes;
    };
    const auto contains = [](const Bytes& bytes, const Bytes& part)
    { return !std::ranges::search(bytes, part).empty(); };
    auto result = exportSmf(song);
    CHECK_FALSE(contains(result.bytes, meta(0x06, "Section 1")));
    CHECK(contains(result.bytes, meta(0x07, "fix this")));

    song.splitSection(4 * bar, "Verse");
    result = exportSmf(song);
    CHECK(contains(result.bytes, meta(0x06, "Section 1")));
    CHECK(contains(result.bytes, meta(0x06, "Verse")));

    // Read back, the section starts are bookmarks too, until they can become sections.
    const Song back = reimported(result.bytes);
    CHECK(std::ranges::equal(
        back.markers(),
        std::vector<Marker>{{0, "Section 1"}, {bar, "fix this"}, {4 * bar, "Verse"}}));
}

TEST_CASE("at each tick come note-offs, other events, note-ons, then zero-length note-offs")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    addTrack(song, "T", 3,
             [&](EditTrack& track)
             {
                 track.events.insert({0, Note{60, 100, q}});
                 track.events.insert({q, Note{60, 90, q}});
                 track.events.insert({q, ControlChange{7, 100}});
                 track.events.insert({2 * q, Note{62, 80, 0}});
             });

    const auto result = exportSmf(song);
    REQUIRE(result.error.empty());
    const auto& file = result.bytes;
    CHECK(Bytes(file.begin(), file.begin() + 14) ==
          Bytes{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2,
                static_cast<std::uint8_t>(ppqn.value >> 8), static_cast<std::uint8_t>(ppqn.value)});

    const auto conductor = chunk("MTrk", concat({
                                             {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20},
                                             {0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 24, 8},
                                             vlq(2 * q), // ends with the song
                                             {0xFF, 0x2F, 0x00},
                                         }));
    const auto track = chunk("MTrk", concat({
                                         {0x00, 0xFF, 0x03, 0x01, 'T'},
                                         {0x00, 0xFF, 0x20, 0x01, 0x02}, // channel prefix
                                         {0x00, 0x92, 60, 100},
                                         vlq(q),
                                         {0x82, 60, 64},
                                         {0x00, 0xB2, 7, 100},
                                         {0x00, 0x92, 60, 90},
                                         vlq(q),
                                         {0x82, 60, 64},
                                         {0x00, 0x92, 62, 80},
                                         {0x00, 0x82, 62, 64},
                                         {0x00, 0xFF, 0x2F, 0x00},
                                     }));
    CHECK(tracksOf(file) == concat({conductor, track}));
}

TEST_CASE("running status is used between channel messages, but not across meta events")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    addTrack(song, "", 1,
             [&](EditTrack& track)
             {
                 track.events.insert({0, ControlChange{1, 10}});
                 track.events.insert({0, ControlChange{1, 20}});
                 track.events.insert({0, makeMetaText(MetaText::Kind::Text, "x")});
                 track.events.insert({0, ControlChange{1, 30}});
                 track.events.insert({q, ControlChange{1, 40}});
             });

    const auto result = exportSmf(song);
    REQUIRE(result.error.empty());
    const auto track = chunk("MTrk", concat({
                                         {0x00, 0xFF, 0x20, 0x01, 0x00}, // no name
                                         {0x00, 0xB0, 1, 10},
                                         {0x00, 1, 20},
                                         {0x00, 0xFF, 0x01, 0x01, 'x'},
                                         {0x00, 0xB0, 1, 30},
                                         vlq(q),
                                         {1, 40},
                                         {0x00, 0xFF, 0x2F, 0x00},
                                     }));
    const auto tracks = tracksOf(result.bytes);
    CHECK(Bytes(tracks.end() - static_cast<std::ptrdiff_t>(track.size()), tracks.end()) == track);
}

TEST_CASE("a type 0 export puts everything in one track")
{
    const Tick q = kPpqn.quarter();
    Song song{kPpqn};
    song.title = "Song";
    addTrack(song, "Piano", 1,
             [&](EditTrack& track) { track.events.insert({0, Note{60, 100, q}}); });
    addTrack(song, "Bass", 2,
             [&](EditTrack& track) { track.events.insert({q, Note{36, 100, 2 * q}}); });

    const auto result = exportSmf(song, SmfType::Type0);
    REQUIRE(result.error.empty());
    const auto& file = result.bytes;
    CHECK(file[9] == 0);  // type 0
    CHECK(file[11] == 1); // one track

    // Import splits the one track by channel.
    const Song back = reimported(file);
    CHECK(back.title == "Song");
    REQUIRE(back.tracks().size() == 2);
    CHECK(back.tracks()[0]->channel == 1);
    CHECK(eventsOf(back, *back.tracks()[0]) == eventsOf(song, *song.tracks()[0]));
    CHECK(back.tracks()[1]->channel == 2);
    CHECK(eventsOf(back, *back.tracks()[1]) == eventsOf(song, *song.tracks()[1]));
}

TEST_CASE("each track ends at its last event, and the first track at the song's end")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    addTrack(song, "", 1,
             [&](EditTrack& track) { track.events.insert({0, Note{60, 100, 5 * q}}); });
    addTrack(song, "", 1, [&](EditTrack& track) { track.events.insert({q, ControlChange{7, 1}}); });

    const auto result = exportSmf(song);
    REQUIRE(result.error.empty());
    const auto tracks = tracksOf(result.bytes);
    const auto conductor = chunk("MTrk", concat({
                                             {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20},
                                             {0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 24, 8},
                                             vlq(5 * q),
                                             {0xFF, 0x2F, 0x00},
                                         }));
    CHECK(Bytes(tracks.begin(), tracks.begin() + static_cast<std::ptrdiff_t>(conductor.size())) ==
          conductor);
    const auto last = chunk("MTrk", concat({
                                        {0x00, 0xFF, 0x20, 0x01, 0x00},
                                        vlq(q),
                                        {0xB0, 7, 1},
                                        {0x00, 0xFF, 0x2F, 0x00},
                                    }));
    CHECK(Bytes(tracks.end() - static_cast<std::ptrdiff_t>(last.size()), tracks.end()) == last);
}

TEST_CASE("export clamps tempos too slow for a MIDI file, and refuses what won't fit")
{
    Song slow{kPpqn};
    slow.updateTempo([&](TempoMap& tempo)
                     { tempo.set(0, Tempo{Tempo::kMaxMicrosPerQuarter}); }); // 1 BPM
    const auto clamped = exportSmf(slow);
    REQUIRE(clamped.error.empty());
    REQUIRE(clamped.warnings.size() == 1);
    CHECK(clamped.warnings[0].find("clamped 1 tempo") != std::string::npos);
    const Bytes tempo{0xFF, 0x51, 0x03, 0xFF, 0xFF, 0xFF};
    CHECK(std::ranges::search(clamped.bytes, tempo).begin() != clamped.bytes.end());

    const auto tooFine = exportSmf(Song{Ppqn{40'000}});
    CHECK_FALSE(tooFine.error.empty());
    CHECK(tooFine.bytes.empty());

    Song gap{kPpqn};
    addTrack(gap, "", 1,
             [](EditTrack& track) { track.events.insert({0x1000'0000, ControlChange{7, 1}}); });
    const auto tooLong = exportSmf(gap);
    CHECK_FALSE(tooLong.error.empty());
    CHECK(tooLong.bytes.empty());
}

TEST_CASE("exporting to a file replaces it whole")
{
    const auto directory = std::filesystem::temp_directory_path();
    const auto path = directory / "modal_smf_export_test.mid";
    {
        std::ofstream old{path, std::ios::binary};
        old << "an older, longer file that the export replaces";
    }
    Song song{kPpqn};
    addTrack(song, "T", 1, [](EditTrack& track) { track.events.insert({0, Note{60, 100, 1}}); });

    const auto result = exportSmfFile(song, path);
    REQUIRE(result.error.empty());
    std::ifstream stream{path, std::ios::binary};
    const Bytes written{std::istreambuf_iterator<char>{stream}, {}};
    stream.close();
    CHECK(written == result.bytes);
    CHECK_FALSE(std::filesystem::exists(directory / "modal_smf_export_test.mid.tmp"));
    std::filesystem::remove(path);

    const auto nowhere = directory / "modal_no_such_directory" / "song.mid";
    const auto failed = exportSmfFile(song, nowhere);
    CHECK_FALSE(failed.error.empty());
    CHECK(failed.bytes.empty());
    CHECK_FALSE(std::filesystem::exists(nowhere));
}
