// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/smf_builder.hpp"
#include "support/temp_directory.hpp"

#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

// A host whose dialogs wait for the test to answer them.
class Host final : public FileHost
{
public:
    void chooseOpen(Chosen done) override
    {
        asked = "open";
        pending = std::move(done);
    }
    void chooseSave(FileKind kind, const std::filesystem::path& suggestion, Chosen done) override
    {
        asked = kind == FileKind::Song ? "save" : kind == FileKind::Midi ? "export" : "config";
        suggested = suggestion;
        pending = std::move(done);
    }
    void quit() override { quitted = true; }
    void songReplaced() override { ++replaced; }
    void report(const CommandResult& result) override { reported = result; }
    void songUsed(const std::filesystem::path& file, const std::optional<std::filesystem::path>&,
                  const std::string& title) override
    {
        used.emplace_back(utf8Text(file.filename()), title);
    }

    // Answers the dialog, as the user would.
    void answer(std::optional<std::filesystem::path> file) { std::exchange(pending, {})(file); }

    std::string asked;
    std::filesystem::path suggested;
    Chosen pending;
    bool quitted = false;
    int replaced = 0;
    std::optional<CommandResult> reported;
    std::vector<std::pair<std::string, std::string>> used; // file names and titles
};

// The commands on a document, in a fresh directory of their own, which is home too.
struct Files
{
    Files() { unsaved = addFileCommands(registry, recording, host, directory); }

    CommandResult run(const std::string& line) { return registry.runLine(line); }
    std::string path(const std::string& name) const { return utf8Text(directory / name); }

    // A change, so there's something unsaved.
    void change(std::string title = "Changed")
    {
        document.edit("Rename", {}, [&](core::Song& song) { song.title = title; });
    }
    std::string titleIn(const std::string& name) const
    {
        return core::readSongFile(directory / name).song->title;
    }

    test::TempDirectory temporary{"modal-file-commands-test"};
    std::filesystem::path directory = temporary.path();
    core::Document document{newSong()};
    core::RecordingSession recording{document};
    Host host;
    CommandRegistry registry;
    std::function<std::vector<std::string>()> unsaved;
};

} // namespace

TEST_CASE("a new song has one empty track on channel 1, and isn't modified")
{
    const auto song = newSong();
    REQUIRE(song.tracks().size() == 1);
    CHECK(song.tracks()[0]->name == "Track 1");
    CHECK(song.tracks()[0]->channel == 1);
    CHECK(song.events(song.tracks()[0]->id()).size() == 0);
    CHECK(song.sections().size() == 1);
    CHECK_FALSE(core::Document{newSong()}.modified());
}

TEST_CASE("file commands consult the recording session independently of the preview")
{
    Files f;
    f.document.setPreview([](const core::Song& song) { return song; });
    REQUIRE(f.document.hasPreview());
    CHECK_FALSE(f.recording.pending());
    CHECK(f.run("w idle-preview").message == "Saved idle-preview.modal");
    CHECK(std::filesystem::exists(f.directory / "idle-preview.modal"));
}

TEST_CASE(":w saves to the song's file, or asks for one, and :w FILE saves there")
{
    Files f;
    f.change();
    CHECK(f.run("w") == CommandResult{});
    CHECK(f.host.asked == "save");
    CHECK(f.host.suggested == "Untitled.modal");
    f.host.answer(f.directory / "first.modal");
    CHECK(f.host.reported == CommandResult{true, "Saved first.modal", false});
    CHECK_FALSE(f.document.modified());
    CHECK(f.document.file() == f.directory / "first.modal");
    CHECK(std::filesystem::exists(f.directory / "first.modal"));

    // Cancelling the dialog saves nothing.
    Files cancelled;
    cancelled.change();
    cancelled.run("w");
    cancelled.host.answer(std::nullopt);
    CHECK(cancelled.document.modified());
    CHECK_FALSE(cancelled.host.reported);

    // Its own file again, with no dialog.
    f.change();
    f.host.asked.clear();
    CHECK(f.run("w") == CommandResult{true, "Saved first.modal", false});
    CHECK(f.host.asked.empty());

    // Another file, given .modal if it has no extension, becomes the song's file.
    CHECK(f.run("w " + f.path("second")) == CommandResult{true, "Saved second.modal", false});
    CHECK(f.document.file() == f.directory / "second.modal");

    // Writing over a file that isn't the song's own needs !.
    const auto refused = f.run("w " + f.path("first.modal"));
    CHECK(refused.warning);
    CHECK(refused.message.starts_with("first.modal exists: :w! "));
    CHECK(f.document.file() == f.directory / "second.modal");
    CHECK(f.run("w! " + f.path("first.modal")) == CommandResult{true, "Saved first.modal", false});
    CHECK(f.run("w \"" + f.path("first.modal") + "\"") ==
          CommandResult{true, "Saved first.modal", false}); // its own now
}

TEST_CASE("a relative file is in the song's folder, or the home folder for a new song")
{
    Files f;
    f.change();
    CHECK(f.run("w first") == CommandResult{true, "Saved first.modal", false});
    CHECK(f.document.file() == f.directory / "first.modal");
    std::filesystem::create_directories(f.directory / "My Songs");
    CHECK(f.run("w \"My Songs/second\"") == CommandResult{true, "Saved second.modal", false});
    CHECK(f.document.file() == f.directory / "My Songs" / "second.modal");

    // Beside the song's file now, for :w, :export and :e.
    CHECK(f.run("w third") == CommandResult{true, "Saved third.modal", false});
    CHECK(f.document.file() == f.directory / "My Songs" / "third.modal");
    CHECK(f.run("export third") == CommandResult{false, "Exported third.mid", false});
    CHECK(std::filesystem::exists(f.directory / "My Songs" / "third.mid"));
    CHECK(f.run("e second") == CommandResult{true, "Opened second.modal", false});
    CHECK(f.document.file() == f.directory / "My Songs" / "second.modal");

    // ~ is the home folder.
    CHECK(f.run("e ~/first") == CommandResult{true, "Opened first.modal", false});
    CHECK(f.document.file() == f.directory / "first.modal");
    CHECK(songFolder(f.document, std::nullopt) == f.directory);
    CHECK(songFolder(core::Document{newSong()}, f.directory) == f.directory);
}

TEST_CASE("opening and switching songs preserves their own changes and undo")
{
    Files f;
    auto song = newSong();
    song.title = "Saved one";
    const auto file = f.directory / "song.modal";
    REQUIRE(core::writeSongFile(song, file).empty());
    REQUIRE(core::exportSmfFile(song, f.directory / "song.mid").error.empty());

    f.change();
    const auto version = f.document.version();
    CHECK(f.run("e " + f.path("song.modal")) == CommandResult{true, "Opened song.modal", false});
    CHECK(f.document.song().title == "Saved one");
    CHECK(f.document.file() == file);
    CHECK(f.document.version() != version);
    CHECK_FALSE(f.document.canUndo());
    REQUIRE(f.document.edit("Rename", {}, [](core::Song& song) { song.title = "Edited one"; }));

    CHECK(f.run("e " + f.path("song.mid")) == CommandResult{true, "Opened song.mid", false});
    CHECK_FALSE(f.document.file());
    CHECK(f.document.importedFrom() == f.directory / "song.mid");
    f.run("w");
    CHECK(f.host.suggested == file);
    f.host.answer(std::nullopt);

    CHECK(f.run("e") == CommandResult{});
    f.host.answer(file);
    REQUIRE(f.host.reported);
    CHECK_FALSE(f.host.reported->refused);
    CHECK(f.document.song().title == "Edited one");
    CHECK(f.document.modified());
    REQUIRE(f.document.undo());
    CHECK(f.document.song().title == "Saved one");
    CHECK_FALSE(f.document.modified());

    const auto missing = f.run("e missing");
    CHECK(missing.refused);
    CHECK(missing.message.starts_with("Can't open "));
    CHECK(f.document.file() == file);
    CHECK(f.host.replaced == 3);
    CHECK(f.run("q").refused); // the original untitled song is still modified
}

TEST_CASE("Importing MIDI with a warning succeeds without refusing the command")
{
    Files f;
    const auto file = f.directory / "warning.mid";
    const core::Ppqn ppqn{96};
    test::SmfTrack track;
    track
        .event(0, {0x80, 50, 0}) // an unmatched note-off is reported, but import succeeds
        .event(0, {0x90, 60, 100})
        .event(static_cast<std::uint32_t>(ppqn.quarter()), {0x80, 60, 0})
        .end();
    const auto bytes = test::makeSmf(0, static_cast<std::uint16_t>(ppqn.value), {track});
    {
        std::ofstream out{file, std::ios::binary};
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }
    const auto result = f.run("e " + utf8Text(file));
    CHECK(result.warning);
    CHECK_FALSE(result.refused);
    CHECK(result.message.find("without a note-on") != std::string::npos);
    CHECK(f.host.replaced == 1);
    CHECK(f.document.importedFrom() == file);
    CHECK(f.run("e " + f.path("missing.mid")).refused);
}

TEST_CASE(":export writes a MIDI file, refusing to write over one unless given !")
{
    Files f;
    CHECK(f.run("export " + f.path("out")) == CommandResult{false, "Exported out.mid", false});
    CHECK(std::filesystem::exists(f.directory / "out.mid"));
    CHECK(f.run("export " + f.path("out.mid")).warning);
    CHECK(f.run("export! " + f.path("out.mid")) == CommandResult{false, "Exported out.mid", false});

    CHECK(f.run("export") == CommandResult{});
    CHECK(f.host.asked == "export");
    CHECK(f.host.suggested == "Untitled.mid");
}

TEST_CASE(":w refuses a MIDI file's name and :export a song's, from a dialog too")
{
    Files files;
    const auto saved = files.run("w " + files.path("song.MID"));
    CHECK(saved.refused);
    CHECK(saved.message == "song.MID is a MIDI file's name: :export writes one, :w a song");
    CHECK_FALSE(std::filesystem::exists(files.directory / "song.MID"));
    CHECK_FALSE(files.document.file());
    CHECK(files.run("w! " + files.path("song.midi")).refused);

    const auto exported = files.run("export " + files.path("song.modal"));
    CHECK(exported.refused);
    CHECK(exported.message == "song.modal is a song's name: :w saves one, :export a MIDI file");
    CHECK_FALSE(std::filesystem::exists(files.directory / "song.modal"));

    files.run("w");
    REQUIRE(files.host.asked == "save");
    files.host.answer(files.directory / "chosen.mid");
    REQUIRE(files.host.reported);
    CHECK(files.host.reported->refused);
    CHECK_FALSE(std::filesystem::exists(files.directory / "chosen.mid"));
    CHECK_FALSE(files.document.file());
}

TEST_CASE("new and close protect open songs with unsaved changes")
{
    Files f;
    f.change();
    CHECK(f.run("new") == CommandResult{true});
    CHECK(f.document.song().title.empty());
    CHECK(f.run("q").refused);
    CHECK_FALSE(f.host.quitted);
    CHECK(f.run("song-close") == CommandResult{true});
    CHECK(f.document.song().title == "Changed");
    CHECK(f.document.modified());
    CHECK(f.run("song-close").refused);
    CHECK(f.run("song-close!") == CommandResult{true});
    CHECK_FALSE(f.document.modified());
    CHECK(f.document.song().title.empty());
    CHECK(f.run("q") == CommandResult{true});
    CHECK(f.host.quitted);
}

TEST_CASE("saving and quitting guard other open songs and reject stale picker results")
{
    Files f;
    f.change();
    REQUIRE(f.run("w first").changed);
    REQUIRE(f.document.edit("Rename", {}, [](core::Song& song) { song.title = "Unsaved edit"; }));
    CHECK(f.run("new") == CommandResult{true});
    CHECK(f.run("w! first").refused);
    CHECK(core::readSongFile(f.directory / "first.modal").song->title == "Changed");

    CHECK_FALSE(f.run("w").changed);
    REQUIRE(f.run("e first").changed);
    f.host.answer(f.directory / "stale.modal");
    REQUIRE(f.host.reported);
    CHECK(f.host.reported->refused);
    CHECK_FALSE(std::filesystem::exists(f.directory / "stale.modal"));

    CHECK(f.run("new") == CommandResult{true});
    CHECK(f.run("wq") == CommandResult{});
    f.host.answer(f.directory / "second.modal");
    REQUIRE(f.host.reported);
    CHECK(f.host.reported->refused);
    CHECK(std::filesystem::exists(f.directory / "second.modal"));
    CHECK_FALSE(f.host.quitted);
    CHECK(f.run("q!") == CommandResult{true});
    CHECK(f.host.quitted);
}

TEST_CASE(":wa saves every changed song, asking for a file for each new one in turn")
{
    Files f;
    f.change("One");
    REQUIRE(f.run("w one").changed);
    f.change("One changed");
    REQUIRE(f.run("new").changed);
    f.change("Two");
    REQUIRE(f.run("w two").changed); // saved, so left alone
    REQUIRE(f.run("new").changed);
    f.change("Three");
    REQUIRE(f.run("new").changed);
    f.change("Four");
    f.host.used.clear();

    // Songs with files are saved at once, then each new one is shown and asked about.
    CHECK(f.run("wa") == CommandResult{});
    CHECK(f.titleIn("one.modal") == "One changed");
    CHECK(f.host.asked == "save");
    CHECK(f.host.suggested == "Untitled.modal");
    CHECK(f.document.song().title == "Three");
    f.host.answer(f.directory / "three.modal");
    CHECK(f.document.song().title == "Four");
    CHECK(f.host.pending);
    f.host.answer(f.directory / "four.modal");
    REQUIRE(f.host.reported);
    CHECK(*f.host.reported == CommandResult{true, "Saved 3 songs", false});
    CHECK(f.titleIn("three.modal") == "Three");
    CHECK(f.titleIn("four.modal") == "Four");
    CHECK_FALSE(f.document.modified());
    // The recent list hears of each by its own title, not the shown song's.
    CHECK(f.host.used ==
          std::vector<std::pair<std::string, std::string>>{
              {"one.modal", "One changed"}, {"three.modal", "Three"}, {"four.modal", "Four"}});

    CHECK(f.run("wa") == CommandResult{false, "No songs have unsaved changes", false});
    f.change("Four again");
    CHECK(f.run("wa") == CommandResult{true, "Saved four.modal", false});

    // Cancelling the dialog stops there, saying what was saved.
    f.change("Four once more");
    REQUIRE(f.run("new").changed);
    f.change("Five");
    CHECK(f.run("wa") == CommandResult{});
    f.host.answer(std::nullopt);
    CHECK(*f.host.reported ==
          CommandResult{false, "Saved four.modal. This song isn't saved: :wa asks again", true});
    CHECK(f.document.modified());
    CHECK_FALSE(f.host.quitted);
}

TEST_CASE(":wqa saves every changed song and quits, :qa quits as :q does")
{
    Files f;
    f.change("One");
    REQUIRE(f.run("w one").changed);
    f.change("One changed");
    REQUIRE(f.run("new").changed);
    f.change("Two");

    // :qa refuses while songs are unsaved, pointing to :wqa; :qa! quits anyway.
    const auto refused = f.run("qa");
    CHECK(refused.refused);
    CHECK(refused.message.find(":wqa saves them all") != std::string::npos);
    CHECK_FALSE(f.host.quitted);

    // :xa is :wqa; quitting waits for the new song's file.
    CHECK(f.run("xa") == CommandResult{});
    CHECK_FALSE(f.host.quitted);
    f.host.answer(f.directory / "two.modal");
    CHECK(f.host.quitted);
    CHECK(f.titleIn("one.modal") == "One changed");
    CHECK(f.titleIn("two.modal") == "Two");

    Files g;
    g.change();
    CHECK(g.run("qa!") == CommandResult{true});
    CHECK(g.host.quitted);
}

TEST_CASE("closing the window names every unsaved song, by title or else by file")
{
    Files f;
    const auto addTrack = [&]
    { f.document.edit("Add a track", {}, [](core::Song& song) { song.addTrack(); }); };
    CHECK(f.unsaved().empty());

    // A song with a file and no title, by its file; one with a title, by that; a new one by
    // its number; an unchanged one not at all.
    addTrack();
    REQUIRE(f.run("w plain").changed);
    addTrack();
    REQUIRE(f.run("new").changed);
    f.change("Morning");
    REQUIRE(f.run("new").changed);
    addTrack();
    REQUIRE(f.run("new").changed);
    CHECK(f.unsaved() == std::vector<std::string>{"plain.modal", "Morning", "Untitled 3"});
    CHECK(closingQuestion(f.unsaved()) ==
          "3 songs have changes not saved: plain.modal, Morning and Untitled 3. Save them all "
          "before closing?");

    REQUIRE(f.run("song-close!").changed);
    REQUIRE(f.run("song-close!").changed);
    REQUIRE(f.run("song-close!").changed);
    CHECK(f.unsaved() == std::vector<std::string>{"plain.modal"});
    CHECK(closingQuestion(f.unsaved()) ==
          "plain.modal has changes not saved. Save them before closing?");

    CHECK(closingQuestion({"A", "B", "C", "D"}) ==
          "4 songs have changes not saved: A, B, C and D. Save them all before closing?");
    CHECK(closingQuestion({"A", "B", "C", "D", "E", "F"}) ==
          "6 songs have changes not saved: A, B, C, D and 2 more. Save them all before closing?");
}

TEST_CASE(":wq saves and quits, once the song has a file")
{
    Files f;
    f.change();
    CHECK(f.run("wq") == CommandResult{});
    CHECK_FALSE(f.host.quitted); // until the dialog's answered
    f.host.answer(f.directory / "song.modal");
    CHECK(f.host.quitted);
    CHECK_FALSE(f.document.modified());
}

TEST_CASE("an unexpected end saves a song with unsaved changes, and the next start says where")
{
    Files files;
    CHECK_FALSE(writeRecovery(files.document, files.directory, "20261117-094107"));
    CHECK_FALSE(takeRecoveryNotice(files.directory)); // nothing unsaved, so nothing kept

    files.change();
    const auto file = writeRecovery(files.document, files.directory / "state", "20261117-094107");
    REQUIRE(file);
    CHECK(*file == files.directory / "state" / "recovered-20261117-094107.modal");
    const auto back = core::readSongFile(*file);
    REQUIRE(back.song);
    CHECK(back.song->title == "Changed");
    CHECK(files.document.modified()); // still unsaved, as far as the document goes

    const auto notice = takeRecoveryNotice(files.directory / "state");
    REQUIRE(notice);
    CHECK(*notice == "Modal ended unexpectedly last time: the song's unsaved changes are in " +
                         utf8Text(*file) + " (:e opens it)");
    CHECK_FALSE(takeRecoveryNotice(files.directory / "state")); // said once
    CHECK(std::filesystem::exists(*file));                      // the song stays
}
