// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/temp_directory.hpp"

#include <modal/core/drum_map_file.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/backups.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
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
                            : kind == FileKind::Midi              ? Picker::Midi
                                                                  : Picker::Song;
        workspace.pickFile(picker, suggested, std::move(done), !review);
    }
    void chooseSong(std::vector<OpenSong> songs, std::size_t active, SongChosen done) override
    {
        workspace.chooseSong(std::move(songs), active, std::move(done));
    }
    void quit() override { quitRequested = true; }
    void songReplaced() override { workspace.showSong(); }
    std::shared_ptr<void> leaveSong() override { return workspace.leaveSong(); }
    std::optional<std::filesystem::path> songsFolder() const override
    {
        return workspace.songsFolder();
    }
    void songUsed(const std::filesystem::path& file,
                  const std::optional<std::filesystem::path>& replacing,
                  const std::string& title) override
    {
        workspace.songUsed(file, replacing, title);
    }
    void returnToSong(std::shared_ptr<void> kept) override
    {
        workspace.returnToSong(std::move(kept));
    }
    std::string backUp(const std::filesystem::path& file) override
    {
        return workspace.backUp(file);
    }
    void songShown(std::uint64_t id) override { workspace.songShown(id); }
    void songClosed(std::uint64_t id) override { workspace.songClosed(id); }
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
    for (int i = 0; i < 4; ++i)
        p.press(NamedKey::Down);
    p.type(U"\nrec\n");
    CHECK(p.document.song().title == "Recovered");
}

TEST_CASE("the songfolder setting is where a new song's files start, and a Place")
{
    Pickers p;
    Settings settings;
    p.workspace.setSettings(&settings);
    p.makeSong("Songs/Ballad.modal", "Ballad");
    p.makeSong("Elsewhere/Sketch.modal", "Sketch");

    // Unset, Songs is home, the setting named after it; a new song starts there.
    p.type(U":e\n");
    CHECK(p.shows(U"Folder: "));
    // By the folder's own name, as the picker shows it resolved, which may be spelled otherwise.
    CHECK(p.shows(decodeUtf8(utf8Text(p.temporary.path().filename()))));
    p.press(NamedKey::Down, kAlt);
    CHECK(p.shows(U"Songs"));
    CHECK(p.shows(decodeUtf8(utf8Text(p.temporary.path())) + U"  (songfolder)"));
    CHECK(p.shows(U"Home"));
    CHECK_FALSE(p.shows(U"This song's folder"));
    p.press(NamedKey::Escape);
    p.press(NamedKey::Escape);

    // Set, a new song's picker and typed paths start there.
    REQUIRE(settings.set("songfolder", utf8Text(p.temporary / "Songs")).empty());
    p.type(U":e\n");
    CHECK(p.shows(U"Ballad.modal"));
    p.type(U"Ballad\n");
    CHECK(p.document.song().title == "Ballad");
    p.type(U":new\n:e Ballad\n");
    CHECK(p.workspace.message() == "Opened Ballad.modal");

    // A song with a file keeps to its own folder, which Places offers too.
    p.type(U":e ../Elsewhere/Sketch\n:e\n");
    CHECK(p.shows(U"Sketch.modal"));
    p.press(NamedKey::Down, kAlt);
    CHECK(p.shows(U"This song's folder"));
    p.press(NamedKey::Enter); // Songs
    CHECK(p.shows(U"Ballad.modal"));
    p.press(NamedKey::Escape);

    // A Songs folder that isn't there is marked, and not entered.
    REQUIRE(settings.set("songfolder", utf8Text(p.temporary / "Missing")).empty());
    p.type(U":new\n:e\n");
    p.press(NamedKey::Down, kAlt);
    CHECK(p.shows(U"(songfolder, not found)"));
    p.press(NamedKey::Enter);
    CHECK(p.shows(U"Folder not found: "));
    p.press(NamedKey::Escape);
    p.press(NamedKey::Escape);
}

TEST_CASE("recent songs list the files opened and saved, to open one again")
{
    Pickers p;
    Settings settings;
    p.workspace.setSettings(&settings);
    p.makeSong("Ballad.modal", "Ballad");
    p.makeSong("Groove.modal", "Groove");
    p.makeSong("Gone.modal", "Gone");
    p.type(U":e Ballad\n:e Gone\n:e Groove\n");
    std::filesystem::remove(p.temporary / "Gone.modal");

    // Newest first, the gone one marked; typing narrows; Enter opens.
    p.type(U",fr");
    CHECK(p.shows(U"Recent songs: "));
    // A cursor after what's typed, so it's plain the line takes typing.
    bool cursor = false;
    for (const auto& region : p.workspace.frame())
        for (std::size_t i = 0; i + 1 < region.text.size(); ++i)
            cursor = cursor || (region.text[i].text == U"Recent songs: " &&
                                region.text[i + 1].style.background == Role::Cursor);
    CHECK(cursor);
    CHECK(p.shows(U"Del removes from the list"));
    CHECK(p.shows(U"not found"));
    CHECK(p.shows(U"today "));
    p.type(U"ball\n");
    CHECK(p.document.song().title == "Ballad");
    CHECK(p.workspace.remembered().songs().front().path ==
          utf8Text((p.temporary / "Ballad.modal").lexically_normal()));
    CHECK(p.workspace.remembered().songs().front().title == "Ballad");

    // Titles in a column of their own before the file's name; typing narrows by title too, and
    // a title set and saved is the one listed.
    p.type(U":title Slow one\n:w\n,fr");
    CHECK(p.shows(U"Slow one"));
    bool titleFirst = false;
    for (const auto& region : p.workspace.frame())
        for (const auto& run : region.text)
            if (const auto title = run.text.find(U"Slow one"); title != std::u32string::npos)
                titleFirst = titleFirst || title < run.text.find(U"Ballad.modal");
    CHECK(titleFirst);
    p.type(U"slow\n");
    CHECK(p.document.song().title == "Slow one");

    // Delete takes an entry off the list, never the file.
    p.type(U",frgroove");
    p.press(NamedKey::Delete);
    CHECK(p.workspace.message() ==
          "Removed Groove.modal from the recent list; the file is untouched");
    CHECK(std::filesystem::exists(p.temporary / "Groove.modal"));
    CHECK(p.workspace.remembered().songs().size() == 2);
    p.press(NamedKey::Escape);

    // A file that's gone is offered for removal.
    p.type(U",frgone\n");
    CHECK(p.workspace.message() == "Gone.modal no longer exists. Remove it from the list? (y/n)");
    p.type(U"y");
    CHECK(p.workspace.remembered().songs().size() == 1);

    // An imported MIDI file gives its place to the song saved from it.
    auto song = newSong();
    REQUIRE(core::exportSmfFile(song, p.temporary / "Sketch.mid").error.empty());
    p.type(U":e Sketch.mid\n");
    CHECK(p.workspace.remembered().songs().front().path.ends_with("Sketch.mid"));
    p.type(U":w Sketch\n");
    CHECK(p.workspace.remembered().songs().front().path.ends_with("Sketch.modal"));
    CHECK(std::ranges::none_of(p.workspace.remembered().songs(), [](const auto& recent)
                               { return recent.path.ends_with("Sketch.mid"); }));

    // None kept with recentsongs at 0.
    REQUIRE(settings.set("recentsongs", "0").empty());
    const auto before = p.workspace.remembered().songs().size();
    p.type(U":e Groove\n");
    CHECK(p.workspace.remembered().songs().size() == before);
}

TEST_CASE("saving over a song keeps the version it replaces as a backup")
{
    Pickers p;
    Settings settings;
    p.workspace.setSettings(&settings);
    const auto file = p.temporary.path() / "Backed.modal";
    const auto backups = [&] { return songBackups(p.temporary / "State", file); };
    const auto contents = [](const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{stream}, {}};
    };

    // A new file replaces nothing.
    p.type(U":w Backed\n");
    CHECK(p.workspace.message() == "Saved Backed.modal");
    CHECK(backups().empty());

    p.type(U":song-title First\n:w\n");
    p.type(U":song-title Second\n:w\n");
    REQUIRE(backups().size() == 2);
    CHECK(contents(backups()[0].file).find("\"First\"") != std::string::npos);
    CHECK(contents(backups()[1].file).find("\"First\"") == std::string::npos);
    CHECK(contents(file).find("\"Second\"") != std::string::npos);

    // None kept with backups at 0.
    REQUIRE(settings.set("backups", "0").empty());
    p.type(U":song-title Third\n:w\n");
    CHECK(backups().size() == 2);
    REQUIRE(settings.set("backups", "5").empty());

    // A backup that can't be made doesn't stop the save, and says so.
    p.temporary.write("Blocked", "in the way");
    p.workspace.setStateFolder(p.temporary / "Blocked");
    p.type(U":song-title Fourth\n:w\n");
    CHECK(p.workspace.message().starts_with(
        "Saved Backed.modal, but no backup was made: can't make "));
    CHECK_FALSE(p.document.modified());
    CHECK(contents(file).find("\"Fourth\"") != std::string::npos);
}

TEST_CASE("song-backups lists the song's backups, to open one as a new song")
{
    Pickers p;
    Settings settings;
    p.workspace.setSettings(&settings);
    std::int64_t now = 1'800'000'000;
    p.workspace.setClock([&] { return now; });

    // Nothing to list for a song without a file, or with no backups yet.
    p.type(U",fb");
    CHECK(p.workspace.message().starts_with("This song has no file yet, so no backups"));
    p.type(U":w Kept\n");
    p.type(U",fb");
    CHECK(p.workspace.message() ==
          "No backups of Kept.modal yet: each save keeps the version it replaces");

    p.type(U":song-title First\n:w\n");
    p.type(U":song-title Second\n:w\n");
    p.type(U":song-title Third\n");
    p.type(U",fb");
    CHECK(p.shows(U"Backups of Kept.modal"));
    CHECK(p.shows(U"Backup 1"));
    CHECK(p.shows(U"(newest)"));
    CHECK(p.shows(U"Backup 2"));
    CHECK(p.shows(U"Enter opens it as a new song"));
    p.press(NamedKey::Escape);
    CHECK_FALSE(p.shows(U"Backup 1"));
    CHECK(p.document.song().title == "Third");

    // Backup 1 is the version the last save replaced, opened beside the song, which is kept
    // as it was, unsaved edits and all.
    p.type(U",fb");
    p.type(U"\n");
    CHECK(p.workspace.message().starts_with("Opened backup 1 of Kept.modal, saved "));
    CHECK(p.document.song().title == "First");
    CHECK_FALSE(p.document.file());
    CHECK_FALSE(p.document.modified());
    p.type(U"[f");
    CHECK(p.document.song().title == "Third");
    CHECK(p.document.modified());

    // Down chooses an older one; a save of the copy asks where, never writing over the song.
    p.type(U",fb");
    p.press(NamedKey::Down);
    p.type(U"\n");
    CHECK(p.workspace.message().starts_with("Opened backup 2 of Kept.modal"));
    CHECK(p.document.song().title.empty());
    p.type(U":w\n");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    CHECK(p.shows(U"Enter for Untitled.modal"));
}

TEST_CASE(":wa asks for each new song's file in the picker in turn")
{
    Pickers p;
    p.makeSong("Kept.modal", "Kept");
    p.type(U":e Kept\n:song-title Kept changed\n");
    p.type(U":new\n:song-title Sketch\n:new\n:song-title Idea\n");
    p.type(U":wa\n");
    CHECK(p.document.song().title == "Sketch");
    CHECK(p.shows(U"Enter for Untitled.modal"));
    p.type(U"Sketch\n");
    CHECK(p.document.song().title == "Idea");
    CHECK(p.shows(U"Enter for Untitled.modal"));
    p.type(U"Idea\n");
    CHECK(p.workspace.message() == "Saved 3 songs");
    CHECK(std::filesystem::exists(p.temporary / "Sketch.modal"));
    CHECK(std::filesystem::exists(p.temporary / "Idea.modal"));
    CHECK(p.workspace.remembered().songs().front().title == "Idea");
    CHECK(std::ranges::any_of(p.workspace.remembered().songs(),
                              [](const auto& recent) { return recent.title == "Kept changed"; }));
}

TEST_CASE("Ctrl-o goes back across songs, and a closed song's places are dropped")
{
    Pickers p;
    p.makeSong("A.modal", "Song A");
    p.makeSong("B.modal", "Song B");
    const auto ctrl = [&](char32_t key) { p.workspace.handle(Key::typed(key, kCtrl), 0); };
    const auto tick = [&] { return p.workspace.arrangement().cursor().tick; };
    p.type(U":e A\n");
    p.type(U"G");
    const auto endA = tick();
    REQUIRE(endA > 0);
    p.type(U":e B\n"); // switching songs is a jump
    p.type(U"G");

    ctrl(U'o');
    CHECK(p.document.song().title == "Song B");
    CHECK(tick() == 0);
    ctrl(U'o'); // into the song before, where it was left
    CHECK(p.document.song().title == "Song A");
    CHECK(tick() == endA);
    ctrl(U'o');
    CHECK(tick() == 0);
    ctrl(U'i');
    ctrl(U'i');
    CHECK(p.document.song().title == "Song B");
    CHECK(tick() == 0);

    // :jumps names each place's song.
    p.type(U":jumps\n");
    using Line = std::pair<std::string, std::string>;
    REQUIRE(p.workspace.listing().size() == 4);
    CHECK(p.workspace.listing()[0] == Line{"1", "Song A, Arrangement, 1:1:000, Track 1"});
    CHECK(p.workspace.listing()[1].second.starts_with("Song A, Arrangement, "));
    CHECK(p.workspace.listing()[2] == Line{">3", "Song B, Arrangement, 1:1:000, Track 1"});
    p.press(NamedKey::Escape);

    // Closing B drops its places; Ctrl-o goes back within A.
    p.type(U":song-close\n");
    CHECK(p.document.song().title == "Song A");
    p.type(U":jumps\n");
    CHECK(p.workspace.listing().size() == 3); // A's two, and here
    p.press(NamedKey::Escape);
    ctrl(U'o');
    CHECK(tick() == 0);
    ctrl(U'o');
    CHECK(p.workspace.message() == "No earlier place in the jump list");
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
    CHECK(keys == std::vector<std::string>{"b", "c", "f", "i", "n", "o", "r", "s", "S", "t", "x"});
    const auto title = std::ranges::find(*shown, std::string{"t"}, &Workspace::Hint::keys);
    REQUIRE(title != shown->end());
    CHECK(title->title == "Set song title");
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

TEST_CASE("song-title sets the title as an undo step, or asks with it to edit and can clear it")
{
    Pickers p;
    REQUIRE(p.document.song().title.empty());
    REQUIRE_FALSE(p.document.modified());

    // Typed in full, then as the alias.
    p.type(U":song-title Morning\n");
    CHECK(p.document.song().title == "Morning");
    CHECK(p.document.modified());
    CHECK(p.workspace.message() == "Titled the song Morning");
    p.type(U":title Morning\n");
    CHECK(p.workspace.message() == "The song's title is already Morning");
    p.type(U"u");
    CHECK(p.document.song().title.empty());
    p.type(U":redo\n");
    CHECK(p.document.song().title == "Morning");

    // , f t asks with the title there to edit; Escape leaves it.
    p.type(U",ft");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    CHECK(p.shows(U"Song title: "));
    CHECK(p.shows(U"Morning"));
    p.press(NamedKey::Escape);
    CHECK(p.document.song().title == "Morning");

    // Editing it, then an empty answer clears it.
    p.type(U",ft Song\n");
    CHECK(p.document.song().title == "Morning Song");
    p.type(U",ft");
    for (int i = 0; i < 12; ++i)
        p.press(NamedKey::Backspace);
    p.press(NamedKey::Enter);
    CHECK(p.document.song().title.empty());
    CHECK(p.workspace.message() == "The song has no title now");
    p.type(U"u");
    CHECK(p.document.song().title == "Morning Song");
}

TEST_CASE("]f and [f step through the open songs, going round at either end")
{
    Pickers p;
    p.type(U"]f");
    CHECK(p.workspace.message() == "Only one song is open");
    p.makeSong("First.modal", "First");
    p.makeSong("Second.modal", "Second");
    p.type(U":e First\n:e Second\n:new\n");
    CHECK_FALSE(p.document.file());

    // Each song keeps its edits as the keys move between them.
    p.document.edit("Rename", {}, [](core::Song& song) { song.title = "Third"; });
    p.type(U"]f");
    CHECK(p.document.song().title == "First");
    CHECK(p.workspace.message() == "Song 1 of 3: First");
    p.type(U"[f");
    CHECK(p.document.song().title == "Third");
    p.type(U"2[f");
    CHECK(p.document.song().title == "First");
    p.press(NamedKey::Tab, kCtrl);
    CHECK(p.document.song().title == "Second");
    p.press(NamedKey::Tab, kCtrl | kShift);
    CHECK(p.document.song().title == "First");
    p.type(U"3]f");
    CHECK(p.document.song().title == "First");
}

TEST_CASE("returning to a song brings back its view, its places, Backspace and gv")
{
    Pickers p;
    p.makeSong("First.modal", "First");
    p.type(U":e First\n");
    p.type(U"grkkkll");
    REQUIRE(p.workspace.shown() == ViewKind::PianoRoll);
    p.type(U"vl");
    p.press(NamedKey::Escape);
    const auto pitch = p.workspace.pianoRoll().pitch();
    const auto time = p.workspace.pianoRoll().time();
    REQUIRE(pitch == 63);
    REQUIRE(time > 0);

    // A new song keeps the kind of view, starting afresh, with no selection for gv.
    p.type(U",fn");
    CHECK(p.workspace.shown() == ViewKind::PianoRoll);
    CHECK(p.workspace.pianoRoll().pitch() == 60);
    CHECK(p.workspace.pianoRoll().time() == 0);
    p.type(U"gv");
    CHECK(p.workspace.mode() == Mode::Normal);
    p.type(U"ge");

    p.type(U"[f");
    CHECK(p.workspace.shown() == ViewKind::PianoRoll);
    CHECK(p.workspace.pianoRoll().pitch() == pitch);
    CHECK(p.workspace.pianoRoll().time() == time);
    p.type(U"gv");
    CHECK(p.workspace.mode() == Mode::Visual);
    p.press(NamedKey::Escape);
    p.press(NamedKey::Backspace);
    CHECK(p.workspace.shown() == ViewKind::Arrangement);

    // And the new song comes back in the event list it was left in.
    p.type(U"]f");
    CHECK(p.workspace.shown() == ViewKind::EventList);
    CHECK_FALSE(p.document.file());
}

TEST_CASE("a section put in another song keeps its notes on the tracks of the same names")
{
    Pickers p;
    auto source = newSong();
    source.title = "Source";
    const auto bass = source.addTrack("Bass");
    source.updateEvents(bass, [&](core::EventList& events)
                        { events.insert({0, core::Note{40, 100, source.ppqn().quarter()}}); });
    REQUIRE(core::writeSongFile(source, p.temporary / "Source.modal").empty());
    // The destination's Piano has the number the source's Bass has.
    auto destination = newSong();
    destination.title = "Destination";
    destination.addTrack("Piano");
    destination.addTrack("Bass");
    REQUIRE(core::writeSongFile(destination, p.temporary / "Destination.modal").empty());

    p.type(U":e Source\ngsY");
    INFO(p.workspace.message());
    p.type(U":e Destination\ngsp");
    INFO(p.workspace.message());
    const auto& song = p.document.song();
    REQUIRE(song.title == "Destination");
    REQUIRE(song.sections().size() == 2);
    REQUIRE(song.tracks().size() == 3);
    CHECK(song.events(song.tracks()[1]->id()).all().empty());
    const auto& put = song.events(song.tracks()[2]->id()).all();
    REQUIRE(put.size() == 1);
    CHECK(put.front().tick == song.sectionStart(1));

    // A track with no match comes too, named in the message.
    p.type(U":e Source\n");
    p.document.edit(
        "Rename", {}, [&](core::Song& s)
        { s.updateTrack(s.tracks()[1]->id(), [](core::Track& t) { t.name = "Cello"; }); });
    p.type(U"gsY:e Destination\ngsp");
    CHECK(p.document.song().tracks().size() == 4);
    CHECK(p.document.song().tracks().back()->name == "Cello");
    CHECK(p.workspace.message().find("added track Cello") != std::string::npos);
}

TEST_CASE("a whole track put in a song with other sections is laid along time")
{
    Pickers p;
    auto source = newSong();
    source.title = "Source";
    const auto lead = source.tracks().front()->id();
    source.updateTrack(lead, [](core::Track& track) { track.name = "Lead"; });
    REQUIRE(source.insertEmptySection(1, "Coda", 2));
    const auto last = source.end() - source.ppqn().quarter();
    source.updateEvents(lead, [&](core::EventList& events)
                        { events.insert({last, core::Note{72, 100, source.ppqn().quarter()}}); });
    REQUIRE(core::writeSongFile(source, p.temporary / "Source.modal").empty());
    auto destination = newSong();
    destination.title = "Destination";
    destination.renameSection(0, "Verse");
    REQUIRE(core::writeSongFile(destination, p.temporary / "Destination.modal").empty());

    p.type(U":e Source\n,ty:e Destination\n,tp");
    const auto& song = p.document.song();
    REQUIRE(song.title == "Destination");
    REQUIRE(song.tracks().size() == 2);
    CHECK(song.tracks()[1]->name == "Lead");
    CHECK(song.end() >= last);
    const auto& put = song.events(song.tracks()[1]->id()).all();
    REQUIRE(put.size() == 1);
    CHECK(put.front().tick == last);
    CHECK(p.workspace.message().starts_with("Put 1 whole track; added section "));
    p.type(U"u");
    CHECK(p.document.song().sections().size() == 1);
}

TEST_CASE("whole bars bring their meter to another song, and a bar's events don't")
{
    Pickers p;
    auto source = newSong();
    source.title = "Source";
    const auto q = source.ppqn().quarter();
    const auto eighth = q / 2;
    source.updateMeter([](core::MeterMap& meter) { meter.set(0, core::TimeSignature{7, 8}); });
    const auto track = source.tracks().front()->id();
    source.updateEvents(track,
                        [&](core::EventList& events)
                        {
                            events.insert({0, core::Note{40, 100, q}});
                            events.insert({7 * eighth, core::Note{43, 100, q}});
                        });
    REQUIRE(core::writeSongFile(source, p.temporary / "Source.modal").empty());
    auto destination = newSong();
    destination.title = "Destination";
    REQUIRE(core::writeSongFile(destination, p.temporary / "Destination.modal").empty());
    const auto ticks = [&]
    {
        std::vector<core::Tick> found;
        const auto& song = p.document.song();
        for (const auto& event : song.events(song.tracks().front()->id()).all())
            found.push_back(event.tick);
        return found;
    };

    // Two whole bars of 7/8 into 4/4: the same times, in the track's own 7/8.
    p.type(U":e Source\n2yy:e Destination\np");
    REQUIRE(p.document.song().title == "Destination");
    CHECK(ticks() == std::vector<core::Tick>{0, 7 * eighth});
    const auto& song = p.document.song();
    const auto& bars = song.meter(song.tracks().front()->id());
    CHECK(bars.signatureAt(0) == core::TimeSignature{7, 8});
    CHECK(bars.toBarBeatTick(7 * eighth) == core::BarBeatTick{2, 1, 0});
    CHECK(song.meter().signatureAt(0) == core::TimeSignature{4, 4});
    CHECK(p.workspace.message().find(", keeping its 7/8") != std::string::npos);

    // A bar's events are notes in time, landing in the destination's bars.
    p.type(U"u:e Source\nyib:e Destination\np");
    CHECK(ticks() == std::vector<core::Tick>{0});
    const auto& after = p.document.song();
    CHECK(after.meter(after.tracks().front()->id()).signatureAt(0) == core::TimeSignature{4, 4});
    CHECK(p.workspace.message().find("keeping") == std::string::npos);
}

TEST_CASE("export without a filename uses the Folder panel for MIDI files")
{
    Pickers p;
    p.temporary.write("Earlier take.mid", "MThd");
    p.temporary.write("Notes.txt", "not MIDI");
    p.makeSong("Session.modal", "Session");
    p.type(U":e Session\n:export\n");
    CHECK(p.shows(U"Export MIDI"));
    CHECK(p.shows(U"Enter for Session.mid"));
    CHECK(p.shows(U"Earlier take.mid"));
    CHECK_FALSE(p.shows(U"Notes.txt"));
    CHECK_FALSE(p.shows(U"Session.modal"));
    p.type(U"\n");
    CHECK(std::filesystem::exists(p.temporary / "Session.mid"));
    CHECK(p.workspace.message() == "Exported Session.mid");

    // A typed name gets .mid, and an existing file is replaced only when confirmed.
    p.type(U":export\nEarlier take\n");
    CHECK(p.workspace.message().starts_with("Replace "));
    p.type(U"n");
    CHECK(p.workspace.mode() == Mode::CommandLine);
    p.press(NamedKey::Escape);
    CHECK(std::filesystem::file_size(p.temporary / "Earlier take.mid") == 4);
    p.type(U":export\nMix\n");
    CHECK(std::filesystem::exists(p.temporary / "Mix.mid"));
}

namespace
{

// A song of three tracks at 100 BPM, Track 1, Kick with a drum map, and Bass, a note on each,
// written to Groove.modal.
void writeGroove(Pickers& p)
{
    auto song = newSong();
    song.title = "Groove";
    song.updateTempo([](core::TempoMap& tempo) { tempo.set(0, core::Tempo::fromBpm(100)); });
    const auto kick = song.addTrack("Kick");
    song.updateTrack(kick,
                     [](core::Track& track)
                     {
                         track.channel = 10;
                         track.drumMap = core::DrumMap{.name = "Studio kit"};
                     });
    const auto bass = song.addTrack("Bass");
    for (const auto& track : song.tracks())
    {
        song.updateEvents(track->id(), [&](core::EventList& events)
                          { events.insert({0, core::Note{36, 100, song.ppqn().quarter()}}); });
    }
    (void)bass;
    REQUIRE(core::writeSongFile(song, p.temporary / "Groove.modal").empty());
}

std::vector<std::string> trackNames(const core::Song& song)
{
    std::vector<std::string> names;
    for (const auto& track : song.tracks())
        names.push_back(track->name);
    return names;
}

} // namespace

TEST_CASE("track-import reads chosen tracks from a song file after the cursor's track")
{
    Pickers p;
    writeGroove(p);
    p.type(U":r Groove 2-3\n");
    CHECK(trackNames(p.document.song()) == std::vector<std::string>{"Track 1", "Kick", "Bass"});
    CHECK(p.workspace.message() ==
          "Read 2 tracks from Groove.modal: Kick, Bass; its tempo was 100, this song plays at 120");
    const auto& kick = *p.document.song().tracks()[1];
    CHECK(kick.channel == 10);
    REQUIRE(kick.drumMap);
    CHECK(kick.drumMap->name == "Studio kit");
    CHECK(p.document.song().events(kick.id()).all().size() == 1);
    p.type(U"u");
    CHECK(p.document.song().tracks().size() == 1);

    // Mistakes say what's wrong and change nothing.
    p.type(U":track-import Groove 9\n");
    CHECK(p.workspace.message() == "No track 9: the file has 3");
    p.type(U":track-import Missing\n");
    CHECK(p.workspace.message().starts_with("Can't find "));
    CHECK(p.document.song().tracks().size() == 1);
}

TEST_CASE("the track-import chooser ticks tracks and runs the line that imports them")
{
    Pickers p;
    writeGroove(p);
    p.type(U",fiGroove\n");
    CHECK(p.shows(U"[ ] 1  Track 1"));
    CHECK(p.shows(U"Studio kit"));
    CHECK(p.shows(U"Import from Groove.modal: 0 of 3 ticked"));
    CHECK(p.shows(U"↑↓ move · Space ticks · a all or none"));

    // Typing isn't a selection here: other keys do nothing.
    p.type(U"2-3");
    CHECK(p.shows(U"0 of 3 ticked"));

    // Space ticks the highlighted track; a ticks every one, or none once every one is.
    p.type(U"j ");
    CHECK(p.shows(U"[x] 2  Kick"));
    p.type(U"a");
    CHECK(p.shows(U"3 of 3 ticked"));
    p.type(U"a");
    CHECK(p.shows(U"0 of 3 ticked"));
    p.type(U" j \n");
    CHECK(trackNames(p.document.song()) == std::vector<std::string>{"Track 1", "Kick", "Bass"});
    CHECK(p.workspace.message().starts_with("Read 2 tracks from Groove.modal"));

    // What Enter ran is on the : prompt's history, to recall and change.
    p.type(U":");
    p.press(NamedKey::Up);
    // The folder as the picker resolved it, which may be spelled otherwise than the temporary
    // directory's own path (as on the hosted Windows runner).
    CHECK(p.shows(U"read "));
    CHECK(p.shows(U"Groove.modal 2-3"));
    p.press(NamedKey::Escape);

    // Escape leaves without importing; Enter with nothing ticked takes the highlighted track.
    p.type(U",fiGroove\n");
    p.press(NamedKey::Escape);
    CHECK(p.document.song().tracks().size() == 3);
    p.type(U",fiGroove\n\n");
    CHECK(p.document.song().tracks().size() == 4);
    CHECK(p.workspace.message().starts_with("Read 1 track from Groove.modal: Track 1 (2)"));
}

TEST_CASE("track-import reads tracks from a MIDI file")
{
    Pickers p;
    auto song = newSong();
    const auto lead = song.tracks().front()->id();
    song.updateTrack(lead, [](core::Track& track) { track.name = "Lead"; });
    song.updateEvents(lead, [&](core::EventList& events)
                      { events.insert({0, core::Note{72, 100, song.ppqn().quarter()}}); });
    REQUIRE(core::exportSmfFile(song, p.temporary / "Tune.mid").error.empty());
    p.type(U":read Tune.mid all\n");
    REQUIRE(p.document.song().tracks().size() == 2);
    CHECK(p.workspace.message().starts_with("Read 1 track from Tune.mid"));
    CHECK(p.document.song().events(p.document.song().tracks()[1]->id()).all().size() == 1);
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
    CHECK(p.shows(decodeUtf8(std::string(20, 'b'))));
}

TEST_CASE("open song folders align even when titles and names are shortened")
{
    Pickers p;
    std::optional<std::size_t> chosen;
    p.workspace.chooseSong({{"Short", "first.modal", "/songs", true},
                            {std::string(60, 'L') + " Secret ending",
                             std::string(60, 'N') + " Hidden name", "/imports", false, true}},
                           0, [&](std::size_t index) { chosen = index; });
    const auto column = [&](std::u32string_view path) -> std::optional<std::size_t>
    {
        for (const auto& region : p.workspace.frame())
            for (const auto& run : region.text)
                if (const auto offset = run.text.find(path); offset != std::u32string::npos)
                    return static_cast<std::size_t>(run.x / 10) + offset;
        return std::nullopt;
    };
    const auto saved = column(U"/songs");
    REQUIRE(saved);
    CHECK(column(U"/imports") == saved);
    const auto name = column(U"first.modal");
    REQUIRE(name);
    CHECK(column(U"NNN") == name);
    CHECK(column(U"Short") < name);
    CHECK(p.shows(U"(MIDI import)"));
    CHECK_FALSE(p.shows(U"Secret ending"));
    CHECK_FALSE(p.shows(U"Hidden name"));
    p.type(U"Hidden name\n");
    CHECK(chosen == 1);
}

TEST_CASE("open songs show titles and file names apart, and the window's title names both")
{
    Pickers p;
    p.makeSong("Dawn.modal", "Morning");
    p.makeSong("Plain.modal", "");
    p.type(U":e Plain\n");
    CHECK(windowTitle(p.document) == "Plain.modal – Modal");
    p.type(U":e Dawn\n");
    CHECK(windowTitle(p.document) == "Morning (Dawn.modal) – Modal");
    p.type(U":title Evening\n");
    CHECK(windowTitle(p.document) == "Evening (Dawn.modal) [+] – Modal");

    // The chooser: title, then file name, then folder; no title, a blank title column.
    p.type(U":song-select\n");
    const auto line = [&](std::u32string_view part) -> std::u32string
    {
        for (const auto& region : p.workspace.frame())
            for (const auto& run : region.text)
                if (run.text.find(part) != std::u32string::npos)
                    return run.text;
        return {};
    };
    const auto titled = line(U"Dawn.modal");
    const auto plain = line(U"Plain.modal");
    REQUIRE(titled.find(U"Evening") != std::u32string::npos);
    CHECK(titled.find(U"Evening") < titled.find(U"Dawn.modal"));
    CHECK(titled.find(U"Dawn.modal") == plain.find(U"Plain.modal"));
    CHECK(plain.find(U"Evening") == std::u32string::npos);
    // Typing narrows by title too.
    p.type(U"Even\n");
    CHECK(p.document.song().title == "Evening");

    // A new song with a title, not yet saved.
    p.type(U":new\n:title Sketch\n");
    CHECK(windowTitle(p.document) == "Sketch (unsaved) [+] – Modal");
    p.type(U"[f");
    CHECK(p.workspace.message() == "Song 2 of 3: Evening");
}

TEST_CASE("each open song keeps its own punch range")
{
    Pickers p;
    p.makeSong("A.modal", "Song A");
    p.makeSong("B.modal", "Song B");
    p.type(U":e A\n");
    p.type(U",rp");
    const auto armedA = p.workspace.punchSpan();
    REQUIRE(armedA);
    p.type(U":e B\n");
    CHECK_FALSE(p.workspace.punchSpan());
    p.type(U"Gv,rp"); // its last bar
    const auto armedB = p.workspace.punchSpan();
    REQUIRE(armedB);
    CHECK(armedB != armedA);
    p.type(U"]f");
    CHECK(p.document.song().title == "Song A");
    CHECK(p.workspace.punchSpan() == armedA);
    p.type(U"]f");
    CHECK(p.workspace.punchSpan() == armedB);
}
