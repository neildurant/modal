// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/backups.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/recent_songs.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

namespace modal::ui
{

namespace
{

// Keyed by the full path, as the recent list is, so a song reached by another route shares
// its backups.
std::filesystem::path fullPath(const std::filesystem::path& file)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(file, error);
    return (error ? file : absolute).lexically_normal();
}

} // namespace

std::string Workspace::backUp(const std::filesystem::path& file) const
{
    const auto most = settings_ != nullptr ? settings_->integer("backups") : 5;
    if (most <= 0 || !stateFolder_)
        return {};
    return backUpSong(*stateFolder_, fullPath(file), static_cast<std::size_t>(most));
}

CommandResult Workspace::showBackups()
{
    const auto& file = document_.file();
    if (!file)
        return {false,
                "This song has no file yet, so no backups: each save keeps the version "
                "it replaces",
                false};
    const auto song = fullPath(*file);
    const auto backups =
        stateFolder_ ? songBackups(*stateFolder_, song) : std::vector<SongBackup>{};
    if (backups.empty())
        return {false,
                "No backups of " + utf8Text(song.filename()) +
                    " yet: each save keeps the version it replaces",
                false};
    auto& choice = backupChoice_.emplace();
    choice.song = song;
    std::vector<std::string> names;
    for (const auto& backup : backups)
    {
        choice.files.push_back(backup.file);
        choice.numbers.push_back(backup.number);
        choice.saved.push_back(secondsSince1970(backup.saved));
        names.push_back("Backup " + std::to_string(backup.number));
    }
    choice.list.emplace(std::move(names), 0, false);
    choice.list->name("backup");
    return {true};
}

bool Workspace::backupKey(const Key& key)
{
    message_.clear();
    auto& choice = *backupChoice_;
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
    {
        backupChoice_.reset();
        return true;
    }
    if (command == "command-line-run")
    {
        const auto index = choice.list->chosen();
        if (!index)
            return true;
        const auto file = choice.files[*index];
        const auto number = choice.numbers[*index];
        const auto saved = usedText(choice.saved[*index], clock_());
        const auto name = utf8Text(choice.song.filename());
        backupChoice_.reset();
        if (!lineHandler_)
            return true;
        refused_ = false;
        lineHandler_("song-open-copy " + utf8Text(file));
        if (!refused_)
            setMessage("Opened backup " + std::to_string(number) + " of " + name + ", saved " +
                           saved + ", as a new song: saving asks where",
                       MessageKind::Information);
        return true;
    }
    choice.list->run(command);
    return true;
}

Region Workspace::backupRegion() const
{
    const auto& choice = *backupChoice_;
    const auto bar = messageRow();
    const auto now = clock_();
    std::vector<std::u32string> labels;
    for (std::size_t i = 0; i < choice.files.size(); ++i)
    {
        auto label = decodeUtf8("Backup " + std::to_string(choice.numbers[i]));
        label.resize(std::max<std::size_t>(label.size(), 10), U' ');
        labels.push_back(label + U"  saved " + decodeUtf8(usedText(choice.saved[i], now)) +
                         (i == 0 ? U"  (newest)" : U""));
    }
    const Rect field{0, bar.y, bar.width, cell_.height};
    return choice.list->region(field, {0, 0, bar.width, field.y}, cell_, labels);
}

} // namespace modal::ui
