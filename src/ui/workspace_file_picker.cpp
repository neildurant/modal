// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/drum_maps.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace modal::ui
{

namespace
{

std::string_view filenameText(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    return first == text.npos ? std::string_view{}
                              : text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::pair<std::string_view, std::string_view> splitFilename(std::string_view text)
{
    text = filenameText(text);
    if (text.starts_with('"'))
    {
        text.remove_prefix(1);
        if (text.ends_with('"'))
            text.remove_suffix(1);
    }
    const auto slash = std::ranges::find_if(text.rbegin(), text.rend(), pathSeparator);
    const auto split = static_cast<std::size_t>(text.rend() - slash);
    return {text.substr(0, split), text.substr(split)};
}

bool shownFile(Workspace::PickerKind kind, bool save, const std::filesystem::path& path)
{
    if (kind == Workspace::PickerKind::Config)
        return true;
    const auto extension = utf8Text(path.extension());
    if (kind == Workspace::PickerKind::DrumMap)
        return sameDrumMapName(extension, ".drum-map");
    return sameDrumMapName(extension, ".modal") ||
           (!save && (sameDrumMapName(extension, ".mid") || sameDrumMapName(extension, ".midi")));
}

std::u32string fitted(std::string_view text, std::size_t cells, bool keepEnd = false)
{
    auto wide = decodeUtf8(text);
    if (wide.size() <= cells)
        return wide;
    if (keepEnd)
        return U"…" + wide.substr(wide.size() - cells + 1);
    wide.resize(cells - 1);
    wide.push_back(U'…');
    return wide;
}

} // namespace

void Workspace::pickFile(PickerKind kind, std::optional<std::filesystem::path> saveAs,
                         PickedFile done, bool confirmOverwrite)
{
    filePicker_ = FilePicker{};
    auto& picker = *filePicker_;
    picker.kind = kind;
    picker.version = document_.version();
    picker.done = std::move(done);
    picker.save = saveAs.has_value();
    picker.confirmOverwrite = confirmOverwrite;
    const auto songs = songFolder(document_, home_);
    const auto maps = drumMapFolder();
    picker.places.emplace_back("Song", songs);
    if (configFolder_)
        picker.places.emplace_back("Config", *configFolder_);
    picker.places.emplace_back("Drum maps", maps);
    if (stateFolder_)
        picker.places.emplace_back("State", *stateFolder_);
    auto folder = kind == PickerKind::DrumMap  ? maps
                  : kind == PickerKind::Config ? configFolder_.value_or(songs)
                                               : songs;
    if (saveAs)
    {
        picker.suggested = utf8Text(saveAs->filename());
        if (saveAs->has_parent_path())
            folder = saveAs->parent_path();
    }
    prompt_.answer = {};
    prompt_.palette.reset();
    mode_ = Mode::CommandLine;
    enterPickerFolder(std::move(folder));
}

void Workspace::enterPickerFolder(std::filesystem::path folder)
{
    auto& picker = *filePicker_;
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(folder, error);
    picker.folder = error || resolved.empty() ? std::move(folder) : std::move(resolved);
    picker.listingFolder.clear();
    picker.selected.reset();
    picker.choosingPlace = false;
    prompt_.line.clear();
    prompt_.completing.reset();
    listPickerFolder();
}

void Workspace::listPickerFolder()
{
    auto& picker = *filePicker_;
    const auto input = encodeUtf8(prompt_.line.text());
    const auto prefix = splitFilename(input).first;
    auto listing = prefix.empty() ? picker.folder : typedPath(prefix, picker.folder, home_);
    if (listing == picker.listingFolder)
        return;
    picker.listingFolder = std::move(listing);
    picker.entries.clear();
    picker.error.clear();

    std::error_code error;
    std::vector<std::string> folders;
    std::vector<std::string> files;
    auto entry = std::filesystem::directory_iterator{picker.listingFolder, error};
    for (; !error && entry != std::filesystem::directory_iterator{}; entry.increment(error))
    {
        const auto& item = *entry;
        std::error_code typeError;
        auto name = utf8Text(item.path().filename());
        if (item.is_directory(typeError))
            folders.push_back(std::move(name) + "/");
        else if (!typeError && item.is_regular_file(typeError) &&
                 shownFile(picker.kind, picker.save, item.path()))
            files.push_back(std::move(name));
    }
    if (error &&
        !(picker.kind == PickerKind::Config && error == std::errc::no_such_file_or_directory))
        picker.error = "Can't list this directory: " + error.message();
    std::ranges::sort(folders);
    std::ranges::sort(files);
    // A trailing slash would make parent_path return the same folder without the slash.
    const auto directory =
        picker.listingFolder.filename().empty() && picker.listingFolder.has_relative_path()
            ? picker.listingFolder.parent_path()
            : picker.listingFolder;
    if (directory.has_parent_path() && directory.parent_path() != directory)
        picker.entries.emplace_back("../");
    picker.entries.insert(picker.entries.end(), folders.begin(), folders.end());
    picker.entries.insert(picker.entries.end(), files.begin(), files.end());
}

std::vector<std::string> Workspace::filePickerMatches() const
{
    const auto& picker = *filePicker_;
    const auto text = encodeUtf8(prompt_.line.text());
    const auto name = splitFilename(text).second;
    auto rows = matching(picker.entries, name);
    if (!name.starts_with('.'))
        std::erase_if(rows, [](const auto& row) { return row.starts_with('.') && row != "../"; });
    return rows;
}

std::vector<std::string> Workspace::filePickerChoices(std::string_view word) const
{
    const auto& picker = *filePicker_;
    auto paths = fileChoices(picker.folder, word, home_);
    std::erase_if(paths,
                  [&](const auto& path)
                  {
                      return !pathSeparator(path.back()) &&
                             !shownFile(picker.kind, picker.save,
                                        typedPath(path, picker.folder, home_));
                  });
    return paths;
}

void Workspace::filePickerEdited()
{
    filePicker_->selected.reset();
    prompt_.completing.reset();
    listPickerFolder();
}

std::optional<bool> Workspace::runFilePicker(std::string_view command)
{
    auto& picker = *filePicker_;
    if (command == "command-line-cancel")
    {
        if (picker.choosingPlace)
            picker.choosingPlace = false;
        else
            finishFilePicker(std::nullopt);
        return true;
    }
    if (command == "command-line-list")
    {
        picker.choosingPlace = !picker.choosingPlace;
        return true;
    }
    if (command == "command-line-run")
    {
        if (picker.choosingPlace)
            enterPickerFolder(picker.places[picker.place].second);
        else
            submitFilePicker();
        return true;
    }
    if (command == "command-line-older" || command == "command-line-newer" ||
        command == "command-line-page-up" || command == "command-line-page-down")
    {
        const bool forward = command == "command-line-newer" || command == "command-line-page-down";
        const auto step = command.starts_with("command-line-page-") ? filePickerRows() : 1;
        const auto moved = [forward, step](std::size_t from, std::size_t count)
        {
            return forward ? std::min(from + static_cast<std::size_t>(step), count - 1)
                           : from - std::min(from, static_cast<std::size_t>(step));
        };
        if (picker.choosingPlace)
            picker.place = moved(picker.place, picker.places.size());
        else if (const auto rows = filePickerMatches(); !rows.empty())
            picker.selected = picker.selected ? moved(*picker.selected, rows.size())
                              : forward       ? 0
                                              : rows.size() - 1;
        return true;
    }
    if (picker.choosingPlace)
        return false;
    if (command == "command-line-next" || command == "command-line-previous")
    {
        picker.selected.reset();
        const auto changed = complete(command == "command-line-next" ? 1 : -1);
        listPickerFolder();
        return changed;
    }
    const auto before = prompt_.line.text();
    const auto edited = editLine(prompt_.line, command);
    if (before != prompt_.line.text())
        filePickerEdited();
    return edited;
}

void Workspace::submitFilePicker()
{
    const auto& picker = *filePicker_;
    const auto input = encodeUtf8(prompt_.line.text());
    auto name = std::string{filenameText(input)};
    const auto rows = filePickerMatches();
    bool selected = false;
    if (picker.selected && *picker.selected < rows.size())
    {
        name = rows[*picker.selected];
        selected = true;
    }
    if (name.empty() && picker.save)
        name = picker.suggested;
    if (name.empty())
        return;

    // A listed filename is already literal, including any spaces or quotes in it.
    auto path =
        selected ? picker.listingFolder / utf8Path(name) : typedPath(name, picker.folder, home_);
    std::error_code error;
    const auto isFolder = [&](const auto& candidate)
    { return std::filesystem::is_directory(candidate, error); };
    if (isFolder(path))
    {
        enterPickerFolder(std::move(path));
        return;
    }
    if (path.filename().empty() || (name.size() > 0 && pathSeparator(name.back())))
    {
        refuse("Can't open folder " + utf8Text(path), MessageKind::Warning);
        return;
    }
    if (picker.kind != PickerKind::Config && !path.has_extension())
        path += picker.kind == PickerKind::Song ? ".modal" : ".drum-map";
    error.clear();
    if (!picker.save)
    {
        if (!std::filesystem::is_regular_file(path, error))
        {
            if (!selected && rows.size() == 1)
            {
                filePicker_->selected = 0;
                submitFilePicker();
                return;
            }
            refuse("Choose an existing file: " + utf8Text(path.filename()), MessageKind::Warning);
            return;
        }
        finishFilePicker(std::move(path));
        return;
    }
    const auto parent = path.parent_path();
    // The config writer creates the directory when saving a first configuration.
    if (picker.kind != PickerKind::Config && !parent.empty() && !isFolder(parent))
    {
        refuse("The destination directory doesn't exist: " + utf8Text(parent),
               MessageKind::Warning);
        return;
    }
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        refuse("Can't check the destination: " + error.message(), MessageKind::Warning);
        return;
    }
    if (std::filesystem::exists(status))
    {
        if (!std::filesystem::is_regular_file(path, error))
        {
            refuse("The destination is not a regular file: " + utf8Text(path),
                   MessageKind::Warning);
            return;
        }
        if (picker.confirmOverwrite)
        {
            mode_ = Mode::Normal; // put the confirmation on the message row
            const auto resume = [this] { mode_ = Mode::CommandLine; };
            ask(
                "Replace " + utf8Text(path) + "?", [this, path] { finishFilePicker(path); }, resume,
                resume);
            return;
        }
    }
    finishFilePicker(std::move(path));
}

void Workspace::finishFilePicker(std::optional<std::filesystem::path> path)
{
    auto picker = std::move(*filePicker_);
    filePicker_.reset();
    mode_ = Mode::Normal;
    prompt_.line.clear();
    prompt_.completing.reset();
    if (path && picker.kind != PickerKind::Config && picker.version != document_.version())
    {
        refuse("The song changed while choosing a file; open the picker again",
               MessageKind::Warning);
        return;
    }
    if (picker.done)
        picker.done(std::move(path));
}

int Workspace::filePickerRows() const
{
    return std::clamp(messageRow().y / std::max(cell_.height, 1) - 3, 1, 8);
}

Region Workspace::filePickerRegion() const
{
    const auto& picker = *filePicker_;
    const auto rows = filePickerMatches();
    const auto total = picker.choosingPlace ? picker.places.size() : rows.size();
    const auto selected = picker.choosingPlace ? std::optional{picker.place} : picker.selected;
    const auto visible = static_cast<std::size_t>(filePickerRows());
    const auto first = selected && *selected >= visible ? *selected - visible + 1 : 0;
    const auto shown = std::max(std::min(total - std::min(first, total), visible), std::size_t{1});
    const int h = cell_.height;
    const auto base = panelBase();
    const int height = (static_cast<int>(shown) + 3) * h;
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto width =
        static_cast<std::size_t>(std::max((base.width - 2 * margin) / cell_.width, 1));
    Region region{.bounds = {base.x, base.y - height, base.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style label{Role::Accent, Role::Panel, kBold};
    const Style text{Role::Text, Role::Panel, kPlain};
    const Style dim{Role::Dim, Role::Panel, kPlain};
    const Style cursor{Role::CursorText, Role::Cursor, kPlain};
    const auto draw =
        [&](std::size_t row, std::string_view value, Style style, bool keepEnd = false)
    {
        region.text.push_back(
            {margin, static_cast<int>(row) * h, fitted(value, width, keepEnd), style});
    };
    const auto title = picker.choosingPlace                ? "Places"
                       : picker.kind == PickerKind::Config ? "Write configuration"
                       : picker.kind == PickerKind::Song   ? picker.save ? "Save song" : "Open song"
                       : picker.save                       ? "Save drum map"
                                                           : "Load drum map";
    draw(0, title, label);
    draw(1, utf8Text(picker.listingFolder), text, true);
    if (total == 0)
        draw(2, "No matching files or folders", dim);
    else
    {
        for (std::size_t i = 0; i < shown; ++i)
        {
            const auto at = first + i;
            const auto row = i + 2;
            if (selected == at)
                region.under.push_back(
                    {{0, static_cast<int>(row) * h, base.width, h}, Role::Cursor, 255});
            const auto line = picker.choosingPlace ? padText(picker.places[at].first, 9, false) +
                                                         "  " + utf8Text(picker.places[at].second)
                                                   : rows[at];
            draw(row, line, selected == at ? cursor : text);
        }
    }
    const std::array hints{"Alt-Down Places  ·  Tab completes  ·  Up/Down choose  ·  Enter opens",
                           "Up/Down choose  ·  Enter goes there  ·  Esc goes back"};
    draw(shown + 2,
         !message_.empty()       ? message_
         : !picker.error.empty() ? picker.error
                                 : hints[picker.choosingPlace],
         message_.empty() && picker.error.empty() ? text
                                                  : Style{Role::Warning, Role::Panel, kPlain});
    return region;
}

void Workspace::chooseSong(std::vector<OpenSong> songs, std::size_t active,
                           std::function<void(std::size_t)> chosen)
{
    std::vector<std::string> labels;
    std::vector<SongChoice::Row> rows;
    for (std::size_t i = 0; i < songs.size(); ++i)
    {
        const auto& song = songs[i];
        const auto location = song.location + (song.imported ? " (MIDI import)" : "");
        labels.push_back(song.name + " " + location);
        rows.push_back({decodeUtf8((i == active ? "› " : "  ") +
                                   std::string{song.modified ? "+ " : "  "} + song.name),
                        decodeUtf8(location)});
    }
    songChoice_.emplace(SongChoice{ChoiceList{std::move(labels), active, true}, std::move(rows),
                                   std::move(chosen)});
    songChoice_->list.name("song");
}

bool Workspace::songChoiceKey(const Key& key)
{
    message_.clear();
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
        songChoice_.reset();
    else if (command == "command-line-run")
    {
        if (const auto index = songChoice_->list.chosen())
        {
            auto chosen = std::move(songChoice_->chosen);
            songChoice_.reset();
            chosen(*index);
        }
    }
    else if (!songChoice_->list.run(command) && key.named == NamedKey::None &&
             (key.modifiers & (kCtrl | kAlt)) == 0)
        songChoice_->list.type(key.character);
    return true;
}

Region Workspace::songChoiceRegion() const
{
    const auto& choice = *songChoice_;
    const auto bar = messageRow();
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto columns =
        static_cast<std::size_t>(std::max((bar.width - 2 * margin) / cell_.width, 1));
    std::size_t nameWidth = 0;
    for (const auto& row : choice.rows)
        nameWidth = std::max(nameWidth, row.name.size());
    // Leave most of the row for paths, even when a song title is long.
    nameWidth = std::min(nameWidth, std::max(columns / 3, std::size_t{1}));
    std::vector<std::u32string> labels;
    for (const auto& row : choice.rows)
    {
        auto name = row.name;
        name.resize(nameWidth, U' ');
        if (row.name.size() > nameWidth)
            name.back() = U'…';
        labels.push_back(std::move(name) + U"  " + row.location);
    }
    std::u32string detail;
    if (const auto chosen = choice.list.chosen(); chosen && labels[*chosen].size() + 1 > columns)
        detail = choice.rows[*chosen].location;
    const auto maxRows = static_cast<std::size_t>(std::max(bar.y / cell_.height - 8, 1));
    const auto rows = std::min((detail.size() + columns - 1) / columns, maxRows);
    if (detail.size() > rows * columns)
    {
        detail.resize(rows * columns);
        detail.back() = U'…';
    }
    const int detailHeight = static_cast<int>(rows) * cell_.height;
    const Rect field{0, bar.y - detailHeight, bar.width, cell_.height};
    auto region = choice.list.region(field, {0, 0, bar.width, field.y}, cell_, labels);
    for (std::size_t i = 0; i < rows; ++i)
        region.text.push_back({margin,
                               region.bounds.height + static_cast<int>(i) * cell_.height,
                               detail.substr(i * columns, columns),
                               {Role::Accent, Role::Panel, kPlain}});
    region.bounds.height += detailHeight;
    return region;
}

} // namespace modal::ui
