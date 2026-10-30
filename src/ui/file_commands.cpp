// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 6> kFileCommands{{
    {.name = "edit",
     .description = "Open a song, or import a MIDI file; with no file, choose one",
     .aliases = "e",
     .usage = "[FILE]",
     .bang = true,
     .keys = false,
     .choices = {Choices::File}},
    {.name = "write",
     .description = "Save the song, to its file or to FILE",
     .aliases = "w",
     .usage = "[FILE]",
     .bang = true,
     .keys = false,
     .choices = {Choices::File}},
    {.name = "export",
     .description = "Export the song as a Standard MIDI File",
     .usage = "[FILE]",
     .bang = true,
     .keys = false,
     .choices = {Choices::File}},
    {.name = "new", .description = "Start a new song", .bang = true, .keys = false},
    {.name = "quit", .description = "Quit", .aliases = "q", .bang = true},
    {.name = "wq", .description = "Save the song, then quit", .keys = false},
}};

// The argument as a path: the rest of the line, without quotes around it, given an
// extension if it has none.
std::optional<std::filesystem::path> fileArgument(std::string_view text, std::string_view extension)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return std::nullopt;
    }
    text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    {
        text = text.substr(1, text.size() - 2);
    }
    auto path = utf8Path(text);
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

class FileCommands
{
public:
    FileCommands(core::Document& document, FileHost& host)
        : document_(document)
        , host_(host)
    {
    }

    CommandResult edit(const CommandCall& call)
    {
        if (document_.modified() && !call.bang)
        {
            return unsaved("e");
        }
        if (const auto file = fileArgument(call.arguments, ".modal"))
        {
            return opened(*file);
        }
        host_.chooseOpen(
            [this](std::optional<std::filesystem::path> file)
            {
                if (file)
                {
                    host_.report(opened(*file));
                }
            });
        return {};
    }

    CommandResult write(const CommandCall& call, bool thenQuit)
    {
        if (const auto file = fileArgument(call.arguments, ".modal"))
        {
            if (!call.bang && overwrites(*file, document_.file()))
            {
                return CommandResult::error(named(*file) + " exists: :w! " +
                                            std::string{call.arguments} + " writes over it");
            }
            return saved(*file, thenQuit);
        }
        if (document_.file())
        {
            return saved(*document_.file(), thenQuit);
        }
        const auto& imported = document_.importedFrom();
        auto suggested = imported ? std::filesystem::path{*imported}.replace_extension(".modal")
                                  : std::filesystem::path{"Untitled.modal"};
        host_.chooseSave(FileHost::FileKind::Song, suggested,
                         [this, thenQuit](std::optional<std::filesystem::path> file)
                         {
                             if (file)
                             {
                                 host_.report(saved(*file, thenQuit));
                             }
                         });
        return {};
    }

    CommandResult exportMidi(const CommandCall& call)
    {
        if (const auto file = fileArgument(call.arguments, ".mid"))
        {
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
                         [this](std::optional<std::filesystem::path> chosen)
                         {
                             if (chosen)
                             {
                                 host_.report(exported(*chosen));
                             }
                         });
        return {};
    }

    CommandResult newSong(const CommandCall& call)
    {
        if (document_.modified() && !call.bang)
        {
            return unsaved("new");
        }
        document_.replace(ui::newSong(), std::nullopt);
        host_.songReplaced();
        return {true};
    }

    CommandResult quit(const CommandCall& call)
    {
        if (document_.modified() && !call.bang)
        {
            return unsaved("q");
        }
        host_.quit();
        return {true};
    }

private:
    CommandResult opened(const std::filesystem::path& file)
    {
        auto result = openFile(document_, file);
        if (!result.opened)
        {
            return CommandResult::error(std::move(result.message));
        }
        host_.songReplaced();
        if (!result.message.empty())
        {
            return CommandResult::error(std::move(result.message)); // opened, with a warning
        }
        return {true, "Opened " + named(file), false};
    }

    CommandResult saved(const std::filesystem::path& file, bool thenQuit)
    {
        if (auto error = core::writeSongFile(document_.song(), file); !error.empty())
        {
            return CommandResult::error("Can't save " + named(file) + ": " + error);
        }
        document_.markSaved(file);
        if (thenQuit)
        {
            host_.quit();
        }
        return {true, "Saved " + named(file), false};
    }

    CommandResult exported(const std::filesystem::path& file)
    {
        const auto result = core::exportSmfFile(document_.song(), file);
        if (!result.error.empty())
        {
            return CommandResult::error("Can't export " + named(file) + ": " + result.error);
        }
        return {false, "Exported " + named(file), false};
    }

    core::Document& document_;
    FileHost& host_;
};

} // namespace

std::span<const CommandInfo> fileCommands()
{
    return kFileCommands;
}

void addFileCommands(CommandRegistry& registry, core::Document& document, FileHost& host)
{
    // Kept as long as the registry runs them.
    auto commands = std::make_shared<FileCommands>(document, host);
    registry.add(kFileCommands[0],
                 [commands](const CommandCall& call) { return commands->edit(call); });
    registry.add(kFileCommands[1],
                 [commands](const CommandCall& call) { return commands->write(call, false); });
    registry.add(kFileCommands[2],
                 [commands](const CommandCall& call) { return commands->exportMidi(call); });
    registry.add(kFileCommands[3],
                 [commands](const CommandCall& call) { return commands->newSong(call); });
    registry.add(kFileCommands[4],
                 [commands](const CommandCall& call) { return commands->quit(call); });
    registry.add(kFileCommands[5],
                 [commands](const CommandCall& call) { return commands->write(call, true); });
}

OpenResult openFile(core::Document& document, const std::filesystem::path& file)
{
    auto extension = utf8Text(file.extension());
    std::ranges::transform(extension, extension.begin(), [](char c)
                           { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (extension == ".mid" || extension == ".midi")
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

} // namespace modal::ui
