// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/core/drum_map_file.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

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
        const auto picker = kind == FileKind::Config    ? Picker::Config
                            : kind == FileKind::DrumMap ? Picker::DrumMap
                                                        : Picker::Song;
        workspace.pickFile(picker, suggested, std::move(done));
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
