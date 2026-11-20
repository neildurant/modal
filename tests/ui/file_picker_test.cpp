// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/core/drum_map_file.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

struct Pickers final : FileHost
{
    Pickers()
    {
        std::filesystem::create_directories(temporary / "Config/drum-maps");
        std::filesystem::create_directories(temporary / "State");
        workspace.setFolders(temporary / "Config", temporary.path());
        workspace.setStateFolder(temporary / "State");
        workspace.setFileHost(this);
        workspace.setLineHandler([this](const std::string& line)
                                 { report(registry.runLine(line)); });
        workspace.resize(1200, 640, {10, 20});
        workspace.show(ViewKind::Arrangement);
        addFileCommands(registry, workspace.recordingSession(), *this, temporary.path());
        addPromptCommands(registry, reader, config, temporary / "Config", this, temporary.path());
    }
    void chooseOpen(Chosen done) override
    {
        workspace.pickFile(Workspace::PickerKind::Song, std::nullopt, std::move(done));
    }
    void chooseOpenFile(FileKind kind, const std::filesystem::path&, Chosen done) override
    {
        REQUIRE(kind == FileKind::DrumMap);
        workspace.pickFile(Workspace::PickerKind::DrumMap, std::nullopt, std::move(done));
    }
    void chooseSave(FileKind kind, const std::filesystem::path& suggested, Chosen done) override
    {
        using Picker = Workspace::PickerKind;
        const bool review = kind == FileKind::DrumMapSave;
        const auto picker = kind == FileKind::Config              ? Picker::Config
                            : kind == FileKind::DrumMap || review ? Picker::DrumMap
                                                                  : Picker::Song;
        workspace.pickFile(picker, suggested, std::move(done), !review);
    }
    void chooseSong(std::vector<OpenSong> songs, std::size_t active, SongChosen done) override
    {
        workspace.chooseSong(std::move(songs), active, std::move(done));
    }
    void quit() override { quitRequested = true; }
    void songReplaced() override { workspace.showSong(); }
    void report(const CommandResult& result) override { workspace.report(result); }

    void type(std::u32string_view keys)
    {
        for (const auto key : keys)
            workspace.handle(key == U'\n'   ? Key::of(NamedKey::Enter)
                             : key == U'\t' ? Key::of(NamedKey::Tab)
                                            : Key::typed(key),
                             0);
    }
    void press(NamedKey key, Modifiers modifiers = kNoModifiers)
    {
        workspace.handle(Key::of(key, modifiers), 0);
    }
    bool shows(std::u32string_view expected)
    {
        for (const auto& region : workspace.frame())
            for (const auto& run : region.text)
                if (run.text.find(expected) != std::u32string::npos)
                    return true;
        return false;
    }
    void makeSong(std::string_view name, std::string title)
    {
        auto song = newSong();
        song.title = std::move(title);
        const auto file = temporary / name;
        std::filesystem::create_directories(file.parent_path());
        REQUIRE(core::writeSongFile(song, file).empty());
    }

    test::TempDirectory temporary{"modal-picker"};
    core::Document document{newSong()};
    CommandRegistry registry;
    Config config;
    ConfigReader reader{config, registry};
    Workspace workspace{registry, document};
    bool quitRequested = false;
};

} // namespace

TEST_CASE("the song picker browses folders and returns to a parent")
{
    Pickers p;
    p.makeSong("Takes/Morning Phrase.modal", "Morning");
    p.temporary.write("Takes/hidden-from-picker.txt", "not a song");
    p.type(U":e\nTakes\n");
    CHECK(p.shows(U"Morning Phrase.modal"));
    CHECK_FALSE(p.shows(U"hidden-from-picker.txt"));
    p.press(NamedKey::Down);
    p.press(NamedKey::Enter); // ../
    CHECK(p.shows(U"Takes/"));
    p.type(U"Takes\nMor\t\n");
    CHECK(p.document.song().title == "Morning");
    CHECK(p.workspace.mode() == Mode::Normal);
}

TEST_CASE("the song picker completes a folder with spaces and keeps completing inside it")
{
    Pickers p;
    p.makeSong("My Songs/Night Drive.modal", "Night Drive");
    p.type(U":e\nMy S\t");
    CHECK(p.workspace.commandLine().text() == U"\"My Songs/");
    CHECK(p.shows(U"Night Drive.modal"));
    p.type(U"Night\t\n");
    CHECK(p.document.song().title == "Night Drive");
}

TEST_CASE("Tab completes a shared file prefix before choosing a match")
{
    Pickers p;
    p.makeSong("Night Ride.modal", "Ride");
    p.makeSong("Night Run.modal", "Run");
    p.type(U":e\nNi\t");
    CHECK(p.workspace.commandLine().text() == U"\"Night R");
    CHECK(p.shows(U"Night Ride.modal"));
    CHECK(p.shows(U"Night Run.modal"));
    p.type(U"\t\t\n");
    CHECK(p.document.song().title == "Run");
}

TEST_CASE("the picker can reach the state directory from Places")
{
    Pickers p;
    p.makeSong("State/recovered.modal", "Recovered");
    p.type(U":e\n");
    p.press(NamedKey::Down, kAlt);
    CHECK(p.shows(U"Places"));
    CHECK(p.shows(U"Config"));
    CHECK(p.shows(U"Drum maps"));
    CHECK(p.shows(U"State"));
    p.press(NamedKey::Down);
    p.press(NamedKey::Down);
    p.press(NamedKey::Down);
    p.type(U"\nrec\n");
    CHECK(p.document.song().title == "Recovered");
}

TEST_CASE("saving an unnamed song offers a filename and confirms overwriting")
{
    Pickers p;
    p.makeSong("Untitled.modal", "Keep me");
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "New title"; });
    p.type(U":w\n");
    CHECK(p.shows(U"Enter for Untitled.modal"));
    p.type(U"\n");
    CHECK(p.workspace.message().starts_with("Replace "));
    p.type(U"n");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    CHECK(p.document.modified());
    CHECK_FALSE(p.document.file());
    p.type(U"\ny");
    CHECK_FALSE(p.document.modified());
    REQUIRE(p.document.file());
    CHECK(std::filesystem::equivalent(*p.document.file(), p.temporary / "Untitled.modal"));
    p.document.edit("Rename again", {}, [](core::Song& song) { song.title = "Saved again"; });
    p.type(U":w\n");
    CHECK(p.workspace.mode() == Mode::Normal);
    CHECK_FALSE(p.document.modified());
    CHECK_FALSE(p.quitRequested);
}

TEST_CASE("the drum map commands open the picker in the map directory")
{
    Pickers p;
    const auto library = p.temporary / "Config/drum-maps";
    const auto map = core::generalMidiDrumMap();
    REQUIRE(core::writeDrumMapFile(map, library / "Studio kit.drum-map").empty());
    p.type(U":drum-map-load\n");
    CHECK(p.shows(U"Load drum map"));
    CHECK(p.shows(U"Studio kit.drum-map"));
    p.type(U"Studio\t\n");
    const auto track = p.document.song().tracks().front()->id();
    REQUIRE(p.document.song().track(track)->drumMap == map);
    p.type(U":drum-map-save\nMy copy\n");
    CHECK(std::filesystem::exists(library / "My copy.drum-map"));
    p.type(U":drum-map-save\n");
    p.press(NamedKey::Escape);
    CHECK(p.workspace.mode() == Mode::Normal);
    CHECK_FALSE(std::filesystem::exists(library / "General MIDI.drum-map"));
}

TEST_CASE("saving over a drum map from the file picker asks for replacement once")
{
    Pickers p;
    const auto path = p.temporary / "Config/drum-maps/Kit.drum-map";
    auto saved = core::generalMidiDrumMap();
    saved.name = "Kit";
    REQUIRE(core::writeDrumMapFile(saved, path).empty());
    p.type(U":drum-map-load Kit\n");
    const auto track = p.document.song().tracks().front()->id();
    auto marked = saved;
    marked.alwaysShow.set(36);
    p.type(U"grgp36\nzP");
    REQUIRE(p.document.song().track(track)->drumMap == marked);

    p.type(U":drum-map-save\nKit\n");
    CHECK(p.shows(U"Always show"));
    CHECK(p.shows(U"Cancel save"));
    CHECK(p.shows(U"Replace the saved copy"));
    CHECK_FALSE(p.shows(U"Keep the song's copy"));
    CHECK_FALSE(p.shows(U"Use the saved copy"));
    CHECK_FALSE(p.shows(U"Save as another map"));
    CHECK(core::readDrumMapFile(path).map == saved);
    p.press(NamedKey::Enter); // Cancel save is selected first.
    CHECK(core::readDrumMapFile(path).map == saved);

    p.type(U":drum-map-save\nKit\n");
    p.press(NamedKey::Down);
    p.press(NamedKey::Enter);
    CHECK(core::readDrumMapFile(path).map == marked);
    CHECK(p.document.song().track(track)->drumMap == marked);
    CHECK(p.workspace.message().starts_with("Saved drum map to "));
}

TEST_CASE("the first save from write and quit only quits after saving")
{
    Pickers p;
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "New"; });
    p.type(U":wq\n");
    CHECK(p.shows(U"Save song"));
    p.press(NamedKey::Escape);
    CHECK_FALSE(p.quitRequested);
    CHECK(p.document.modified());
    p.type(U":wq\nWorking draft\n");
    CHECK(p.quitRequested);
    CHECK_FALSE(p.document.modified());
    CHECK(std::filesystem::exists(p.temporary / "Working draft.modal"));
}

TEST_CASE("an open picker cannot replace a song edited after it opened")
{
    Pickers p;
    p.makeSong("Another.modal", "Another");
    p.type(U":e\n");
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "Keep this"; });
    p.type(U"Another\n");
    CHECK(p.document.song().title == "Keep this");
    CHECK(p.workspace.message().starts_with("The song changed"));
}

TEST_CASE("write-config uses the file picker and keeps the chosen filename")
{
    Pickers p;
    const auto existing = p.temporary.write("Config/modalrc", "\" Existing configuration\n");
    p.type(U":write-config\n");
    CHECK(p.shows(U"Write configuration"));
    CHECK(p.shows(U"Enter for modalrc"));
    p.type(U"\n");
    CHECK(p.workspace.message().starts_with("Replace "));
    p.type(U"n");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    CHECK(std::filesystem::file_size(existing) ==
          std::string_view{"\" Existing configuration\n"}.size());
    p.type(U"Nested/modalrc\n");
    CHECK(p.workspace.mode() == Mode::Normal);
    CHECK(std::filesystem::is_regular_file(p.temporary / "Config/Nested/modalrc"));
    CHECK_FALSE(std::filesystem::exists(p.temporary / "Config/Nested/modalrc.modal"));
}

TEST_CASE("song select shows active and modified songs and narrows by path")
{
    Pickers p;
    p.makeSong("Rehearsal/Same.modal", "Same title");
    p.makeSong("Studio/Same.modal", "Same title");
    p.type(U":e Rehearsal/Same\n");
    CHECK(p.document.song().title == "Same title");
    p.document.edit("Rename track", {},
                    [](core::Song& song)
                    {
                        song.updateTrack(song.tracks().front()->id(),
                                         [](core::Track& track) { track.name = "Rehearsal edit"; });
                    });
    p.type(U":e ../Studio/Same\n:song-select\n");
    CHECK(p.shows(U"Open songs"));
    CHECK(p.shows(U"›   Same title"));
    CHECK(p.shows(U"+ Same title"));
    CHECK(p.shows(U"Studio"));
    CHECK(p.shows(U"Rehearsal"));
    p.type(U"Rehearsal\n");
    CHECK(p.document.song().tracks().front()->name == "Rehearsal edit");
    CHECK(p.document.modified());
    p.type(U"u");
    CHECK_FALSE(p.document.modified());
    p.type(U":new\n:song-select\n");
    CHECK(p.shows(U"Untitled"));
    CHECK(p.shows(U"Unsaved"));
    p.type(U"Studio");
    p.press(NamedKey::Escape);
    CHECK_FALSE(p.document.file());
}

TEST_CASE("the Files leader group opens, switches to, adds and closes songs")
{
    Pickers p;
    p.makeSong("First.modal", "First");
    p.workspace.setHints(true, 0);
    p.type(U",f");
    const auto shown = p.workspace.hints();
    REQUIRE(shown);
    std::vector<std::string> keys;
    for (const auto& hint : *shown)
        keys.push_back(hint.keys);
    CHECK(keys == std::vector<std::string>{"c", "f", "n", "o", "s", "S", "x"});
    p.press(NamedKey::Escape);
    p.workspace.setHints(false, 0);
    p.type(U",ff");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    p.type(U"First\n");
    CHECK(p.document.song().title == "First");
    p.type(U",fn");
    CHECK_FALSE(p.document.file());
    p.type(U",fo");
    CHECK(p.shows(U"Open songs"));
    p.type(U"First\n");
    CHECK(p.document.song().title == "First");

    // Closing keeps unsaved edits until the forced command discards them.
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "Edited"; });
    p.type(U",fc");
    CHECK(p.document.song().title == "Edited");
    CHECK(p.workspace.message().find(":song-close!") != std::string::npos);
    p.type(U":song-close!\n");
    CHECK_FALSE(p.document.file());
    p.type(U",fo");
    CHECK_FALSE(p.shows(U"First.modal"));
    p.press(NamedKey::Escape);
}

TEST_CASE("save as suggests the song's own name and moves the song to the file chosen")
{
    Pickers p;
    p.makeSong("Take.modal", "Take");
    p.type(U":e Take\n");
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "Edited"; });
    p.type(U",fS");
    CHECK(p.shows(U"Enter for Take.modal"));
    p.type(U"Take two\n");
    REQUIRE(p.document.file());
    CHECK(std::filesystem::equivalent(*p.document.file(), p.temporary / "Take two.modal"));
    CHECK_FALSE(p.document.modified());
    const auto original = core::readSongFile(p.temporary / "Take.modal");
    REQUIRE(original.song);
    CHECK(original.song->title == "Take");

    // Saving goes on to the new file; a typed name already taken needs the forced form.
    p.document.edit("Rename again", {}, [](core::Song& song) { song.title = "Again"; });
    p.type(U",fs");
    CHECK_FALSE(p.document.modified());
    CHECK(core::readSongFile(p.temporary / "Take two.modal").song->title == "Again");
    p.type(U":song-save-as Take\n");
    CHECK(p.workspace.message().find(":song-save-as! Take") != std::string::npos);
    CHECK(std::filesystem::equivalent(*p.document.file(), p.temporary / "Take two.modal"));
}

TEST_CASE("song select shows the rest of a long location beneath the list")
{
    Pickers p;
    p.workspace.resize(800, 640, {10, 20});
    const auto folder = std::string(50, 'a') + "/" + std::string(50, 'b');
    p.makeSong(folder + "/test-ending.modal", "Long location");
    p.type(decodeUtf8(":e " + folder + "/test-ending\n:song-select\n"));
    CHECK(p.shows(U"Long location"));
    CHECK(p.shows(U"test-ending.modal"));
}

TEST_CASE("open song paths align even when names are shortened")
{
    Pickers p;
    std::optional<std::size_t> chosen;
    p.workspace.chooseSong(
        {{"Short", "/songs/first.modal", true},
         {std::string(60, 'L') + " Secret ending", "/imports/second.mid", false, true}},
        0, [&](std::size_t index) { chosen = index; });
    const auto column = [&](std::u32string_view path) -> std::optional<std::size_t>
    {
        for (const auto& region : p.workspace.frame())
            for (const auto& run : region.text)
                if (const auto offset = run.text.find(path); offset != std::u32string::npos)
                    return static_cast<std::size_t>(run.x / 10) + offset;
        return std::nullopt;
    };
    const auto saved = column(U"/songs/first.modal");
    REQUIRE(saved);
    CHECK(column(U"/imports/second.mid") == saved);
    CHECK(p.shows(U"(MIDI import)"));
    CHECK_FALSE(p.shows(U"Secret ending"));
    p.type(U"Secret ending\n");
    CHECK(chosen == 1);
}
