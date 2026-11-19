// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/recording_session.hpp>
#include <modal/core/song.hpp>
#include <modal/ui/commands.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace modal::ui
{

// What the file commands need from the program: the system's file dialogs, quitting, and
// somewhere to show what happened.
class FileHost
{
public:
    virtual ~FileHost() = default;

    enum class FileKind : std::uint8_t
    {
        Song,   // a song to open: .modal, or a MIDI file to import
        Midi,   // a MIDI file to export
        Config, // a config file, written by write-config
        DrumMap,
    };

    // Asks for a file to open or to save to, suggesting one (beside the song's file, or the
    // file it was imported from, if it has one), and calls done with it, or with
    // nothing if the dialog was cancelled. done may be called later, on the program's own
    // thread, after other events, so the command has finished by then.
    using Chosen = std::function<void(std::optional<std::filesystem::path>)>;
    virtual void chooseOpen(Chosen done) = 0;
    virtual void chooseOpenFile(FileKind, const std::filesystem::path&, Chosen done)
    {
        chooseOpen(std::move(done));
    }
    virtual void chooseSave(FileKind kind, const std::filesystem::path& suggested, Chosen done) = 0;

    virtual void quit() = 0;

    // The document has a new song, opened or new, to show from the start.
    virtual void songReplaced() = 0;

    // Shows the outcome of a command finished after a dialog, as a command's result would be.
    virtual void report(const CommandResult& result) = 0;
};

// The file commands (decided in milestone 4, following Vim):
//
//   edit, e [FILE]     opens a song, or imports a MIDI file (.mid, .midi); with no file, the
//                      open dialog; refused with unsaved changes, unless edit!
//   write, w [FILE]    saves the song, to its file or else through the save dialog; with a
//                      file, saves it there and makes that the song's file, refusing to write
//                      over another file unless write!
//   export [FILE]      writes a Standard MIDI File, through the save dialog if none is given;
//                      refusing to write over a file unless export!
//   new                starts a new song; refused with unsaved changes, unless new!
//   quit, q            quits; refused with unsaved changes, unless quit!
//   wq                 saves, then quits
//
// A FILE without an extension is given .modal (or .mid for export). A relative FILE is in the
// song's folder (see songFolder), and ~ is the home folder.
std::span<const CommandInfo> fileCommands();

// Gives the registry file commands for the session's document. The session, its document
// and the host outlive the commands' use of them. home is what ~ stands for.
void addFileCommands(CommandRegistry& registry, core::RecordingSession& recording, FileHost& host,
                     std::optional<std::filesystem::path> home = std::nullopt);

// Where a song's files typed with a relative path are (decided November 2026, rather than the
// program's current directory, which means little when it's started from a menu): the folder
// of the song's file, or of the file it was imported from; or else the home folder; or else
// the current directory.
std::filesystem::path songFolder(const core::Document& document,
                                 const std::optional<std::filesystem::path>& home);

// Opens a song file, or imports a MIDI file, into the document, replacing its song: whether
// it did, and what went wrong, or else the first warning and how many more, if any.
struct OpenResult
{
    bool opened = false;
    std::string message;
};
OpenResult openFile(core::Document& document, const std::filesystem::path& file);

// The song a new song starts as: one empty track, Track 1 on channel 1, and the conductor's
// one section at 120 BPM in 4/4.
core::Song newSong();

// When Modal is ending unexpectedly, as for an exception nothing caught: saves a song with
// unsaved changes as recovered-STAMP.modal in the state directory, and leaves a note of it
// there for the next start (decided 17 November 2026, after a code review, so that such an
// end doesn't lose the work). The stamp tells one recovery from another, and must do in a
// file's name. Returns the file, or nothing if the song had no changes or couldn't be saved.
// Never throws.
std::optional<std::filesystem::path> writeRecovery(const core::Document& document,
                                                   const std::filesystem::path& stateDirectory,
                                                   std::string_view stamp) noexcept;

// At the start: what to say of a song recovered at the last end, once, or nothing.
std::optional<std::string> takeRecoveryNotice(const std::filesystem::path& stateDirectory);

} // namespace modal::ui
