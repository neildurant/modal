// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/song_file.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace modal::core;
using namespace std::chrono_literals;

namespace
{

// A song that uses everything the format holds.
Song everything(Ppqn ppqn)
{
    const Tick q = ppqn.quarter();
    const Tick bar = ppqn.whole();
    Song song{ppqn};
    song.title = "Everything \"quoted\" \\ and \xE9 raw";
    song.metronome.when = MetronomeSettings::When::Always;
    song.metronome.port = "Drum Module";
    song.metronome.channel = 9;
    song.metronome.downbeatNote = 37;
    song.metronome.downbeatVelocity = 127;
    song.metronome.beatNote = 42;
    song.metronome.beatVelocity = 64;
    song.metronome.length = NoteFraction{1, 16};
    song.countIn.mode = CountInSettings::Mode::PreRoll;
    song.countIn.bars = 2;
    song.countIn.beats = 3;
    song.countIn.preRollBars = 4;

    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    song.addTrack(""); // nothing in it, and no name
    song.updateTrack(bass,
                     [](Track& track)
                     {
                         track.port = "Pro-800";
                         track.channel = 3;
                         track.transpose = -12;
                         track.velocityOffset = 10;
                         track.timeOffset = -40ms;
                         track.mute = true;
                         track.solo = true;
                         track.arm = true;
                         track.thru = false;
                         track.chaseNotes = false;
                     });

    song.updateMeter([&](MeterMap& meter) { meter.set(8 * q, {3, 4}); });
    song.updateMeter(lead, [](MeterMap& meter) { meter = MeterMap{meter.ppqn(), {7, 8}}; });
    song.updateTempo(
        [&](TempoMap& tempo)
        {
            tempo.set(0, Tempo{523'456}); // a tempo needing decimals
            tempo.set(2 * bar + q, Tempo::fromBpm(140));
        });
    song.setMarker(q, "fix \"this\" fill");
    song.setMarker(5 * bar, "later");
    song.updateEvents(bass,
                      [&](EventList& events)
                      {
                          events.insert({0, Note{40, 96, q / 2}});
                          events.insert({q, Note{41, 1, 437, 0}}); // a length in ticks
                          events.insert({q, Note{127, 127, 0}});   // a zero length
                          events.insert({2 * q, Note{0, 50, 3 * bar}});
                          events.insert({q, ControlChange{74, 64}});
                          events.insert({q + 1, ProgramChange{5}});
                          events.insert({2 * q, PitchBend{PitchBend::kMin}});
                          events.insert({2 * q, PitchBend{PitchBend::kMax}});
                          events.insert({3 * q, ChannelPressure{33}});
                          events.insert({3 * q, PolyPressure{61, 44}});
                          events.insert({4 * bar, makeSysEx({0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7})});
                          events.insert({4 * bar, makeMetaText(MetaText::Kind::Text, "t\nx")});
                          events.insert({4 * bar, makeMetaText(MetaText::Kind::Copyright, "(c)")});
                          events.insert(
                              {4 * bar, makeMetaText(MetaText::Kind::InstrumentName, "i")});
                          events.insert({4 * bar, makeMetaText(MetaText::Kind::Lyric, "la")});
                          events.insert({4 * bar, makeMetaText(MetaText::Kind::CuePoint, "")});
                      });
    song.updateEvents(lead, [&](EventList& events)
                      { events.insert({3 * bar, Note{72, 100, q / 3}}); }); // a triplet

    song.splitSection(2 * bar, "Verse");         // mid-bar on the lead's 7/8
    song.splitSection(2 * bar + 5 * q, "Verse"); // numbered, and mid-bar on the conductor
    song.renameSection(0, "Intro");
    return song;
}

std::string lineOf(const std::string& error)
{
    return error.substr(0, error.find(':'));
}

} // namespace

TEST_CASE("a song reads back the same, and writes the same text again")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto song = everything(ppqn);
    const auto text = writeSong(song);
    INFO(text);

    const auto back = readSong(text, ppqn);
    INFO(back.error);
    REQUIRE(back.song);
    CHECK(*back.song == song);
    CHECK(writeSong(*back.song) == text);

    // The flattened timeline is the same too.
    CHECK(back.song->end() == song.end());
    CHECK(std::ranges::equal(back.song->tempo().changes(), song.tempo().changes()));
    CHECK(back.song->meter() == song.meter());
    for (const auto& track : song.tracks())
    {
        CHECK(back.song->meter(track->id()) == song.meter(track->id()));
        CHECK(back.song->events(track->id()) == song.events(track->id()));
    }
}

TEST_CASE("the song format")
{
    const Ppqn ppqn = kPpqn;
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    song.title = "Untitled";
    const auto bass = song.addTrack("Bass");
    const auto drums = song.addTrack("Drums");
    song.updateTrack(bass, [](Track& track) { track.port = "Pro-800"; });
    song.updateTrack(drums, [](Track& track) { track.channel = 10; });
    song.updateMeter(drums, [](MeterMap& meter) { meter = MeterMap{meter.ppqn(), {7, 8}}; });
    song.updateEvents(bass, [&](EventList& events) { events.insert({0, Note{28, 96, q / 2}}); });
    song.updateEvents(drums, [&](EventList& events)
                      { events.insert({7 * q / 2 + q / 4, Note{36, 110, q / 4}}); });
    song.setMarker(8 * q, "fix this fill");
    song.splitSection(16 * q, "Verse 1");
    song.renameSection(0, "Intro");
    song.fitToContent();

    CHECK(writeSong(song) == R"(modal-song 1
song "Untitled" ppqn=960
metronome when=recording ch=10 downbeat=E5 downbeatvel=110 beat=F5 beatvel=80 len=1/32
countin mode=countin bars=1 beats=0 preroll=1

track 1 "Bass" ch=1 port="Pro-800"
track 2 "Drums" ch=10

section "Intro" bars=4
  meter 1:1:0 4/4
  tempo 1:1:0 120
  marker 3:1:0 "fix this fill"
  track 1
    1:1:0 note E1 v=96 len=1/8
  track 2
    meter 1:1:0 7/8
    meter 5:1:0 4/8
    2:1:240 note C2 v=110 len=1/16

section "Verse 1" bars=1
  meter 1:1:0 4/4
  tempo 1:1:0 120
  track 2
    meter 1:1:0 7/8
)");
}

TEST_CASE("a song at another resolution is rescaled")
{
    const Tick q = kPpqn.quarter();
    Song song{kPpqn};
    const auto piano = song.addTrack("Piano");
    song.updateEvents(piano,
                      [&](EventList& events)
                      {
                          events.insert({0, Note{60, 100, 437}});   // touching the next
                          events.insert({437, Note{62, 100, q}});   // off the grid
                          events.insert({4 * q, Note{64, 100, q}}); // on a bar line
                      });
    song.setMarker(5, "early");
    const auto text = writeSong(song);

    for (const Ppqn to : {Ppqn{96}, Ppqn{6720}})
    {
        CAPTURE(to.value);
        const auto back = readSong(text, to);
        REQUIRE(back.song);
        CHECK(back.song->ppqn() == to);
        CHECK(back.song->end() == 8 * to.whole());
        const auto& events = back.song->events(back.song->tracks()[0]->id());
        REQUIRE(events.size() == 3);
        const auto& first = std::get<Note>(events[0].data);
        CHECK(events[0].tick + first.duration == events[1].tick); // still touching
        CHECK(events[2].tick == to.whole());
        CHECK(std::get<Note>(events[2].data).duration == to.quarter());
    }
}

TEST_CASE("comments, blank lines, indentation and Windows line ends are allowed")
{
    const auto back = readSong("\" A comment\r\n"
                               "modal-song 1\r\n"
                               "\r\n"
                               "song \"S\" ppqn=96\r\n"
                               "track 5 \"T\" ch=2\r\n"
                               "section \"A\" bars=1\r\n"
                               "meter 1:1:0 4/4\r\n"
                               "     \" indented comment\r\n"
                               "tempo 1:1:0 90.5\r\n"
                               "track 5\r\n"
                               "1:2:0 note c4 v=1 len=1/4\r\n",
                               Ppqn{96});
    INFO(back.error);
    REQUIRE(back.song);
    CHECK(back.song->title == "S");
    CHECK(back.song->tempo().tempoAt(0) == Tempo::fromBpm(90.5));
    CHECK(back.song->events(TrackId{5}).size() == 1);
    CHECK(back.song->events(TrackId{5})[0].tick == 96);
}

TEST_CASE("anything wrong in a song file stops it being read, saying where")
{
    const std::string header = "modal-song 1\nsong \"S\" ppqn=960\ntrack 1 \"T\" ch=1\n";
    const std::string section = "section \"A\" bars=2\n  meter 1:1:0 4/4\n  tempo 1:1:0 120\n";
    struct Bad
    {
        std::string text;
        std::string line;
        std::string_view says;
    };
    const std::vector<Bad> cases{
        {"", "line 1", "empty"},
        {"MThd\n", "line 1", "not a Modal song"},
        {"modal-song 2\n", "line 1", "newer Modal"},
        {"modal-song 1\nsection \"A\" bars=1\n", "line 2", "song line"},
        {"modal-song 1\nsong \"S\"\n", "line 2", "ppqn"},
        {header, "line 4", "no sections"},
        {header + "track 1 \"Again\" ch=1\n", "line 4", "already a track 1"},
        {header + "track 2 \"T\" ch=17\n", "line 4", "channel"},
        {header + "track 2 \"T\" colour=red\n", "line 4", "unknown track setting"},
        {header + "section \"A\"\n", "line 4", "bars=N"},
        {header + "section \"A\" bars=1\n  tempo 1:1:0 120\n", "line 5", "meter comes before"},
        {header + "section \"A\" bars=1\n  meter 1:1:0 4/4\n", "line 4", "no tempo"},
        {header + section + "  meter 2:1:0 3/4\n", "line 7", "meter lines come first"},
        {header + section + "  tempo 1:2:0 1001\n", "line 7", "1 to 1000 BPM"},
        {header + section + "  tempo 3:1:0 100\n", "line 7", "past the end"},
        {header + section + "  marker 1:1:0 \"a\"\n  marker 2:1:0 \"a\"\n", "line 8",
         "already a marker \"a\""},
        {header + section + "  meter 1:1:0 4/4\n", "line 7", "meter lines come first"},
        {header + section + "  track 2\n", "line 7", "no track 2"},
        {header + section + "  track 1\n  track 1\n", "line 8", "comes twice"},
        {header + section + "  1:1:0 cc 1 2\n", "line 7", "belongs in a track"},
        {header + section + "  track 1\n    1:5:0 cc 1 2\n", "line 8", "only 4 beats"},
        {header + section + "  track 1\n    1:1:960 cc 1 2\n", "line 8", "only 960 ticks"},
        {header + section + "  track 1\n    3:1:0 cc 1 2\n", "line 8", "past the end"},
        {header + section + "  track 1\n    1:1:0 cc 1 128\n", "line 8", "0 to 127"},
        {header + section + "  track 1\n    1:1:0 note H2 v=1 len=1/4\n", "line 8", "pitch"},
        {header + section + "  track 1\n    1:1:0 note C2 len=1/4\n", "line 8",
         "velocity and length"},
        {header + section + "  track 1\n    1:1:0 note C2 v=1 len=1/3x\n", "line 8", "length"},
        {header + section + "  track 1\n    1:1:0 sysex F0 7E\n", "line 8", "out of range"},
        {header + section + "  track 1\n    1:1:0 lyric la\n", "line 8", "text in quotes"},
        {header + section + "  track 1\n    1:1:0 notee C2\n", "line 8", "unknown event type"},
        {header + section + "  track 1\n    1:1:0 text \"open\n", "line 8", "quote isn't closed"},
        {header + section + "  track 1\n    1:1:0 text \"\\q\"\n", "line 8", "unknown escape"},
        {header + section + "  track 1\n    meter 1:1:0 7/7\n", "line 8", "time signature"},
        {header + section + "section \"A\" bars=1\n", "line 7", "already a section"},
        {header + section + "metronome when=off\n", "line 7", "before the sections"},
        {header + "metronome when=sometimes\n", "line 4", "off, recording or always"},
        {header + "countin bars=-1\n", "line 4", "0 to 999"},
        {header + "bogus\n", "line 4", "unknown line"},
    };
    for (const auto& [text, line, says] : cases)
    {
        CAPTURE(text);
        const auto result = readSong(text);
        CAPTURE(result.error);
        CHECK_FALSE(result.song);
        CHECK(lineOf(result.error) == line);
        CHECK(result.error.find(says) != std::string::npos);
    }
}

TEST_CASE("a song file is written whole, or not at all")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal_song_file_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "song.modal";

    const auto song = everything(kPpqn);
    CHECK(writeSongFile(song, path).empty());
    CHECK_FALSE(std::filesystem::exists(directory / "song.modal.tmp"));
    const auto back = readSongFile(path);
    REQUIRE(back.song);
    CHECK(*back.song == song);

    CHECK_FALSE(writeSongFile(song, directory / "missing" / "song.modal").empty());
    const auto missing = readSongFile(directory / "nothing.modal");
    CHECK_FALSE(missing.song);
    CHECK(missing.error.find("can't read") != std::string::npos);

    {
        std::ofstream{directory / "bad.modal"} << "modal-song 1\nsong \"S\" ppqn=960\nbogus\n";
    }
    const auto bad = readSongFile(directory / "bad.modal");
    CHECK(bad.error.find("bad.modal, line 3: unknown line") != std::string::npos);
    std::filesystem::remove_all(directory);
}
