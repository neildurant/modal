// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/core/drum_map_file.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/drum_maps.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <fstream>
#include <utility>

using namespace modal;
using namespace modal::ui;

namespace
{
core::DrumMap kit(std::string kick = "Kick")
{
    core::DrumMap map;
    map.name = "Studio";
    map.notes[36] = std::move(kick);
    map.notes[38] = "Snare";
    map.notes[42] = "Closed HH";
    return map;
}

core::Song pattern(core::Ppqn ppqn)
{
    core::Song song{ppqn};
    const auto id = song.addTrack("Drums");
    song.addTrack("Bass");
    song.updateEvents(id,
                      [&](core::EventList& events)
                      {
                          const auto beat = ppqn.quarter();
                          events.insert({0, core::Note{36, 90, beat / 4}});
                          events.insert({beat, core::Note{38, 90, beat / 4}});
                          events.insert({2 * beat, core::Note{42, 90, beat / 4}});
                      });
    return song;
}

struct Dialogs final : FileHost
{
    explicit Dialogs(Workspace& workspace)
        : ws(workspace)
    {
    }
    void chooseOpen(Chosen done) override { pending = std::move(done); }
    void chooseOpenFile(FileKind kind, const std::filesystem::path& from, Chosen done) override
    {
        chosenKind = kind;
        suggested = from;
        pending = std::move(done);
        saving = false;
    }
    void chooseSave(FileKind kind, const std::filesystem::path& from, Chosen done) override
    {
        chosenKind = kind;
        suggested = from;
        pending = std::move(done);
        saving = true;
    }
    void quit() override {}
    void songReplaced() override { ws.showSong(); }
    void report(const CommandResult& result) override { ws.report(result); }
    void answer(std::optional<std::filesystem::path> file)
    {
        REQUIRE(pending);
        std::exchange(pending, {})(std::move(file));
    }
    Workspace& ws;
    Chosen pending;
    FileKind chosenKind = FileKind::Song;
    std::filesystem::path suggested;
    bool saving = false;
};

struct Maps
{
    explicit Maps(core::Ppqn ppqn = core::kPpqn)
        : document(pattern(ppqn))
    {
        registry.add(windowCommands());
        ws.setLineHandler([&](const std::string& line) { ws.report(registry.runLine(line)); });
        ws.setFolders(temporary.path(), temporary.path());
        ws.setFileHost(&dialogs);
        ws.resize(1200, 640, {10, 20});
        ws.show(ViewKind::Arrangement);
        type(U"gr");
    }
    core::TrackId id() const { return document.song().tracks().front()->id(); }
    const std::optional<core::DrumMap>& map() const { return document.song().track(id())->drumMap; }
    std::filesystem::path folder() const { return temporary.path() / "drum-maps"; }
    void project(std::optional<core::DrumMap> map)
    {
        auto song = document.song();
        song.updateTrack(id(), [&](core::Track& track) { track.drumMap = std::move(map); });
        document.replace(std::move(song), std::nullopt);
        ws.showSong();
        ws.show(ViewKind::Arrangement);
        type(U"gr");
    }
    std::filesystem::path file(std::string_view name, const core::DrumMap& map)
    {
        std::filesystem::create_directories(folder());
        const auto path = folder() / utf8Path(name);
        REQUIRE(core::writeDrumMapFile(map, path).empty());
        return path;
    }
    void type(std::u32string_view keys)
    {
        for (const auto c : keys)
            ws.handle(c == U'\n'     ? Key::of(NamedKey::Enter)
                      : c == U'\t'   ? Key::of(NamedKey::Tab)
                      : c == U'\x1b' ? Key::of(NamedKey::Escape)
                                     : Key::typed(c),
                      0);
    }
    void press(NamedKey key, Modifiers modifiers = kNoModifiers)
    {
        ws.handle(Key::of(key, modifiers), 0);
    }
    void line(std::u32string_view text)
    {
        type(U":");
        type(text);
        type(U"\n");
    }
    std::u32string visible() const
    {
        std::u32string text;
        for (const auto& region : ws.frame())
            for (const auto& run : region.text)
                text += run.text + U" ";
        return text;
    }
    void has(std::u32string_view text) const
    {
        CHECK(visible().find(text) != std::u32string::npos);
    }
    void review() { type(U",tk\n"); }
    test::TempDirectory temporary{"modal-drum-maps-ui"};
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
    Dialogs dialogs{ws};
};
} // namespace

TEST_CASE("go to pitch accepts drum names and unnamed pitches without moving in time",
          "[drum-maps]")
{
    Maps m{GENERATE(core::kPpqn, core::Ppqn{96})};
    m.project(kit("Kick Centre"));
    const auto before = m.ws.pianoRoll().cursor();
    m.type(U"gpKICK C\t");
    CHECK(m.ws.commandLine().text() == U"Kick Centre ");
    m.type(U"\n");
    CHECK(m.ws.pianoRoll().cursor().pitch == 36);
    CHECK(m.ws.pianoRoll().cursor().tick == before.tick);
    m.type(U"''");
    CHECK(m.ws.pianoRoll().cursor() == before);
    m.type(U"gp127\n");
    CHECK(m.ws.pianoRoll().cursor().pitch == 127);
    m.type(U"gpC4\n");
    CHECK(m.ws.pianoRoll().cursor().pitch == 60);
    m.type(U"gp128\n");
    CHECK(m.ws.message().find("Unknown pitch") != std::string::npos);
    CHECK(m.ws.pianoRoll().cursor().pitch == 60);
    CHECK_FALSE(m.document.modified());
}

TEST_CASE("a drum name can select one pitch row for an edit", "[drum-maps]")
{
    Maps m{GENERATE(core::kPpqn, core::Ppqn{96})};
    m.project(kit());
    m.type(U":select pitch closed hh\n");
    REQUIRE(m.ws.mode() == Mode::Visual);
    CHECK(m.ws.pianoRoll().cursor().pitch == 42);
    m.type(U"d");
    const auto events = m.document.song().events(m.id()).all();
    REQUIRE(events.size() == 2);
    CHECK(std::get<core::Note>(events[0].data).pitch == 36);
    CHECK(std::get<core::Note>(events[1].data).pitch == 38);
}

TEST_CASE("a drum search asks which pitch to use when a name is ambiguous", "[drum-maps]")
{
    Maps m;
    auto map = kit();
    map.notes[38] = "Snare Centre";
    map.notes[40] = "Snare Edge";
    map.notes[42] = "Snare Rim";
    {
        auto song = m.document.song();
        song.updateEvents(m.id(),
                          [&](core::EventList& events)
                          {
                              events.insert({3 * core::kPpqn.quarter(),
                                             core::Note{42, 90, core::kPpqn.quarter() / 4}});
                          });
        m.document.replace(std::move(song), std::nullopt);
    }
    m.project(map);
    m.type(U"/SNARE\n");
    REQUIRE(m.ws.mode() == Mode::CommandLine);
    m.has(U"Snare Centre [38] · 1 note");
    m.has(U"Snare Edge [40] · 0 notes");
    m.has(U"Snare Rim [42] · 2 notes");
    m.type(U"\t\n");
    CHECK(m.ws.mode() == Mode::Normal);
    CHECK(m.ws.pianoRoll().cursor().pitch == 42);
    CHECK(m.ws.pianoRoll().cursor().tick == 2 * core::kPpqn.quarter());
    m.type(U"/Snare Centre\n");
    CHECK(m.ws.pianoRoll().cursor().pitch == 38);
    m.type(U"/snare\n\x1b");
    CHECK(m.ws.mode() == Mode::Normal);
    CHECK(m.ws.pianoRoll().cursor().pitch == 38);
    CHECK_FALSE(m.document.modified());
}

TEST_CASE("the palette lists the current track's drum pitches", "[drum-maps]")
{
    Maps m;
    auto map = kit();
    map.notes[38] = "Snare";
    map.notes[42] = "Snare";
    m.project(map);
    const auto before = m.ws.pianoRoll().cursor();
    m.ws.handle(Key::typed(U'p', kCtrl), 0);
    m.type(U"~SNARE");
    m.has(U"D2   · MIDI 38");
    m.has(U"F#2  · MIDI 42");
    m.press(NamedKey::Down);
    m.type(U"\n");
    CHECK(m.ws.pianoRoll().cursor().pitch == 42);
    CHECK(m.ws.pianoRoll().cursor().tick == before.tick);
    CHECK_FALSE(m.document.modified());

    m.press(NamedKey::PageDown, kCtrl);
    m.ws.handle(Key::typed(U'p', kCtrl), 0);
    m.type(U"~snare");
    CHECK(m.visible().find(U"MIDI 42") == std::u32string::npos);
}

TEST_CASE("drum maps can be chosen and removed from the piano roll as single undo steps",
          "[drum-maps]")
{
    Maps m{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = m.document.song();
    const auto cursor = m.ws.pianoRoll().cursor();
    m.type(U",tkGeneral\n");
    INFO(m.ws.message());
    REQUIRE(m.map());
    CHECK(*m.map() == core::generalMidiDrumMap());
    CHECK(m.ws.pianoRoll().cursor() == cursor);
    CHECK(m.document.song().events(m.id()) == before.events(m.id()));
    m.type(U"u");
    CHECK(m.document.song() == before);
    m.type(U",tkGeneral\n,tkNone\n");
    CHECK_FALSE(m.map());
    m.type(U"u");
    REQUIRE(m.map());
    CHECK(*m.map() == core::generalMidiDrumMap());
}

TEST_CASE("the Drum map field in Tracks opens the shared chooser", "[drum-maps]")
{
    Maps m;
    m.type(U"gt0");
    for (const auto& column : trackColumns())
    {
        if (column.field == TrackField::DrumMap)
            break;
        m.type(U"l");
    }
    m.type(U"\nGeneral\n");
    INFO(m.ws.message());
    REQUIRE(m.map());
    CHECK(m.map()->name == "General MIDI");
    CHECK(m.ws.shown() == ViewKind::Tracks);
}

TEST_CASE("opening a song flags same-name map differences without changing either copy",
          "[drum-maps]")
{
    Maps m;
    const auto saved = kit("Saved kick");
    const auto path = m.file("Different filename.drum-map", saved);
    m.project(kit("Song kick"));
    const auto before = m.document.song();
    const auto version = m.document.version();
    // showSong is the notification used by file opening; it also resets the view's cursor.
    m.ws.showSong();
    CHECK(m.ws.message().find("differs") != std::string::npos);
    CHECK(m.ws.message().find(", t k") != std::string::npos);
    CHECK_FALSE(m.document.modified());
    CHECK(m.document.version() == version);
    m.review();
    m.has(U"Song");
    m.has(U"Saved");
    m.has(U"Song kick");
    m.has(U"Saved kick");
    m.has(U"Different filename.drum-map");
    CHECK(m.document.song() == before);
    m.type(U"\n"); // Keep the song's copy is first.
    CHECK(m.document.song() == before);
    CHECK(m.document.version() == version);
    CHECK(core::readDrumMapFile(path).map == saved);
}

TEST_CASE("using a reviewed saved drum map changes only the track and undoes once", "[drum-maps]")
{
    Maps m{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto path = m.file("Saved.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    const auto before = m.document.song();
    const auto cursor = m.ws.pianoRoll().cursor();
    m.review();
    m.type(U"u\n");
    INFO(m.ws.message());
    CHECK(m.map() == kit("Saved kick"));
    CHECK(m.ws.pianoRoll().cursor() == cursor);
    CHECK(m.document.song().events(m.id()) == before.events(m.id()));
    CHECK_FALSE(m.document.song().tracks()[1]->drumMap);
    CHECK(core::readDrumMapFile(path).map == kit("Saved kick"));
    m.type(U"u");
    CHECK(m.document.song() == before);
}

TEST_CASE("replacing a reviewed saved map updates the file and leaves the song unmodified",
          "[drum-maps]")
{
    Maps m;
    const auto path = m.file("Saved.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    const auto version = m.document.version();
    m.review();
    m.type(U"r\n");
    INFO(m.ws.message());
    CHECK(core::readDrumMapFile(path).map == kit("Song kick"));
    CHECK(m.document.version() == version);
    CHECK_FALSE(m.document.modified());
    m.type(U",tk");
    m.has(U"matches saved");
}

TEST_CASE("several saved maps of one name require a file choice", "[drum-maps]")
{
    Maps m;
    const auto first = m.file("First.drum-map", kit("First kick"));
    const auto second = m.file("Second.drum-map", kit("Second kick"));
    m.project(kit("Song kick"));
    m.review();
    m.has(U"First.drum-map");
    m.has(U"Second.drum-map");
    CHECK(m.map() == kit("Song kick"));
    m.type(U"Second\nu\n");
    CHECK(m.map() == kit("Second kick"));
    CHECK(core::readDrumMapFile(first).map == kit("First kick"));
    CHECK(core::readDrumMapFile(second).map == kit("Second kick"));
}

TEST_CASE("an altered General MIDI map can be compared with the built-in copy and restored",
          "[drum-maps]")
{
    Maps m;
    auto changed = core::generalMidiDrumMap();
    changed.notes[36] = "Deep kick";
    m.project(changed);
    m.review();
    m.has(U"Built-in General MIDI");
    m.has(U"Deep kick");
    m.has(U"Bass Drum 1");
    CHECK(m.visible().find(U"Replace the saved copy") == std::u32string::npos);
    m.type(U"u\n");
    CHECK(m.map() == core::generalMidiDrumMap());
    m.type(U"u");
    CHECK(m.map() == changed);
    CHECK_FALSE(std::filesystem::exists(m.folder()));
}

TEST_CASE("a saved map edited outside Modal must be reviewed again before replacement",
          "[drum-maps]")
{
    Maps m;
    const auto path = m.file("Saved.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    m.review();
    REQUIRE(core::writeDrumMapFile(kit("Edited outside"), path).empty());
    m.type(U"r\n");
    CHECK(core::readDrumMapFile(path).map == kit("Edited outside"));
    m.has(U"Edited outside");
    CHECK(m.ws.message().find("file changed") != std::string::npos);
    m.type(U"r\n");
    CHECK(core::readDrumMapFile(path).map == kit("Song kick"));
}

TEST_CASE("drum-map file commands use the map folder, protect overwrites and complete filenames",
          "[drum-maps]")
{
    Maps m;
    m.file("My kit.drum-map", kit());
    m.line(U"drum-map-load \"My kit\"");
    INFO(m.ws.message());
    CHECK(m.map() == kit());
    const auto version = m.document.version();
    m.line(U"drum-map-save \"Another kit\"");
    CHECK(core::readDrumMapFile(m.folder() / "Another kit.drum-map").map == kit());
    CHECK(m.document.version() == version);
    const auto path = m.file("Another kit.drum-map", kit("Another kick"));
    m.line(U"drum-map-save \"Another kit\"");
    CHECK(m.ws.refused());
    CHECK(core::readDrumMapFile(path).map == kit("Another kick"));
    m.line(U"drum-map-save! \"Another kit\"");
    m.has(U"Another kick");
    m.type(U"r\n");
    CHECK(core::readDrumMapFile(path).map == kit());
    const auto choices = m.ws.choices(Choices::DrumMapFile, "My");
    CHECK(std::ranges::any_of(choices, [](const auto& choice)
                              { return choice.find("My kit") != std::string::npos; }));
    {
        std::ofstream{m.folder() / "Broken.drum-map"} << "modal-drum-map 1\nbogus\n";
    }
    m.line(U"drum-map-load Broken");
    CHECK(m.ws.refused());
    CHECK(m.ws.message().find("line 2") != std::string::npos);
    CHECK(m.map() == kit());
}

TEST_CASE("drum-map dialogs keep their target track, cancel cleanly and reject a replaced song",
          "[drum-maps]")
{
    Maps m;
    const auto path = m.file("My kit.drum-map", kit());
    const auto id = m.id();
    m.line(U"drum-map-load");
    CHECK(m.dialogs.chosenKind == FileHost::FileKind::DrumMap);
    CHECK_FALSE(m.dialogs.saving);
    CHECK(m.dialogs.suggested == m.folder());
    m.press(NamedKey::PageDown, kCtrl);
    m.dialogs.answer(path);
    CHECK(m.document.song().track(id)->drumMap == kit());
    CHECK_FALSE(m.document.song().tracks()[1]->drumMap);
    m.press(NamedKey::PageUp, kCtrl);
    const auto before = m.document.song();
    m.line(U"drum-map-save");
    CHECK(m.dialogs.saving);
    CHECK(m.dialogs.suggested.parent_path() == m.folder());
    m.dialogs.answer(std::nullopt);
    CHECK(m.document.song() == before);
    m.line(U"drum-map-load");
    m.project(std::nullopt);
    m.dialogs.answer(path);
    CHECK(m.ws.refused());
    CHECK_FALSE(m.map());
}

TEST_CASE("saving a drum map under a new name keeps both files and renames this track's copy",
          "[drum-maps]")
{
    Maps m;
    const auto saved = m.file("Studio.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    const auto before = m.document.song();
    m.review();
    m.type(U"s\n");
    m.ws.handle(Key::typed(U'u', kCtrl), 0);
    m.type(U"My studio\n");
    REQUIRE(m.dialogs.pending);
    CHECK(m.dialogs.suggested.filename() == "My studio.drum-map");
    const auto another = m.folder() / "My studio.drum-map";
    m.dialogs.answer(another);
    REQUIRE(m.map());
    CHECK(m.map()->name == "My studio");
    CHECK(m.map()->notes[36] == "Song kick");
    CHECK(core::readDrumMapFile(another).map == m.map());
    CHECK(core::readDrumMapFile(saved).map == kit("Saved kick"));
    m.type(U"u");
    CHECK(m.document.song() == before);
    CHECK(std::filesystem::exists(another)); // undo changes the song, not the reusable file
}

TEST_CASE("loading a differing same-name drum map opens review instead of replacing the song",
          "[drum-maps]")
{
    Maps m;
    m.file("Other.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    const auto version = m.document.version();
    m.line(U"drum-map-load Other");
    m.has(U"Song kick");
    m.has(U"Saved kick");
    CHECK(m.map() == kit("Song kick"));
    m.type(U"\x1b");
    CHECK(m.document.version() == version);
    m.line(U"drum-map-load Other");
    m.type(U"u\n");
    CHECK(m.map() == kit("Saved kick"));
}

TEST_CASE("saving another drum map can be cancelled at either question without renaming the song",
          "[drum-maps]")
{
    Maps m;
    m.file("Saved.drum-map", kit("Saved kick"));
    m.project(kit("Song kick"));
    const auto before = m.document.song();
    m.review();
    m.type(U"s\n\x1b");
    CHECK(m.document.song() == before);
    CHECK_FALSE(m.dialogs.pending);
    m.review();
    m.type(U"s\n\n"); // the suggested new name, then the save dialog
    REQUIRE(m.dialogs.pending);
    m.dialogs.answer(std::nullopt);
    CHECK(m.document.song() == before);
    m.review();
    m.type(U"s\n\n");
    m.dialogs.answer(m.folder() / "missing" / "New kit.drum-map");
    CHECK(m.ws.refused());
    CHECK(m.document.song() == before);
}

TEST_CASE("unreadable saved drum maps remain inspectable without blocking a song or other maps",
          "[drum-maps]")
{
    Maps m;
    m.file("Good.drum-map", kit());
    {
        std::ofstream{m.folder() / "Bad.DRUM-MAP"} << "broken\n";
    }
    m.ws.showSong();
    CHECK(m.ws.message().find("couldn't be read") != std::string::npos);
    m.type(U",tkBad\n");
    CHECK(m.ws.refused());
    CHECK(m.ws.message().find("Bad.DRUM-MAP, line 1") != std::string::npos);
    CHECK_FALSE(m.map());
    m.type(U"\x1b,tkGood\n");
    CHECK(m.map() == kit());
}

TEST_CASE("drum-map saves refuse song and MIDI filenames and saving an absent map", "[drum-maps]")
{
    Maps m;
    m.line(U"drum-map-save Empty");
    CHECK(m.ws.refused());
    CHECK_FALSE(std::filesystem::exists(m.folder() / "Empty.drum-map"));
    m.project(kit());
    const auto version = m.document.version();
    for (const auto name : {U"Song.modal", U"Song.MID", U"Song.midi"})
    {
        m.line(std::u32string{U"drum-map-save! "} + name);
        CHECK(m.ws.refused());
        CHECK(m.ws.message().find("song or MIDI filename") != std::string::npos);
    }
    CHECK(m.document.version() == version);
}

TEST_CASE("long drum-map comparisons wrap names and scroll back from the final differences",
          "[drum-maps]")
{
    Maps m;
    auto song = kit();
    auto saved = kit();
    for (std::size_t pitch = 0; pitch < song.notes.size(); ++pitch)
    {
        song.notes[pitch] =
            "Song name " + std::to_string(pitch) + " with a long descriptive suffix";
        saved.notes[pitch] =
            "Saved name " + std::to_string(pitch) + " with another descriptive suffix";
    }
    m.file("Saved.drum-map", saved);
    m.project(song);
    m.ws.resize(800, 480, {10, 20});
    m.review();
    m.has(U"Song name 0");
    m.has(U"Saved name 0");
    m.has(U"suffix");
    for (int n = 0; n < 100; ++n)
        m.press(NamedKey::PageDown);
    m.has(U"Saved name 127");
    m.press(NamedKey::PageUp);
    CHECK(m.visible().find(U"Saved name 127") == std::u32string::npos);
    for (int n = 0; n < 100; ++n)
        m.press(NamedKey::PageUp);
    m.has(U"Song name 0");
    CHECK(m.map() == song);
    m.type(U"\x1b");
    m.ws.resize(500, 480, {10, 20});
    m.review();
    for (const auto& region : m.ws.frame())
        for (const auto& run : region.text)
            if (region.background == Role::Panel && region.bounds.x == 0 && region.bounds.y == 0)
                CHECK(run.x + static_cast<int>(run.text.size()) * 10 <= region.bounds.width);
}

TEST_CASE("drum-map catalogues match internal names and differences include additions and removals",
          "[drum-maps]")
{
    Maps m;
    auto source = kit();
    source.name = "STUDIO";
    source.notes[0] = "Low";
    source.notes[127] = "High";
    source.notes[36] = "";
    m.file("One.DRUM-MAP", source);
    m.file("Two.drum-map", kit());
    m.file("Ignored.txt", kit());
    const auto library = readDrumMapLibrary(m.folder());
    CHECK(library.error.empty());
    CHECK(library.maps.size() == 3); // built-in and two reusable maps
    CHECK(matchingDrumMaps(kit(), library).size() == 2);
    const auto changes = drumMapDifferences(source, kit());
    REQUIRE(changes.size() == 4);
    CHECK_FALSE(changes[0].pitch); // case of the map name itself differs
    CHECK(changes[1].pitch == 0);
    CHECK(changes[1].song == "Low");
    CHECK(changes[1].saved.empty());
    CHECK(changes[2].pitch == 36);
    CHECK(changes[2].song.empty());
    CHECK(changes[2].saved == "Kick");
    CHECK(changes[3].pitch == 127);
}
