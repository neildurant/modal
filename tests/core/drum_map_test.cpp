// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/core/document.hpp>
#include <modal/core/drum_map_file.hpp>
#include <modal/core/song_file.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <fstream>
#include <string>

using namespace modal::core;

TEST_CASE("the shared General MIDI drum map covers the metronome's percussion names")
{
    const auto map = generalMidiDrumMap();
    CHECK(map.name == "General MIDI");
    CHECK(std::ranges::count_if(map.notes, [](const auto& name) { return !name.empty(); }) == 47);
    CHECK(map.notes[35] == "Acoustic Bass Drum");
    CHECK(map.notes[36] == "Bass Drum 1");
    CHECK(map.notes[42] == "Closed Hi-Hat");
    CHECK(map.notes[81] == "Open Triangle");
    for (int note = 0; note < 128; ++note)
    {
        CHECK(map.notes[static_cast<std::size_t>(note)] == generalMidiDrumName(note));
    }
    CHECK(generalMidiDrumName(-1).empty());
    CHECK(generalMidiDrumName(128).empty());
}

TEST_CASE("reusable drum maps round trip sparse names with the song format's text escaping")
{
    DrumMap map;
    map.name = "Kit \"A\" \\ portable";
    map.notes[0] = "Low\n\t\x01";
    map.notes[36] = "Kick";
    map.notes[38] = "Caisse claire \xC3\xA9";
    map.notes[40] = "Kick"; // duplicate names are allowed, duplicate numbers aren't
    map.notes[127] = std::string{"Raw \xE9"};
    map.alwaysShow.set(36).set(37).set(127);
    const auto text = writeDrumMap(map);
    CHECK(text.starts_with("modal-drum-map 1\nname \"Kit \\\"A\\\" \\\\ portable\"\n"));
    CHECK(text.find("36 \"Kick\"\n") != std::string::npos);
    CHECK(text.find("37 ") == std::string::npos);
    CHECK(text.find("always-show 36\n") != std::string::npos);
    CHECK(text.find("always-show 37\n") != std::string::npos);
    const auto back = readDrumMap(text);
    REQUIRE(back.map);
    CHECK(back.error.empty());
    CHECK(*back.map == map);
    CHECK(writeDrumMap(*back.map) == text);
    const auto gm = readDrumMap(writeDrumMap(generalMidiDrumMap()));
    REQUIRE(gm.map);
    CHECK(*gm.map == generalMidiDrumMap());
    const auto empty = readDrumMap(writeDrumMap(DrumMap{}));
    REQUIRE(empty.map);
    CHECK(*empty.map == DrumMap{});

    const auto windows = readDrumMap("\" a comment\r\n\r\nmodal-drum-map 1\r\n"
                                     "name \"Kit\"\r\n  \" another comment\r\n36 \"Kick\"");
    REQUIRE(windows.map);
    CHECK(windows.map->notes[36] == "Kick");
    CHECK(windows.map->alwaysShow.none());
}

TEST_CASE("broken drum maps refuse the whole file and identify the line")
{
    const std::string header = "modal-drum-map 1\nname \"Kit\"\n";
    for (const auto& [text, line] : {
             std::pair{std::string{}, 1},
             {std::string{"modal-drum-map 2\nname \"Kit\"\n"}, 1},
             {std::string{"modal-song 1\nname \"Kit\"\n"}, 1},
             {std::string{"modal-drum-map 1\n"}, 2},
             {std::string{"modal-drum-map 1\nname Kit\n"}, 2},
             {header + "-1 \"Kick\"\n", 3},
             {header + "128 \"Kick\"\n", 3},
             {header + "999999999999999999999 \"Kick\"\n", 3},
             {header + "36 Kick\n", 3},
             {header + "36 \"\"\n", 3},
             {header + "36 \"Kick\" extra\n", 3},
             {header + "36 \"Kick\n", 3},
             {header + "36 \"\\q\"\n", 3},
             {header + "36 \"Kick\"\n36 \"Snare\"\n", 4},
             {header + "always-show -1\n", 3},
             {header + "always-show 128\n", 3},
             {header + "always-show \"36\"\n", 3},
             {header + "always-show 36\nalways-show 36\n", 4},
         })
    {
        INFO(text);
        const auto back = readDrumMap(text);
        CHECK_FALSE(back.map);
        CHECK(back.error.starts_with("line " + std::to_string(line) + ": "));
    }
}

TEST_CASE("a track keeps its drum map through song saves and independent copies")
{
    const auto ppqn = GENERATE(kPpqn, Ppqn{96});
    Song song{ppqn};
    const auto drums = song.addTrack("Drums");
    const auto other = song.addTrack("Another kit");
    const auto unmapped = song.addTrack("No map");
    song.updateTrack(drums, [](Track& track) { track.drumMap = generalMidiDrumMap(); });
    song.updateTrack(other,
                     [&](Track& track)
                     {
                         track.drumMap = song.track(drums)->drumMap;
                         track.drumMap->notes[36] = "Custom \"kick\"";
                         track.drumMap->notes[0] = "Low";
                         track.drumMap->notes[127] = "High";
                         track.drumMap->alwaysShow.set(0).set(37);
                     });
    song.updateTrack(unmapped, [](Track& track) { track.channel = 10; });
    CHECK_FALSE(song.track(unmapped)->drumMap);
    CHECK(song.track(drums)->drumMap->notes[36] == "Bass Drum 1");
    const auto text = writeSong(song);
    CHECK(text.find("drum-note 36 \"Custom \\\"kick\\\"\"") != std::string::npos);
    CHECK(text.find("drum-always-show 37\n") != std::string::npos);
    CHECK(song.track(drums)->drumMap->alwaysShow.none());
    const auto back = readSong(text, ppqn);
    INFO(back.error);
    REQUIRE(back.song);
    CHECK(*back.song == song);
    CHECK(writeSong(*back.song) == text);
    const auto rescaled = readSong(text, ppqn == kPpqn ? Ppqn{96} : kPpqn);
    REQUIRE(rescaled.song);
    CHECK(rescaled.song->track(other)->drumMap == song.track(other)->drumMap);
    CHECK_FALSE(rescaled.song->track(unmapped)->drumMap);
}

TEST_CASE("song drum-map lines must belong to one header track and name each pitch once")
{
    const std::string header = "modal-song 1\nsong \"Kit\" ppqn=960\n";
    const std::string track = "track 1 \"Drums\"\n";
    const std::string map = "drum-map \"Kit\"\n";
    const std::string tail = "section \"A\" bars=1\nmeter 1:1:0 4/4\ntempo 1:1:0 120\n";
    for (const auto& body : {
             map + track,
             track + "drum-note 36 \"Kick\"\n",
             track + "drum-map Kit\n",
             track + map + map,
             track + map + "drum-note 128 \"Kick\"\n",
             track + map + "drum-note -1 \"Kick\"\n",
             track + map + "drum-note 36 Kick\n",
             track + map + "drum-note 36 \"\"\n",
             track + map + "drum-note 36 \"Kick\" extra\n",
             track + map + "drum-note 36 \"Kick\"\ndrum-note 36 \"Snare\"\n",
             track + "drum-always-show 36\n",
             track + map + "drum-always-show 128\n",
             track + map + "drum-always-show 36 extra\n",
             track + map + "drum-always-show 36\ndrum-always-show 36\n",
             track + tail + map,
         })
    {
        INFO(body);
        const auto back = readSong(header + body + tail);
        CHECK_FALSE(back.song);
        CHECK(back.error.starts_with("line "));
    }
    // Existing files acquire no maps, regardless of their MIDI channel.
    const auto old = readSong(header + "track 1 \"Drums\" ch=10\n" + tail);
    INFO(old.error);
    REQUIRE(old.song);
    CHECK_FALSE(old.song->track(TrackId{1})->drumMap);
}

TEST_CASE("drum maps survive track copying and undo without changing their source")
{
    Song song{GENERATE(kPpqn, Ppqn{96})};
    const auto id = song.addTrack("Drums");
    song.updateTrack(id, [](Track& track) { track.drumMap = generalMidiDrumMap(); });
    const auto before = song;
    const auto copy = song.copyTrack(id);
    REQUIRE(copy);
    const auto pasted = song.insertTrack(1, *copy);
    REQUIRE(pasted);
    CHECK(song.track(*pasted)->drumMap == song.track(id)->drumMap);
    song.updateTrack(*pasted, [](Track& track) { track.drumMap->notes[36] = "Different kick"; });
    CHECK(song.track(id)->drumMap->notes[36] == "Bass Drum 1");
    CHECK(before.track(id)->drumMap == generalMidiDrumMap());

    Document document{before};
    CHECK(document.edit("Change kit", {}, [&](Song& edited)
                        { edited.updateTrack(id, [](Track& track) { track.drumMap.reset(); }); }));
    CHECK_FALSE(document.song().track(id)->drumMap);
    REQUIRE(document.undo());
    CHECK(document.song() == before);
    REQUIRE(document.redo());
    CHECK_FALSE(document.song().track(id)->drumMap);
}

TEST_CASE("reusable drum-map files are saved whole with useful read and write errors")
{
    const modal::test::TempDirectory temporary{"modal-drum-map-test"};
    const auto path = temporary.path() / "Kit with spaces";
    auto map = generalMidiDrumMap();
    CHECK(writeDrumMapFile(map, path).empty());
    map.notes[36] = "New kick";
    CHECK(writeDrumMapFile(map, path).empty());
    const auto back = readDrumMapFile(path);
    INFO(back.error);
    REQUIRE(back.map);
    CHECK(*back.map == map);
    CHECK(std::distance(std::filesystem::directory_iterator{temporary.path()},
                        std::filesystem::directory_iterator{}) == 1);
    const auto missing = readDrumMapFile(temporary.path() / "Missing");
    CHECK_FALSE(missing.map);
    CHECK(missing.error.find("Missing") != std::string::npos);
    CHECK_FALSE(writeDrumMapFile(map, temporary.path() / "Missing" / "Kit").empty());
    {
        std::ofstream{path} << "modal-drum-map 1\nbogus\n";
    }
    const auto broken = readDrumMapFile(path);
    CHECK_FALSE(broken.map);
    CHECK(broken.error.find("Kit with spaces, line 2:") != std::string::npos);
}
