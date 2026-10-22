#include "support/smf_builder.hpp"

#include <modal/core/smf.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

using namespace modal::core;
using modal::test::makeSmf;
using modal::test::SmfTrack;

namespace
{

Song importOk(const std::vector<std::uint8_t>& bytes)
{
    auto result = importSmf(bytes);
    INFO("error: " << result.error);
    REQUIRE(result.song);
    return std::move(*result.song);
}

const Note& noteAt(const EventList& events, std::size_t index)
{
    return std::get<Note>(events[index].data);
}

bool hasWarning(const SmfImportResult& result, std::string_view fragment)
{
    return std::ranges::any_of(result.warnings, [&](const std::string& warning)
                               { return warning.find(fragment) != std::string::npos; });
}

} // namespace

TEST_CASE("files that aren't MIDI files, or are unsupported, are rejected")
{
    CHECK(importSmf(std::vector<std::uint8_t>{}).error.find("not a Standard MIDI File") == 0);
    CHECK_FALSE(importSmf(std::vector<std::uint8_t>{'R', 'I', 'F', 'F', 0, 0, 0, 6}).song);

    auto truncatedHeader = makeSmf(0, 96, {});
    truncatedHeader.resize(12);
    CHECK_FALSE(importSmf(truncatedHeader).song);

    CHECK(importSmf(makeSmf(2, 96, {})).error.find("type 2") != std::string::npos);
    CHECK(importSmf(makeSmf(0, 0xE728, {})).error.find("SMPTE") != std::string::npos);
    CHECK_FALSE(importSmf(makeSmf(0, 0, {})).song);
}

TEST_CASE("notes become single events with durations, rescaled to kPpqn")
{
    const std::uint16_t division = GENERATE(as<std::uint16_t>{}, 96, 480, 1000, 7);
    const Tick q = kPpqn.quarter();
    const auto rescaled = [&](std::uint32_t fileTicks)
    { return (2 * Tick{fileTicks} * kPpqn.value + division) / (2 * division); };

    SmfTrack track;
    track.text(0, 0x03, "Piano")
        .event(0, {0x90, 60, 100})
        .event(division, {0x80, 60, 40})
        .event(0, {0x90, 64, 90})
        .event(division / 2, {0x90, 64, 0}) // velocity 0 is a note-off
        .end();
    const auto song = importOk(makeSmf(0, division, {track}));

    INFO("division " << division);
    REQUIRE(song.tracks().size() == 1);
    CHECK(song.tracks()[0]->name == "Piano");
    CHECK(song.tracks()[0]->channel == 1);
    const auto& piano = song.events(song.tracks()[0]->id());
    REQUIRE(piano.size() == 2);

    CHECK(piano[0].tick == 0);
    CHECK(noteAt(piano, 0) == Note{60, 100, rescaled(division), 40});
    CHECK(piano[1].tick == rescaled(division));
    CHECK(noteAt(piano, 1).pitch == 64);
    CHECK(noteAt(piano, 1).releaseVelocity == 64);
    // Start and end are rescaled separately, so notes that touch still touch.
    CHECK(piano[1].tick + noteAt(piano, 1).duration == rescaled(division + division / 2));
    if (division % 4 == 0)
    {
        CHECK(noteAt(piano, 0).duration == q);
    }
}

TEST_CASE("running status")
{
    SmfTrack track;
    track.event(0, {0x91, 60, 100})
        .raw({0, 62, 100}) // running status: another note-on
        .raw({96, 60, 0})  // running status: velocity 0, a note-off
        .raw({0, 62, 0})
        .text(0, 0x01, "between")
        .raw({0, 64, 100}) // running status carries on past a meta event
        .raw({96, 64, 0})
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    CHECK(song.tracks()[0]->channel == 2);
    const auto& t = song.events(song.tracks()[0]->id());
    REQUIRE(t.size() == 4);
    CHECK(noteAt(t, 0).pitch == 60);
    CHECK(noteAt(t, 1).pitch == 62);
    CHECK(noteAt(t, 1).duration == kPpqn.quarter());
    CHECK(std::holds_alternative<MetaText>(t[2].data));
    CHECK(noteAt(t, 3).pitch == 64);
}

TEST_CASE("overlapping notes of the same pitch pair first on, first off")
{
    SmfTrack track;
    track.event(0, {0x90, 60, 100})
        .event(96, {0x90, 60, 80}) // a second note-on before the first ends
        .event(96, {0x80, 60, 0})  // ends the first
        .event(96, {0x80, 60, 0})  // ends the second
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    const auto& t = song.events(song.tracks()[0]->id());
    REQUIRE(t.size() == 2);
    const Tick q = kPpqn.quarter();
    CHECK(noteAt(t, 0) == Note{60, 100, 2 * q, 0});
    CHECK(t[1].tick == q);
    CHECK(noteAt(t, 1) == Note{60, 80, 2 * q, 0});
}

TEST_CASE("unpaired note-offs and unfinished notes")
{
    SmfTrack track;
    track
        .event(0, {0x80, 50, 0}) // no matching note-on
        .event(0, {0x90, 60, 100})
        .event(4 * 96, {0xB0, 7, 100})
        .end(); // the note never ends
    const auto result = importSmf(makeSmf(0, 96, {track}));

    REQUIRE(result.song);
    const auto& t = result.song->events(result.song->tracks()[0]->id());
    REQUIRE(t.size() == 2);
    CHECK(noteAt(t, 0).duration == kPpqn.whole());
    CHECK(hasWarning(result, "1 note-off(s) without a note-on"));
    CHECK(hasWarning(result, "1 note(s) never ended"));
}

TEST_CASE("channel messages")
{
    SmfTrack track;
    track.event(0, {0xB0, 7, 100})
        .event(0, {0xC0, 5})
        .event(0, {0xE0, 0x00, 0x40}) // centre
        .event(0, {0xE0, 0x7F, 0x7F}) // top
        .event(0, {0xE0, 0x00, 0x00}) // bottom
        .event(0, {0xD0, 33})
        .event(0, {0xA0, 60, 44})
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    const auto& t = song.events(song.tracks()[0]->id());
    REQUIRE(t.size() == 7);
    CHECK(t[0].data == EventData{ControlChange{7, 100}});
    CHECK(t[1].data == EventData{ProgramChange{5}});
    CHECK(t[2].data == EventData{PitchBend{0}});
    CHECK(t[3].data == EventData{PitchBend{PitchBend::kMax}});
    CHECK(t[4].data == EventData{PitchBend{PitchBend::kMin}});
    CHECK(t[5].data == EventData{ChannelPressure{33}});
    CHECK(t[6].data == EventData{PolyPressure{60, 44}});
}

TEST_CASE("non-note events go before notes at the same tick")
{
    SmfTrack track;
    track.event(0, {0x90, 60, 100})
        .event(0, {0xC0, 5}) // after the note in the file, but must be sent first
        .event(96, {0x80, 60, 0})
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    const auto& t = song.events(song.tracks()[0]->id());
    REQUIRE(t.size() == 2);
    CHECK(std::holds_alternative<ProgramChange>(t[0].data));
    CHECK(t[1].isNote());
}

TEST_CASE("multi-channel tracks split into one track per channel")
{
    SmfTrack track;
    track.text(0, 0x03, "Band")
        .sysex(0, 0xF0, {0x7E, 0x7F, 0x09, 0x01, 0xF7})
        .event(0, {0x99, 36, 100}) // channel 10
        .event(0, {0x90, 48, 90})  // channel 1
        .event(96, {0x89, 36, 0})
        .event(0, {0x80, 48, 0})
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    CHECK(song.title == "Band");
    REQUIRE(song.tracks().size() == 2);
    const auto& first = song.events(song.tracks()[0]->id());
    const auto& tenth = song.events(song.tracks()[1]->id());
    CHECK(song.tracks()[0]->name == "Band (ch 1)");
    CHECK(song.tracks()[0]->channel == 1);
    CHECK(song.tracks()[1]->name == "Band (ch 10)");
    CHECK(song.tracks()[1]->channel == 10);

    // The SysEx goes with the first channel's track.
    REQUIRE(first.size() == 2);
    CHECK(std::holds_alternative<SysEx>(first[0].data));
    CHECK(noteAt(first, 1).pitch == 48);
    REQUIRE(tenth.size() == 1);
    CHECK(noteAt(tenth, 0).pitch == 36);
}

TEST_CASE("type 1 files: conductor track, tempo, meter and markers")
{
    const std::uint16_t division = GENERATE(as<std::uint16_t>{}, 96, 480);
    const std::uint32_t bar = 4 * division;

    SmfTrack conductor;
    conductor.text(0, 0x03, "My Song")
        .tempo(0, 600'000)             // 100 BPM
        .timeSignature(0, 4, 2)        // 4/4
        .text(4 * bar, 0x06, "Chorus") // marker at bar 5
        .timeSignature(0, 3, 2)        // 3/4 from bar 5
        .tempo(0, 500'000)             // 120 BPM
        .end();
    SmfTrack bass;
    bass.text(0, 0x03, "Bass").event(0, {0x92, 40, 100}).event(division, {0x82, 40, 0}).end();
    SmfTrack keys;
    keys.text(0, 0x03, "Keys").event(0, {0x93, 60, 100}).event(division, {0x83, 60, 0}).end();

    const auto song = importOk(makeSmf(1, division, {conductor, bass, keys}));
    INFO("division " << division);

    CHECK(song.title == "My Song");
    REQUIRE(song.tracks().size() == 2); // the conductor track has nothing for a track of its own
    CHECK(song.tracks()[0]->name == "Bass");
    CHECK(song.tracks()[0]->channel == 3);
    CHECK(song.tracks()[1]->name == "Keys");

    const Tick bar5 = 4 * kPpqn.whole();
    REQUIRE(song.tempo().changes().size() == 2);
    CHECK(song.tempo().tempoAt(0) == Tempo{600'000});
    CHECK(song.tempo().tempoAt(bar5) == Tempo{500'000});

    REQUIRE(song.markers().size() == 1);
    CHECK(song.markers()[0] == Marker{bar5, "Chorus"});

    // Time signatures apply to every track.
    for (const auto& track : song.tracks())
    {
        CHECK(song.meter(track->id()).signatureAt(0) == TimeSignature{4, 4});
        CHECK(song.meter(track->id()).signatureAt(bar5) == TimeSignature{3, 4});
        CHECK(song.meter(track->id()).barStart(6) == bar5 + 3 * kPpqn.quarter());
    }
}

TEST_CASE("an imported file is one section, with a conductor track's cue points as bookmarks")
{
    const std::uint16_t division = 96;
    const std::uint32_t bar = 4 * division;
    SmfTrack conductor;
    conductor.text(0, 0x07, "count in").text(2 * bar, 0x07, "fix this").end();
    SmfTrack bass;
    bass.text(0, 0x07, "kept")
        .event(0, {0x90, 40, 100})
        .event(2 * bar + division, {0x80, 40, 0})
        .end();
    const auto song = importOk(makeSmf(1, division, {conductor, bass}));

    REQUIRE(song.tracks().size() == 1); // the cue points don't make a track
    REQUIRE(song.markers().size() == 2);
    CHECK(song.markers()[0] == Marker{0, "count in"});
    CHECK(song.markers()[1] == Marker{2 * kPpqn.whole(), "fix this"});

    // A cue point among a track's notes stays one of its events.
    const auto id = song.tracks()[0]->id();
    const auto& events = song.events(id);
    REQUIRE(events.size() == 2);
    CHECK(events[0].data == EventData{makeMetaText(MetaText::Kind::CuePoint, "kept")});

    // One section, ending on the bar line after the last note, which every track follows.
    REQUIRE(song.sections().size() == 1);
    CHECK(song.sections()[0]->name() == "Section 1");
    CHECK(song.end() == 3 * kPpqn.whole());
    REQUIRE(song.sections()[0]->content(id));
    CHECK_FALSE(song.sections()[0]->content(id)->meter);
}

TEST_CASE("a track with only SysEx or text still becomes a track")
{
    SmfTrack conductor;
    conductor.tempo(0, 500'000).end();
    SmfTrack setup;
    setup.text(0, 0x03, "GM Reset").sysex(0, 0xF0, {0x7E, 0x7F, 0x09, 0x01, 0xF7}).end();
    const auto song = importOk(makeSmf(1, 96, {conductor, setup}));

    CHECK(song.title.empty());
    REQUIRE(song.tracks().size() == 1);
    CHECK(song.tracks()[0]->name == "GM Reset");
    CHECK(song.tracks()[0]->channel == 1);
    CHECK(song.events(song.tracks()[0]->id()).size() == 1);
}

TEST_CASE("SysEx: complete, split into packets, and as escapes")
{
    const std::vector<std::uint8_t> message{0xF0, 0x41, 0x10, 0x42, 0x12, 0xF7};
    SmfTrack track;
    track.sysex(0, 0xF0, {0x41, 0x10, 0x42, 0x12, 0xF7})
        .sysex(10, 0xF0, {0x41, 0x10})                         // first packet
        .sysex(5, 0xF7, {0x42})                                // continuation
        .sysex(5, 0xF7, {0x12, 0xF7})                          // last packet
        .sysex(10, 0xF7, {0xF0, 0x41, 0x10, 0x42, 0x12, 0xF7}) // a whole message as an escape
        .sysex(10, 0xF7, {0xFA}) // a real-time Start as an escape: not kept
        .end();
    const auto result = importSmf(makeSmf(0, 96, {track}));

    REQUIRE(result.song);
    const auto& t = result.song->events(result.song->tracks()[0]->id());
    REQUIRE(t.size() == 3);
    for (const auto& event : t.all())
    {
        CHECK(*std::get<SysEx>(event.data).bytes == message);
    }
    // The split message is placed at its first packet.
    CHECK(t[1].tick == (2 * Tick{10} * kPpqn.value + 96) / (2 * 96));
    CHECK(hasWarning(result, "dropped 1"));
}

TEST_CASE("text events")
{
    SmfTrack track;
    track.text(0, 0x02, "(c) Me")
        .text(0, 0x05, "la")
        .text(0, 0x07, "cue")
        .event(0, {0x90, 60, 100})
        .event(96, {0x80, 60, 0})
        .end();
    const auto song = importOk(makeSmf(0, 96, {track}));

    const auto& t = song.events(song.tracks()[0]->id());
    REQUIRE(t.size() == 4);
    CHECK(t[0].data == EventData{makeMetaText(MetaText::Kind::Copyright, "(c) Me")});
    CHECK(t[1].data == EventData{makeMetaText(MetaText::Kind::Lyric, "la")});
    CHECK(t[2].data == EventData{makeMetaText(MetaText::Kind::CuePoint, "cue")});
}

// SMF tempos (24 bits of microseconds) can only be too fast: the slowest is about 3.6 BPM.
TEST_CASE("tempos outside 1-1000 BPM are clamped")
{
    SmfTrack track;
    track.tempo(0, 30'000).tempo(96, 0).event(0, {0x90, 60, 100}).event(96, {0x80, 60, 0}).end();
    const auto result = importSmf(makeSmf(0, 96, {track}));

    REQUIRE(result.song);
    CHECK(result.song->tempo().tempoAt(0) == Tempo{Tempo::kMinMicrosPerQuarter});
    CHECK(result.song->tempo().tempoAt(kPpqn.quarter()) == Tempo{Tempo::kMinMicrosPerQuarter});
    CHECK(hasWarning(result, "clamped 2 tempo(s)"));
}

TEST_CASE("a time signature between beats moves to the next beat")
{
    SmfTrack track;
    track.timeSignature(0, 4, 2)
        .timeSignature(96 + 1, 3, 2) // one file tick after beat 2
        .event(0, {0x90, 60, 100})
        .event(96, {0x80, 60, 0})
        .end();
    const auto result = importSmf(makeSmf(0, 96, {track}));

    REQUIRE(result.song);
    const auto& meter = result.song->meter(result.song->tracks()[0]->id());
    const Tick q = kPpqn.quarter();
    CHECK(meter.signatureAt(2 * q) == TimeSignature{3, 4});
    CHECK(meter.signatureAt(0) == TimeSignature{2, 4}); // the remainder bar
    CHECK(hasWarning(result, "time signature"));
}

TEST_CASE("damaged files are read as far as possible")
{
    SECTION("truncated track data")
    {
        SmfTrack track;
        track.event(0, {0x90, 60, 100}).event(96, {0x80, 60, 0}).raw({0, 0x90, 62});
        const auto result = importSmf(makeSmf(0, 96, {track}));
        REQUIRE(result.song);
        CHECK(result.song->events(result.song->tracks()[0]->id()).size() == 1);
        CHECK(hasWarning(result, "ended early"));
    }

    SECTION("data byte with no status")
    {
        SmfTrack track;
        track.raw({0, 60, 100});
        const auto result = importSmf(makeSmf(0, 96, {track}));
        REQUIRE(result.song);
        CHECK(result.song->tracks().empty());
        CHECK(hasWarning(result, "ended early"));
    }

    SECTION("wrong track count and unknown chunks")
    {
        SmfTrack track;
        track.event(0, {0x90, 60, 100}).event(96, {0x80, 60, 0}).end();
        auto bytes = makeSmf(0, 96, {track}, 3);
        bytes.insert(bytes.end(), {'X', 'Y', 'Z', 'W', 0, 0, 0, 2, 1, 2});
        const auto result = importSmf(bytes);
        REQUIRE(result.song);
        CHECK(result.song->tracks().size() == 1);
        CHECK(hasWarning(result, "lists 3 track(s)"));
    }

    SECTION("events after the end of track are ignored")
    {
        SmfTrack track;
        track.event(0, {0x90, 60, 100}).event(96, {0x80, 60, 0}).end().event(0, {0x90, 62, 100});
        const auto song = importOk(makeSmf(0, 96, {track}));
        CHECK(song.events(song.tracks()[0]->id()).size() == 1);
    }
}

TEST_CASE("importing from a file")
{
    SmfTrack track;
    track.event(0, {0x90, 60, 100}).event(96, {0x80, 60, 0}).end();
    const auto bytes = makeSmf(0, 96, {track});

    const auto path = std::filesystem::temp_directory_path() / "modal_smf_test.mid";
    {
        std::ofstream out{path, std::ios::binary};
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const auto result = importSmfFile(path);
    std::filesystem::remove(path);
    REQUIRE(result.song);
    CHECK(result.song->tracks().size() == 1);

    CHECK_FALSE(importSmfFile(path).song);
}
