// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>

#include <catch2/catch_test_macros.hpp>

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
        asked = kind == FileKind::Song ? "save" : "export";
        suggested = suggestion;
        pending = std::move(done);
    }
    void quit() override { quitted = true; }
    void songReplaced() override { ++replaced; }
    void report(const CommandResult& result) override { reported = result; }

    // Answers the dialog, as the user would.
    void answer(std::optional<std::filesystem::path> file) { std::exchange(pending, {})(file); }

    std::string asked;
    std::filesystem::path suggested;
    Chosen pending;
    bool quitted = false;
    int replaced = 0;
    std::optional<CommandResult> reported;
};

// The commands on a document, in a fresh directory of their own.
struct Files
{
    Files()
        : directory{std::filesystem::temp_directory_path() / "modal-file-commands-test"}
    {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        addFileCommands(registry, document, host);
    }
    ~Files() { std::filesystem::remove_all(directory); }

    CommandResult run(const std::string& line) { return registry.runLine(line); }
    std::string path(const std::string& name) const { return utf8Text(directory / name); }

    // A change, so there's something unsaved.
    void change()
    {
        document.edit("Rename", {}, [](core::Song& song) { song.title = "Changed"; });
    }

    std::filesystem::path directory;
    core::Document document{newSong()};
    Host host;
    CommandRegistry registry;
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

TEST_CASE(":e opens a song or imports a MIDI file, refusing to lose unsaved changes")
{
    Files f;
    auto song = newSong();
    song.title = "Saved one";
    REQUIRE(core::writeSongFile(song, f.directory / "song.modal").empty());
    REQUIRE(core::exportSmfFile(song, f.directory / "song.mid").error.empty());

    f.change();
    CHECK(f.run("e " + f.path("song.modal")) ==
          CommandResult::error("Unsaved changes: :w saves them, or :e! discards them"));
    CHECK(f.document.song().title == "Changed");

    CHECK(f.run("e! " + f.path("song.modal")) == CommandResult{true, "Opened song.modal", false});
    CHECK(f.document.song().title == "Saved one");
    CHECK(f.document.file() == f.directory / "song.modal");
    CHECK(f.host.replaced == 1);

    // Imported, it has no file of its own, and saving asks where, suggesting a .modal.
    CHECK(f.run("e " + f.path("song.mid")) == CommandResult{true, "Opened song.mid", false});
    CHECK_FALSE(f.document.file());
    CHECK(f.document.importedFrom() == f.directory / "song.mid");
    f.run("w");
    CHECK(f.host.suggested == f.directory / "song.modal"); // beside the MIDI file
    f.host.answer(std::nullopt);

    // With no file, the open dialog.
    CHECK(f.run("e") == CommandResult{});
    CHECK(f.host.asked == "open");
    f.host.answer(f.directory / "song.modal");
    CHECK(f.host.reported == CommandResult{true, "Opened song.modal", false});

    // A file that can't be read leaves the song as it was.
    const auto missing = f.run("e " + f.path("missing.modal"));
    CHECK(missing.warning);
    CHECK(missing.message.starts_with("Can't open "));
    CHECK(f.document.file() == f.directory / "song.modal");
    CHECK(f.host.replaced == 3);
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

TEST_CASE(":new and :q refuse to lose unsaved changes unless given !")
{
    Files f;
    CHECK(f.run("q") == CommandResult{true});
    CHECK(f.host.quitted);

    Files changed;
    changed.change();
    CHECK(changed.run("new") ==
          CommandResult::error("Unsaved changes: :w saves them, or :new! discards them"));
    CHECK(changed.run("q") ==
          CommandResult::error("Unsaved changes: :w saves them, or :q! quits anyway"));
    CHECK_FALSE(changed.host.quitted);
    CHECK(changed.run("new!") == CommandResult{true});
    CHECK(changed.document.song().title.empty());
    CHECK_FALSE(changed.document.modified());
    CHECK(changed.host.replaced == 1);

    changed.change();
    CHECK(changed.run("quit!") == CommandResult{true});
    CHECK(changed.host.quitted);
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
