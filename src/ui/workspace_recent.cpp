// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/recent_songs.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace modal::ui
{

void Workspace::songUsed(const std::filesystem::path& file,
                         const std::optional<std::filesystem::path>& replacing)
{
    const auto most = settings_ != nullptr ? settings_->integer("recentsongs") : 12;
    if (most <= 0)
        return;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(file, error);
    if (replacing)
        remembered_.forgetSong(utf8Text(std::filesystem::absolute(*replacing, error)));
    remembered_.usedSong(utf8Text((error ? file : absolute).lexically_normal()), clock_(),
                         static_cast<std::size_t>(most));
    rememberedChanged();
}

CommandResult Workspace::showRecentSongs()
{
    if (remembered_.songs().empty())
        return {false, "No recent songs yet: songs opened or saved are listed here", false};
    recentChoice_.emplace();
    listRecentSongs(0);
    return {true};
}

void Workspace::listRecentSongs(std::size_t highlighted)
{
    auto& choice = *recentChoice_;
    const auto typed = choice.list ? choice.list->typed() : std::u32string{};
    choice.paths.clear();
    choice.missing.clear();
    choice.used.clear();
    std::vector<std::string> names;
    for (const auto& song : remembered_.songs())
    {
        const std::filesystem::path path = utf8Path(song.path);
        std::error_code error;
        choice.paths.push_back(path);
        choice.missing.push_back(!std::filesystem::is_regular_file(path, error));
        choice.used.push_back(song.used);
        names.push_back(utf8Text(path.filename()) + " " + utf8Text(path.parent_path()));
    }
    choice.list.emplace(std::move(names), std::min(highlighted, choice.paths.size() - 1), true);
    choice.list->name("recent song");
    for (const auto c : typed)
        choice.list->type(c);
}

bool Workspace::recentKey(const Key& key)
{
    message_.clear();
    auto& choice = *recentChoice_;
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
    {
        recentChoice_.reset();
        return true;
    }
    const auto index = choice.list->chosen();
    if (command == "command-line-delete")
    {
        // Off the list, never the file (decided 20 November 2026).
        if (index)
        {
            const auto name = utf8Text(choice.paths[*index].filename());
            remembered_.forgetSong(utf8Text(choice.paths[*index]));
            rememberedChanged();
            if (remembered_.songs().empty())
                recentChoice_.reset();
            else
                listRecentSongs(*index);
            setMessage("Removed " + name + " from the recent list; the file is untouched",
                       MessageKind::Information);
        }
        return true;
    }
    if (command == "command-line-run")
    {
        if (!index)
        {
            refuse(choice.list->nothing(), MessageKind::Information);
            return true;
        }
        const auto path = choice.paths[*index];
        const bool missing = choice.missing[*index];
        recentChoice_.reset();
        if (missing)
        {
            ask(utf8Text(path.filename()) + " no longer exists. Remove it from the list?",
                [this, path]
                {
                    remembered_.forgetSong(utf8Text(path));
                    rememberedChanged();
                    setMessage("Removed " + utf8Text(path.filename()) + " from the recent list",
                               MessageKind::Information);
                });
            return true;
        }
        if (lineHandler_)
            lineHandler_("e " + utf8Text(path));
        return true;
    }
    if (!choice.list->run(command) && key.named == NamedKey::None &&
        (key.modifiers & (kCtrl | kAlt)) == 0)
        choice.list->type(key.character);
    return true;
}

Region Workspace::recentRegion() const
{
    const auto& choice = *recentChoice_;
    const auto bar = messageRow();
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto columns =
        static_cast<std::size_t>(std::max((bar.width - 2 * margin) / cell_.width, 1));
    // Three columns: the file's name, its folder, and when it was used, at the right.
    std::vector<std::u32string> names;
    std::vector<std::u32string> folders;
    std::vector<std::u32string> times;
    std::size_t nameWidth = 0;
    std::size_t timeWidth = 0;
    const auto now = clock_();
    for (std::size_t i = 0; i < choice.paths.size(); ++i)
    {
        names.push_back(decodeUtf8(utf8Text(choice.paths[i].filename())));
        folders.push_back(decodeUtf8(utf8Text(choice.paths[i].parent_path())));
        times.push_back(decodeUtf8(choice.missing[i] ? std::string{"not found"}
                                                     : usedText(choice.used[i], now)));
        nameWidth = std::max(nameWidth, names.back().size());
        timeWidth = std::max(timeWidth, times.back().size());
    }
    nameWidth = std::min(nameWidth, std::max(columns / 3, std::size_t{1}));
    const auto folderWidth =
        columns > nameWidth + timeWidth + 6 ? columns - nameWidth - timeWidth - 6 : std::size_t{1};
    const auto fit = [](std::u32string text, std::size_t width, bool keepEnd)
    {
        if (text.size() > width)
        {
            text = keepEnd ? U"…" + text.substr(text.size() - width + 1) : text.substr(0, width);
            if (!keepEnd)
                text.back() = U'…';
        }
        text.resize(width, U' ');
        return text;
    };
    std::vector<std::u32string> labels;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto time = times[i];
        time.insert(0, timeWidth - time.size(), U' ');
        labels.push_back(fit(names[i], nameWidth, false) + U"  " +
                         fit(folders[i], folderWidth, true) + U"  " + time);
    }
    const Rect field{0, bar.y, bar.width, cell_.height};
    return choice.list->region(field, {0, 0, bar.width, field.y}, cell_, labels);
}

} // namespace modal::ui
