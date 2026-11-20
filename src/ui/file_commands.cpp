// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/files.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 11> kFileCommands{{
    {.name = "edit",
     .description = "Open a song, or import a MIDI file; with no file, choose one",
     .aliases = "e",
     .usage = "[FILE]",
     .bang = true,
     .choices = {Choices::File}},
    {.name = "write",
     .description = "Save the song, to its file or to FILE",
     .aliases = "w",
     .usage = "[FILE]",
     .bang = true,
     .choices = {Choices::File}},
    {.name = "export",
     .description = "Export the song as a Standard MIDI File",
     .usage = "[FILE]",
     .bang = true,
     .choices = {Choices::File}},
    {.name = "new", .description = "Start a new song", .bang = true},
    {.name = "quit", .description = "Quit", .aliases = "q", .bang = true},
    {.name = "wq", .description = "Save the song, then quit", .keys = false},
    {.name = "song-select", .description = "Select an open song"},
    {.name = "song-close", .description = "Close the current song", .bang = true},
    {.name = "song-save-as",
     .description = "Save the song as another file; with no file, choose one",
     .aliases = "saveas",
     .usage = "[FILE]",
     .bang = true,
     .choices = {Choices::File}},
    {.name = "song-next",
     .description = "Switch to the next open song, round to the first after the last",
     .count = true},
    {.name = "song-previous",
     .description = "Switch to the open song before, round to the last before the first",
     .count = true},
}};

// The argument as a path: the rest of the line, without quotes around it, from a folder,
// given an extension if it has none.
std::optional<std::filesystem::path> fileArgument(std::string_view text, std::string_view extension,
                                                  const std::filesystem::path& folder,
                                                  const std::optional<std::filesystem::path>& home)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return std::nullopt;
    }
    text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
    auto path = typedPath(text, folder, home);
    if (!path.has_extension())
    {
        path += utf8Path(extension);
    }
    return path;
}

std::string named(const std::filesystem::path& file)
{
    return utf8Text(file.filename());
}

// A file's extension in lower case, as .mid for Song.MID.
std::string lowerExtension(const std::filesystem::path& file)
{
    auto extension = utf8Text(file.extension());
    std::ranges::transform(extension, extension.begin(), [](char c)
                           { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return extension;
}

bool namesMidi(const std::filesystem::path& file)
{
    const auto extension = lowerExtension(file);
    return extension == ".mid" || extension == ".midi";
}

// Why a file's name is the wrong kind for what's being written to it, if it is: a song saved
// as Song.mid would be found not to be a MIDI file when opened again (found in a code review,
// November 2026).
std::optional<CommandResult> wrongKind(const std::filesystem::path& file, bool song)
{
    if (song && namesMidi(file))
    {
        return CommandResult::error(named(file) +
                                    " is a MIDI file's name: :export writes one, :w a song");
    }
    if (!song && lowerExtension(file) == ".modal")
    {
        return CommandResult::error(named(file) +
                                    " is a song's name: :w saves one, :export a MIDI file");
    }
    return std::nullopt;
}

// Whether writing to a file would write over another one.
bool overwrites(const std::filesystem::path& file, const std::optional<std::filesystem::path>& own)
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
    {
        return false;
    }
    return !own || !std::filesystem::equivalent(file, *own, error);
}

CommandResult unsaved(std::string_view command)
{
    return CommandResult::error("Unsaved changes: :w saves them, or :" + std::string{command} +
                                "! " + (command == "q" ? "quits anyway" : "discards them"));
}

std::filesystem::path absolutePath(const std::filesystem::path& file)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(file, error);
    return error ? file : absolute;
}

bool sameFile(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error))
        return true;
    const auto leftPath = std::filesystem::weakly_canonical(absolutePath(left), error);
    if (error)
        return left == right;
    const auto rightPath = std::filesystem::weakly_canonical(absolutePath(right), error);
    return error ? left == right : leftPath == rightPath;
}

class FileCommands
{
public:
    FileCommands(core::RecordingSession& recording, FileHost& host,
                 std::optional<std::filesystem::path> home)
        : recording_(recording)
        , document_(recording.document())
        , host_(host)
        , home_(std::move(home))
    {
        songs_.push_back({nullptr, 1});
    }

    CommandResult edit(const CommandCall& call)
    {
        if (const auto file = argument(call, ".modal"))
        {
            return opened(*file, call.bang);
        }
        host_.chooseOpen(
            [this, force = call.bang, activation = activation_](auto file)
            {
                if (file)
                {
                    host_.report(activation == activation_ ? opened(*file, force) : songChanged());
                }
            });
        return {};
    }

    // Saving: to FILE when given, or to the song's own file, or else to one chosen in the
    // picker, as saving as always does.
    CommandResult write(const CommandCall& call, bool thenQuit, bool saveAs = false)
    {
        if (const auto file = argument(call, ".modal"))
        {
            if (auto wrong = wrongKind(*file, true))
            {
                return *wrong;
            }
            if (!call.bang && overwrites(*file, document_.file()))
            {
                return CommandResult::error(named(*file) +
                                            " exists: :" + (saveAs ? "song-save-as! " : "w! ") +
                                            std::string{call.arguments} + " writes over it");
            }
            return saved(*file, thenQuit);
        }
        if (document_.file() && !saveAs)
        {
            return saved(*document_.file(), thenQuit);
        }
        const auto& imported = document_.importedFrom();
        auto suggested = document_.file() ? *document_.file()
                         : imported ? std::filesystem::path{*imported}.replace_extension(".modal")
                                    : std::filesystem::path{"Untitled.modal"};
        host_.chooseSave(FileHost::FileKind::Song, suggested,
                         [this, thenQuit, activation = activation_](auto file)
                         {
                             if (file)
                             {
                                 host_.report(activation == activation_ ? saved(*file, thenQuit)
                                                                        : songChanged());
                             }
                         });
        return {};
    }

    CommandResult exportMidi(const CommandCall& call)
    {
        if (const auto file = argument(call, ".mid"))
        {
            if (auto wrong = wrongKind(*file, false))
            {
                return *wrong;
            }
            if (!call.bang && overwrites(*file, std::nullopt))
            {
                return CommandResult::error(named(*file) + " exists: :export! " +
                                            std::string{call.arguments} + " writes over it");
            }
            return exported(*file);
        }
        const auto& file = document_.file() ? document_.file() : document_.importedFrom();
        auto suggested = file ? std::filesystem::path{*file}.replace_extension(".mid")
                              : std::filesystem::path{"Untitled.mid"};
        host_.chooseSave(FileHost::FileKind::Midi, suggested,
                         [this, activation = activation_](auto chosen)
                         {
                             if (chosen)
                             {
                                 host_.report(activation == activation_ ? exported(*chosen)
                                                                        : songChanged());
                             }
                         });
        return {};
    }

    CommandResult newSong(const CommandCall& call)
    {
        return activateNew(std::make_unique<core::Document>(ui::newSong()), call.bang);
    }

    CommandResult quit(const CommandCall& call)
    {
        if (!call.bang && otherModified())
            return CommandResult::error("Other open songs have unsaved changes. Use :song-select");
        if (document_.modified() && !call.bang)
        {
            return unsaved("q");
        }
        host_.quit();
        return {true};
    }

    bool otherModified() const
    {
        return std::ranges::any_of(songs_, [](const auto& song)
                                   { return song.document && song.document->modified(); });
    }

    CommandResult selectSong()
    {
        std::vector<OpenSong> choices;
        for (std::size_t i = 0; i < songs_.size(); ++i)
            choices.push_back(describe(i));
        host_.chooseSong(
            std::move(choices), active_, [this, activation = activation_](auto index)
            { host_.report(activation == activation_ ? activate(index, false) : songChanged()); });
        return {true};
    }

    // Steps through the open songs in the chooser's order, going round at either end, as
    // there are usually only a few.
    CommandResult stepSong(int count, bool forward)
    {
        const auto open = songs_.size();
        if (open < 2)
            return {false, "Only one song is open", false, true};
        const auto steps = static_cast<std::size_t>(std::max(count, 1)) % open;
        const auto index = forward ? (active_ + steps) % open : (active_ + open - steps) % open;
        if (auto switched = activate(index, false); switched.refused)
            return switched;
        return {true,
                "Song " + std::to_string(active_ + 1) + " of " + std::to_string(open) + ": " +
                    describe(active_).name,
                false};
    }

    CommandResult closeSong(const CommandCall& call)
    {
        if (document_.modified() && !call.bang)
            return unsaved("song-close");
        if (songs_.size() == 1)
            return activateNew(std::make_unique<core::Document>(ui::newSong()), true);
        return activate(active_ == 0 ? 1 : active_ - 1, true);
    }

private:
    struct SongSlot
    {
        std::unique_ptr<core::Document> document;
        std::size_t number;
        std::shared_ptr<void> place = nullptr; // the window's views of it while inactive
    };

    const core::Document& at(std::size_t index) const
    {
        return index == active_ ? document_ : *songs_[index].document;
    }

    OpenSong describe(std::size_t index) const
    {
        const auto& song = at(index);
        const auto& file = song.file() ? song.file() : song.importedFrom();
        const auto& title = song.underlying().title;
        auto name = !title.empty() ? title : file ? named(*file) : std::string{"Untitled"};
        if (!file)
            return {name + " " + std::to_string(songs_[index].number), "Unsaved", song.modified()};
        const auto location = utf8Text(absolutePath(*file).lexically_normal());
        return {std::move(name), location, song.modified(), !song.file().has_value()};
    }

    std::optional<std::size_t> openedFile(const std::filesystem::path& file,
                                          bool includeImports) const
    {
        for (std::size_t i = 0; i < songs_.size(); ++i)
        {
            const auto& song = at(i);
            const auto& source = includeImports && !song.file() ? song.importedFrom() : song.file();
            if (source && sameFile(file, *source))
                return i;
        }
        return std::nullopt;
    }

    static CommandResult songChanged()
    {
        return CommandResult::error("The active song changed. Open the chooser again");
    }

    std::optional<CommandResult> beforeChange() const
    {
        if (auto refusal = recording_.fileOperationRefusal())
            return CommandResult::error(std::move(*refusal));
        if (!document_.canSwitchSong())
            return CommandResult::error("Finish the current edit before changing songs");
        if (!host_.stopSongPlayback())
            return CommandResult::error("The engine is busy. Try switching songs again");
        return std::nullopt;
    }

    void changedSong()
    {
        ++activation_;
        host_.songReplaced();
    }

    CommandResult activate(std::size_t index, bool closeCurrent)
    {
        if (index >= songs_.size())
            return songChanged();
        if (index == active_)
            return {};
        if (auto refusal = beforeChange())
            return *refusal;
        const auto previous = active_;
        auto leaving = closeCurrent ? nullptr : host_.leaveSong();
        document_.swapSong(*songs_[index].document);
        songs_[previous].document = std::move(songs_[index].document);
        songs_[previous].place = std::move(leaving);
        auto returning = std::move(songs_[index].place);
        active_ = index;
        if (closeCurrent)
        {
            songs_.erase(songs_.begin() + static_cast<std::ptrdiff_t>(previous));
            if (previous < index)
                --active_;
        }
        ++activation_;
        host_.returnToSong(std::move(returning));
        return {true};
    }

    CommandResult activateNew(std::unique_ptr<core::Document> song, bool replace)
    {
        if (!replace)
            songs_.reserve(songs_.size() + 1);
        if (auto refusal = beforeChange())
            return *refusal;
        auto leaving = replace ? nullptr : host_.leaveSong();
        document_.swapSong(*song);
        if (!replace)
        {
            songs_[active_].document = std::move(song);
            songs_[active_].place = std::move(leaving);
            active_ = songs_.size();
            songs_.push_back({nullptr, nextNumber_++});
        }
        changedSong();
        return {true};
    }

    std::optional<std::filesystem::path> argument(const CommandCall& call,
                                                  std::string_view extension) const
    {
        return fileArgument(call.arguments, extension,
                            songFolder(document_, home_, host_.songsFolder()), home_);
    }

    CommandResult opened(const std::filesystem::path& file, bool force)
    {
        if (auto refusal = recording_.fileOperationRefusal())
        {
            return CommandResult::error(std::move(*refusal));
        }
        if (const auto existing = openedFile(file, true);
            existing && (!force || *existing != active_))
        {
            const auto switched = activate(*existing, force);
            if (switched.refused)
                return switched;
            host_.songUsed(at(active_).file() ? *at(active_).file() : file, std::nullopt);
            return {true, "Opened " + named(file), false};
        }
        auto song = std::make_unique<core::Document>();
        auto result = openFile(*song, file);
        if (!result.opened)
        {
            return CommandResult::error(std::move(result.message));
        }
        const bool empty = songs_.size() == 1 && !document_.file() && !document_.importedFrom() &&
                           !document_.modified() && !document_.canUndo() && !document_.canRedo() &&
                           document_.underlying() == ui::newSong();
        if (const auto switched = activateNew(std::move(song), force || empty); switched.refused)
            return switched;
        host_.songUsed(file, std::nullopt);
        if (!result.message.empty())
        {
            return {true, std::move(result.message), true}; // opened, with a warning
        }
        return {true, "Opened " + named(file), false};
    }

    CommandResult saved(const std::filesystem::path& file, bool thenQuit)
    {
        if (auto refusal = recording_.fileOperationRefusal())
        {
            return CommandResult::error(std::move(*refusal));
        }
        if (auto wrong = wrongKind(file, true)) // from a dialog too
        {
            return *wrong;
        }
        if (const auto existing = openedFile(file, false); existing && *existing != active_)
            return CommandResult::error(named(file) + " is already open in another song");
        if (auto error = core::writeSongFile(document_.underlying(), file); !error.empty())
        {
            return CommandResult::error("Can't save " + named(file) + ": " + error);
        }
        // A MIDI file imported and now saved as a song gives the song its place in the list.
        std::optional<std::filesystem::path> imported;
        if (!document_.file())
            imported = document_.importedFrom();
        document_.markSaved(file);
        host_.songUsed(file, imported);
        if (thenQuit)
        {
            if (otherModified())
                return CommandResult::error(
                    "Saved " + named(file) +
                    ". Other open songs have unsaved changes. Use :song-select");
            host_.quit();
        }
        return {true, "Saved " + named(file), false};
    }

    CommandResult exported(const std::filesystem::path& file)
    {
        if (auto refusal = recording_.fileOperationRefusal())
        {
            return CommandResult::error(std::move(*refusal));
        }
        if (auto wrong = wrongKind(file, false))
        {
            return *wrong;
        }
        const auto result = core::exportSmfFile(document_.underlying(), file);
        if (!result.error.empty())
        {
            return CommandResult::error("Can't export " + named(file) + ": " + result.error);
        }
        return {false, "Exported " + named(file), false};
    }

    core::RecordingSession& recording_;
    core::Document& document_;
    FileHost& host_;
    std::optional<std::filesystem::path> home_;
    // A null document marks the active slot, which uses the workspace's stable document.
    std::vector<SongSlot> songs_;
    std::size_t active_ = 0;
    std::size_t nextNumber_ = 2;
    std::uint64_t activation_ = 0;
};

} // namespace

std::span<const CommandInfo> fileCommands()
{
    return kFileCommands;
}

std::function<bool()> addFileCommands(CommandRegistry& registry, core::RecordingSession& recording,
                                      FileHost& host, std::optional<std::filesystem::path> home)
{
    // Kept as long as the registry runs them.
    auto commands = std::make_shared<FileCommands>(recording, host, std::move(home));
    // Check before opening a dialog too; the callbacks above check again, as recording can
    // start while a dialog is waiting for its answer.
    const auto add = [&](const CommandInfo& info, auto run)
    {
        registry.add(info,
                     [&recording, run](const CommandCall& call)
                     {
                         if (auto refusal = recording.fileOperationRefusal())
                         {
                             return CommandResult::error(std::move(*refusal));
                         }
                         return run(call);
                     });
    };
    add(kFileCommands[0], [commands](const CommandCall& call) { return commands->edit(call); });
    add(kFileCommands[1],
        [commands](const CommandCall& call) { return commands->write(call, false); });
    add(kFileCommands[2],
        [commands](const CommandCall& call) { return commands->exportMidi(call); });
    add(kFileCommands[3], [commands](const CommandCall& call) { return commands->newSong(call); });
    add(kFileCommands[4], [commands](const CommandCall& call) { return commands->quit(call); });
    add(kFileCommands[5],
        [commands](const CommandCall& call) { return commands->write(call, true); });
    add(kFileCommands[6], [commands](const CommandCall&) { return commands->selectSong(); });
    add(kFileCommands[7],
        [commands](const CommandCall& call) { return commands->closeSong(call); });
    add(kFileCommands[8],
        [commands](const CommandCall& call) { return commands->write(call, false, true); });
    add(kFileCommands[9],
        [commands](const CommandCall& call) { return commands->stepSong(call.count, true); });
    add(kFileCommands[10],
        [commands](const CommandCall& call) { return commands->stepSong(call.count, false); });
    return [commands] { return commands->otherModified(); };
}

std::filesystem::path songFolder(const core::Document& document,
                                 const std::optional<std::filesystem::path>& home,
                                 const std::optional<std::filesystem::path>& songs)
{
    // The song's own folder first, its context outweighing the usual one (decided 20 November
    // 2026), then the Songs folder, then home.
    if (const auto& file = document.file() ? document.file() : document.importedFrom())
    {
        return file->parent_path();
    }
    if (songs)
    {
        return *songs;
    }
    if (home)
    {
        return *home;
    }
    std::error_code error;
    return std::filesystem::current_path(error);
}

OpenResult openFile(core::Document& document, const std::filesystem::path& file)
{
    if (namesMidi(file))
    {
        auto result = core::importSmfFile(file);
        if (!result.song)
        {
            return {false, "Can't import " + named(file) + ": " + result.error};
        }
        document.replace(std::move(*result.song), std::nullopt, file);
        if (result.warnings.empty())
        {
            return {true, {}};
        }
        return {true, named(file) + ": " + result.warnings.front() +
                          (result.warnings.size() > 1
                               ? " (and " + std::to_string(result.warnings.size() - 1) + " more)"
                               : std::string{})};
    }
    auto result = core::readSongFile(file);
    if (!result.song)
    {
        return {false, "Can't open " + result.error};
    }
    document.replace(std::move(*result.song), file);
    return {true, {}};
}

core::Song newSong()
{
    core::Song song;
    song.addTrack("Track 1");
    return song;
}

namespace
{
constexpr std::string_view kRecoveryNote = "recovered";
} // namespace

std::optional<std::filesystem::path> writeRecovery(const core::Document& document,
                                                   const std::filesystem::path& stateDirectory,
                                                   std::string_view stamp) noexcept
{
    try
    {
        if (!document.modified())
        {
            return std::nullopt;
        }
        std::error_code ignored;
        std::filesystem::create_directories(stateDirectory, ignored);
        const auto file = stateDirectory / utf8Path("recovered-" + std::string{stamp} + ".modal");
        if (!core::writeSongFile(document.underlying(), file).empty())
        {
            return std::nullopt;
        }
        const auto name = utf8Text(file);
        static_cast<void>(core::writeFileAtomically(
            stateDirectory / kRecoveryNote,
            {reinterpret_cast<const std::uint8_t*>(name.data()), name.size()}));
        return file;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::string> takeRecoveryNotice(const std::filesystem::path& stateDirectory)
{
    const auto note = stateDirectory / kRecoveryNote;
    std::string name;
    {
        std::ifstream in{note, std::ios::binary};
        if (!in)
        {
            return std::nullopt;
        }
        name.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    std::error_code ignored;
    std::filesystem::remove(note, ignored);
    if (name.empty())
    {
        return std::nullopt;
    }
    return "Modal ended unexpectedly last time: the song's unsaved changes are in " + name +
           " (:e opens it)";
}

} // namespace modal::ui
