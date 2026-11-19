// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/drum_map_file.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace modal::ui
{
namespace
{
std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    return first == std::string_view::npos
               ? std::string_view{}
               : text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::filesystem::path suggestedDrumMap(const std::filesystem::path& folder, std::string name)
{
    // A display name need not be a usable filename on every platform.
    for (auto& c : name)
        if (static_cast<unsigned char>(c) < 32 ||
            std::string_view{"<>:\"/\\|?*"}.find(c) != std::string_view::npos)
            c = '_';
    if (name.empty())
        name = "Kit";
    return folder / utf8Path(name + ".drum-map");
}
std::vector<std::array<std::u32string, 3>>
drumMapReviewLines(const std::vector<DrumMapDifference>& differences, int valueColumns)
{
    std::vector<std::array<std::u32string, 3>> lines;
    for (const auto& change : differences)
    {
        const auto pitch =
            change.pitch ? std::to_string(*change.pitch) + " " + core::pitchName(*change.pitch)
                         : "Map name";
        const auto song = decodeUtf8(change.song.empty() ? "—" : change.song);
        const auto saved = decodeUtf8(change.saved.empty() ? "—" : change.saved);
        const auto chunks =
            (std::max(song.size(), saved.size()) + static_cast<std::size_t>(valueColumns) - 1) /
            static_cast<std::size_t>(valueColumns);
        for (std::size_t i = 0; i < chunks; ++i)
        {
            const auto start = i * static_cast<std::size_t>(valueColumns);
            lines.push_back(
                {i == 0 ? decodeUtf8(pitch) : U"",
                 start < song.size() ? song.substr(start, static_cast<std::size_t>(valueColumns))
                                     : U"",
                 start < saved.size() ? saved.substr(start, static_cast<std::size_t>(valueColumns))
                                      : U""});
        }
    }
    return lines;
}

bool songOrMidiFilename(const std::filesystem::path& file)
{
    const auto extension = utf8Text(file.extension());
    return sameDrumMapName(extension, ".modal") || sameDrumMapName(extension, ".mid") ||
           sameDrumMapName(extension, ".midi");
}
} // namespace

std::filesystem::path Workspace::drumMapFolder() const
{
    return configFolder_.value_or(std::filesystem::path{}) / "drum-maps";
}

void Workspace::reloadDrumMaps(bool announce)
{
    drumMaps_ = readDrumMapLibrary(drumMapFolder());
    if (!announce)
        return;
    std::size_t different = 0;
    for (const auto& track : document_.song().tracks())
    {
        if (!track->drumMap)
            continue;
        const auto matches = matchingDrumMaps(*track->drumMap, drumMaps_);
        if (std::ranges::any_of(matches, [&](std::size_t i)
                                { return *drumMaps_.maps[i].map != *track->drumMap; }))
            ++different;
    }
    const auto unreadable =
        std::ranges::count_if(drumMaps_.maps, [](const auto& map) { return !map.error.empty(); });
    drumMapNotice_.clear();
    if (different > 0)
        drumMapNotice_ =
            std::to_string(different) +
            (different == 1 ? " track's drum map differs" : " tracks' drum maps differ") +
            " from saved or built-in copies; , t k reviews them";
    if (unreadable > 0 || !drumMaps_.error.empty())
    {
        if (!drumMapNotice_.empty())
            drumMapNotice_ += "; ";
        drumMapNotice_ += "Some saved drum maps couldn't be read; , t k reviews them";
    }
    if (!drumMapNotice_.empty())
        setMessage(drumMapNotice_, MessageKind::Information);
}

std::string Workspace::drumMapSummary(const core::DrumMap& map) const
{
    const auto matches = matchingDrumMaps(map, drumMaps_);
    if (matches.empty())
        return "in song only";
    const auto different = std::ranges::count_if(matches, [&](std::size_t i)
                                                 { return *drumMaps_.maps[i].map != map; });
    if (different > 0)
        return "differs; review " + std::to_string(matches.size()) +
               (matches.size() == 1 ? " copy" : " copies");
    return matches.size() == 1 && !drumMaps_.maps[matches.front()].file ? "matches built-in"
                                                                        : "matches saved";
}

CommandResult Workspace::drumMap()
{
    const auto id = view().cursor().track;
    if (!id || where() == Where::Strip)
        return CommandResult::error("A drum map needs a track");
    if (selection_)
        return CommandResult::error("Choose a drum map on one track: leave Visual mode first");
    reloadDrumMaps(false);
    const auto& current = document_.song().track(*id)->drumMap;
    DrumMapChoice choice{.track = *id, .original = current};
    choice.offered.push_back({std::nullopt, std::nullopt, {}}); // None
    choice.offered.push_back(drumMaps_.maps.front());
    std::vector<std::string> labels{"None", "General MIDI"};
    std::size_t selected = 0;
    if (current)
    {
        choice.current = choice.offered.size();
        selected = *choice.current;
        choice.offered.push_back({std::nullopt, current, {}});
        labels.push_back(current->name + " (" + drumMapSummary(*current) + ")");
    }
    for (std::size_t i = 1; i < drumMaps_.maps.size(); ++i)
    {
        choice.offered.push_back(drumMaps_.maps[i]);
        labels.push_back(drumMaps_.maps[i].label() +
                         (drumMaps_.maps[i].error.empty() ? "" : " (unreadable)"));
    }
    choice.list = ChoiceList{std::move(labels), selected, true};
    choice.list.name("drum map");
    drumMapChoice_ = std::move(choice);
    if (!drumMaps_.error.empty())
        return {true, drumMaps_.error, true};
    return {true};
}

CommandResult Workspace::togglePitchAlwaysShow()
{
    if (where() != Where::PianoRoll)
        return CommandResult::error("Always show works only in the piano roll's notes");
    const auto cursor = view().cursor();
    const auto* track = cursor.track ? document_.song().track(*cursor.track) : nullptr;
    if (!track || !track->drumMap || !cursor.pitch)
        return CommandResult::error("Choose a drum map for this track first");
    auto map = *track->drumMap;
    const auto pitch = *cursor.pitch;
    map.alwaysShow.flip(pitch);
    const auto title =
        (map.alwaysShow[pitch] ? "Always show " : "Stop always showing ") + core::pitchName(pitch);
    return applyDrumMap(*cursor.track, std::move(map), title);
}

CommandResult Workspace::applyDrumMap(core::TrackId id, std::optional<core::DrumMap> map,
                                      std::string title)
{
    const auto* track = document_.song().track(id);
    if (track == nullptr)
        return CommandResult::error("No track to give a drum map");
    if (auto why = recording_.eventEditRefusal(id))
        return CommandResult::error(std::move(*why));
    if (title.empty())
        title = "Drum map of " + track->name + ": " + (map ? map->name : "None");
    const bool changed =
        document_.edit(title, view().place(), [&](core::Song& song)
                       { song.updateTrack(id, [&](core::Track& into) { into.drumMap = map; }); });
    if (auto why = document_.takeRefusal())
        return CommandResult::error(std::move(*why));
    refresh();
    return {changed, title, false};
}

void Workspace::reviewDrumMap(core::TrackId id, std::optional<SavedDrumMap> saved)
{
    const auto* track = document_.song().track(id);
    if (track == nullptr)
    {
        refuse("No track to review");
        return;
    }
    DrumMapChoice choice{.track = id, .original = track->drumMap};
    choice.stage = DrumMapChoice::Stage::Review;
    choice.saved = std::move(saved);
    choice.actions.push_back(DrumMapAction::Keep);
    std::vector<std::string> labels{"Keep the song's copy"};
    const auto* map = choice.saved && choice.saved->map ? &*choice.saved->map : nullptr;
    if (map && choice.original != *map)
    {
        choice.actions.push_back(DrumMapAction::UseSaved);
        labels.emplace_back("Use the saved copy");
    }
    if (choice.original && choice.saved && choice.saved->file && (!map || *choice.original != *map))
    {
        choice.actions.push_back(DrumMapAction::ReplaceSaved);
        labels.emplace_back("Replace the saved copy");
    }
    if (choice.original)
    {
        choice.actions.push_back(DrumMapAction::SaveAs);
        labels.emplace_back("Save as another map...");
    }
    if (map)
        choice.differences = drumMapDifferences(choice.original.value_or(core::DrumMap{}), *map);
    choice.list = ChoiceList{std::move(labels), 0, false};
    drumMapChoice_ = std::move(choice);
}

CommandResult Workspace::loadDrumMap(core::TrackId id, const std::filesystem::path& file)
{
    auto loaded = core::readDrumMapFile(file);
    if (!loaded.map)
        return CommandResult::error(std::move(loaded.error));
    reloadDrumMaps(false);
    const auto* track = document_.song().track(id);
    if (track == nullptr)
        return CommandResult::error("No track to give a drum map");
    if (track->drumMap && sameDrumMapName(track->drumMap->name, loaded.map->name) &&
        track->drumMap != loaded.map)
    {
        reviewDrumMap(id, SavedDrumMap{file, std::move(loaded.map), {}});
        return {true};
    }
    return applyDrumMap(id, std::move(loaded.map));
}

CommandResult Workspace::writeDrumMapCopy(const core::DrumMap& map,
                                          const std::filesystem::path& file)
{
    if (auto why = recording_.fileOperationRefusal())
        return CommandResult::error(std::move(*why));
    if (songOrMidiFilename(file))
        return CommandResult::error("Use a drum-map filename, not a song or MIDI filename");
    if (file.parent_path().lexically_normal() == drumMapFolder().lexically_normal())
    {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
            return CommandResult::error("Can't create " + utf8Text(file.parent_path()) + ": " +
                                        error.message());
    }
    if (auto error = core::writeDrumMapFile(map, file); !error.empty())
        return CommandResult::error(std::move(error));
    reloadDrumMaps(false);
    return {true, "Saved drum map to " + utf8Text(file), false};
}

CommandResult Workspace::saveDrumMap(core::TrackId id, const std::filesystem::path& file,
                                     bool overwrite)
{
    if (songOrMidiFilename(file))
        return CommandResult::error("Use a drum-map filename, not a song or MIDI filename");
    const auto* track = document_.song().track(id);
    if (track == nullptr || !track->drumMap)
        return CommandResult::error("This track has no drum map to save");
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error)
        return CommandResult::error("Can't check " + utf8Text(file) + ": " + error.message());
    if (exists)
    {
        if (!overwrite)
            return CommandResult::error(utf8Text(file.filename()) +
                                        " exists: :drum-map-save! reviews it before replacing it");
        auto loaded = core::readDrumMapFile(file);
        if (loaded.map == track->drumMap)
            return {false, "The saved drum map already matches", false};
        reviewDrumMap(id, SavedDrumMap{file, std::move(loaded.map), std::move(loaded.error)});
        return {true};
    }
    return writeDrumMapCopy(*track->drumMap, file);
}

CommandResult Workspace::drumMapFile(const CommandCall& call, bool save)
{
    const auto id = view().cursor().track;
    if (!id || where() == Where::Strip)
        return CommandResult::error("A drum map needs a track");
    if (auto why = recording_.fileOperationRefusal())
        return CommandResult::error(std::move(*why));
    const auto* track = document_.song().track(*id);
    if (save && !track->drumMap)
        return CommandResult::error("This track has no drum map to save");
    const auto text = trimmed(call.arguments);
    if (!text.empty())
    {
        if (text.ends_with('!'))
            return CommandResult::error("! goes after the command: :drum-map-save! FILE");
        auto file = typedPath(text, drumMapFolder(), home_);
        if (!file.has_extension())
            file += ".drum-map";
        return save ? saveDrumMap(*id, file, call.bang) : loadDrumMap(*id, file);
    }
    if (fileHost_ == nullptr)
        return CommandResult::error("No file dialog: give a drum-map filename");
    const auto generation = ++drumMapDialog_;
    const auto version = document_.version();
    const auto done =
        [this, save, id = *id, generation, version](std::optional<std::filesystem::path> file)
    {
        if (!file)
            return;
        if (generation != drumMapDialog_ || version != document_.version())
        {
            report(CommandResult::error(
                "The song changed while choosing a drum-map file; open it again"));
            return;
        }
        if (auto why = recording_.fileOperationRefusal())
        {
            report(CommandResult::error(std::move(*why)));
            return;
        }
        if (!file->has_extension())
            *file += ".drum-map";
        report(save ? saveDrumMap(id, *file, true) : loadDrumMap(id, *file));
    };
    if (save)
    {
        std::error_code error;
        std::filesystem::create_directories(drumMapFolder(), error);
        if (error)
            return CommandResult::error("Can't create drum-map folder: " + error.message());
        fileHost_->chooseSave(FileHost::FileKind::DrumMap,
                              suggestedDrumMap(drumMapFolder(), track->drumMap->name), done);
    }
    else
        fileHost_->chooseOpenFile(FileHost::FileKind::DrumMap, drumMapFolder(), done);
    return {true};
}

void Workspace::saveDrumMapAs(core::TrackId id)
{
    const auto* track = document_.song().track(id);
    if (track == nullptr || !track->drumMap)
    {
        refuse("This track has no drum map to save");
        return;
    }
    if (fileHost_ == nullptr)
    {
        refuse("No save dialog: :drum-map-save FILE saves a copy");
        return;
    }
    const auto original = *track->drumMap;
    const auto generation = ++drumMapDialog_;
    const auto version = document_.version();
    askName("New drum map name: ", original.name + " copy",
            [this, id, original, generation, version](const std::string& name)
            {
                const auto given = trimmed(name);
                if (given.empty())
                {
                    refuse("A drum map needs a name");
                    return;
                }
                auto map = original;
                map.name = std::string{given};
                reloadDrumMaps(false);
                if (sameDrumMapName(map.name, original.name) ||
                    !matchingDrumMaps(map, drumMaps_).empty())
                {
                    refuse("Choose a new map name, distinct from its saved copies");
                    return;
                }
                if (generation != drumMapDialog_ || document_.version() != version)
                {
                    refuse("The song changed while naming a drum map; open it again");
                    return;
                }
                std::error_code error;
                std::filesystem::create_directories(drumMapFolder(), error);
                if (error)
                {
                    refuse("Can't create drum-map folder: " + error.message());
                    return;
                }
                fileHost_->chooseSave(
                    FileHost::FileKind::DrumMap, suggestedDrumMap(drumMapFolder(), map.name),
                    [this, id, map, generation, version](std::optional<std::filesystem::path> file)
                    {
                        if (!file)
                            return;
                        if (generation != drumMapDialog_ || document_.version() != version)
                        {
                            refuse("The song changed while saving a drum map; open it again");
                            return;
                        }
                        if (auto why = recording_.eventEditRefusal(id))
                        {
                            refuse(std::move(*why));
                            return;
                        }
                        if (!file->has_extension())
                            *file += ".drum-map";
                        auto saved = writeDrumMapCopy(map, *file);
                        if (!saved.refused)
                        {
                            const auto changed = applyDrumMap(id, map);
                            if (changed.refused)
                                saved = changed;
                        }
                        report(saved);
                    });
            });
}

bool Workspace::drumMapChoiceKey(const Key& key)
{
    message_.clear();
    if (key.named == NamedKey::Escape)
    {
        drumMapChoice_.reset();
        return true;
    }
    auto& choice = *drumMapChoice_;
    if (key.named == NamedKey::PageUp || key.named == NamedKey::PageDown)
    {
        if (choice.stage == DrumMapChoice::Stage::Review)
        {
            const int columns =
                std::max((width_ - 2 * BarSpacing::forCell(cell_).margin) / cell_.width, 0);
            const int valueColumns = std::max((columns - std::min(9, columns) - 2) / 2, 1);
            const auto lines = drumMapReviewLines(choice.differences, valueColumns);
            const auto rows = static_cast<std::size_t>(std::max(
                messageRow().y / cell_.height - static_cast<int>(choice.actions.size()) - 4, 0));
            const auto last = lines.size() > rows ? lines.size() - rows : std::size_t{0};
            choice.top = std::min(choice.top, last);
            choice.top = key.named == NamedKey::PageUp ? (choice.top > 8 ? choice.top - 8 : 0)
                                                       : std::min(choice.top + 8, last);
        }
        else
            choice.list.run(key.named == NamedKey::PageUp ? "command-line-page-up"
                                                          : "command-line-page-down");
        return true;
    }
    if (key.named == NamedKey::Up || key.named == NamedKey::Down)
        choice.list.run(key.named == NamedKey::Up ? "command-line-older" : "command-line-newer");
    else if (key.named == NamedKey::Backspace)
        choice.list.run("command-line-backspace");
    else if (key.character == U'u' && key.modifiers == kCtrl)
        choice.list.run("command-line-clear");
    else if (key.named == NamedKey::Enter)
    {
        const auto chosen = choice.list.chosen();
        if (!chosen)
        {
            refuse(choice.list.nothing());
            return true;
        }
        const auto id = choice.track;
        const auto* track = document_.song().track(id);
        if (track == nullptr || track->drumMap != choice.original)
        {
            drumMapChoice_.reset();
            refuse("The track's drum map changed; open it again");
            return true;
        }
        if (choice.stage == DrumMapChoice::Stage::Review)
        {
            const auto action = choice.actions[*chosen];
            if (action == DrumMapAction::Keep)
                drumMapChoice_.reset();
            else if (action == DrumMapAction::SaveAs)
            {
                drumMapChoice_.reset();
                saveDrumMapAs(id);
            }
            else
            {
                auto saved = *choice.saved;
                if (saved.file)
                {
                    auto fresh = core::readDrumMapFile(*saved.file);
                    if (fresh.map != saved.map || fresh.error != saved.error)
                    {
                        reviewDrumMap(id, SavedDrumMap{saved.file, std::move(fresh.map),
                                                       std::move(fresh.error)});
                        setMessage("The saved file changed; review its current copy",
                                   MessageKind::Warning);
                        return true;
                    }
                }
                auto result = action == DrumMapAction::UseSaved
                                  ? applyDrumMap(id, saved.map)
                                  : writeDrumMapCopy(*choice.original, *saved.file);
                if (!result.refused)
                    drumMapChoice_.reset();
                report(result);
            }
            return true;
        }
        if (choice.stage == DrumMapChoice::Stage::Maps && chosen == choice.current)
        {
            const auto matches = matchingDrumMaps(*choice.original, drumMaps_);
            if (matches.size() <= 1)
                reviewDrumMap(id, matches.empty() ? std::nullopt
                                                  : std::optional{drumMaps_.maps[matches.front()]});
            else
            {
                choice.stage = DrumMapChoice::Stage::Copies;
                choice.offered.clear();
                choice.current.reset();
                std::vector<std::string> labels;
                for (const auto i : matches)
                {
                    choice.offered.push_back(drumMaps_.maps[i]);
                    labels.push_back(
                        drumMaps_.maps[i].label() +
                        (*drumMaps_.maps[i].map == *choice.original ? " (same)" : " (differs)"));
                }
                choice.list = ChoiceList{std::move(labels), 0, true};
                choice.list.name("saved copy");
            }
            return true;
        }
        auto saved = choice.offered[*chosen];
        if (saved.file)
        {
            auto fresh = core::readDrumMapFile(*saved.file);
            saved.map = std::move(fresh.map);
            saved.error = std::move(fresh.error);
        }
        if (!saved.error.empty())
        {
            refuse(saved.error);
            return true;
        }
        if (choice.stage == DrumMapChoice::Stage::Copies ||
            (choice.original && saved.map &&
             sameDrumMapName(choice.original->name, saved.map->name) &&
             choice.original != saved.map))
            reviewDrumMap(id, std::move(saved));
        else
        {
            const auto result = applyDrumMap(id, saved.map);
            if (!result.refused)
                drumMapChoice_.reset();
            report(result);
        }
    }
    else if (key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0)
        choice.list.type(key.character);
    return true;
}

Region Workspace::drumMapChoiceRegion() const
{
    const auto& choice = *drumMapChoice_;
    const auto bottom = messageRow();
    const int h = cell_.height;
    const int w = cell_.width;
    const int margin = BarSpacing::forCell(cell_).margin;
    const Rect within{0, 0, width_, bottom.y};
    const Rect field{margin, bottom.y, std::min(40 * w, std::max(width_ - 2 * margin, 0)), h};
    auto actions = choice.list.region(field, within, cell_);
    if (choice.stage != DrumMapChoice::Stage::Review)
        return actions;
    const int columns = std::max((width_ - 2 * margin) / w, 0);
    const int pitchColumns = std::min(9, columns);
    const int valueColumns = std::max((columns - pitchColumns - 2) / 2, 1);
    const auto lines = drumMapReviewLines(choice.differences, valueColumns);
    const auto rowsWanted = 4 + std::max(lines.size(), std::size_t{1}) + choice.actions.size();
    const int height = static_cast<int>(std::min(rowsWanted * static_cast<std::size_t>(h),
                                                 static_cast<std::size_t>(std::max(bottom.y, 0))));
    const int top = bottom.y - height;
    Region region{.bounds = {0, top, width_, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style heading{Role::Accent, Role::Panel, kBold};
    const Style text{Role::Text, Role::Panel, kPlain};
    const Style dim{Role::Dim, Role::Panel, kPlain};
    const auto put = [&](int x, int y, std::u32string value, Style style, int columns)
    {
        columns = std::min(columns, std::max((width_ - margin - x) / w, 0));
        if (columns <= 0 || y < 0 || y + h > actions.bounds.y - top)
            return;
        if (value.size() > static_cast<std::size_t>(columns))
        {
            value.resize(static_cast<std::size_t>(columns));
            value.back() = U'…';
        }
        region.text.push_back({x, y, std::move(value), style});
    };
    const auto source = choice.saved ? choice.saved->label() : std::string{"no saved copy"};
    put(margin, 0,
        decodeUtf8("Drum map: " + (choice.original ? choice.original->name : "None") + " — " +
                   source),
        heading, columns);
    const int songX = margin + pitchColumns * w;
    const int savedX = songX + (valueColumns + 2) * w;
    put(margin, h, U"Pitch", dim, pitchColumns);
    put(songX, h, U"Song", heading, valueColumns);
    put(savedX, h, U"Saved", heading, valueColumns);
    if (lines.empty())
        put(margin, 3 * h,
            decodeUtf8(choice.saved && !choice.saved->error.empty() ? choice.saved->error
                       : choice.saved ? "The copies are identical"
                                      : "This map is kept only in the song"),
            dim, columns);
    const auto rows = static_cast<std::size_t>(std::max((actions.bounds.y - top) / h - 4, 0));
    const auto first =
        std::min(choice.top, lines.size() > rows ? lines.size() - rows : std::size_t{0});
    for (std::size_t i = first; i < lines.size() && i < first + rows; ++i)
    {
        const int y = (static_cast<int>(i - first) + 3) * h;
        put(margin, y, lines[i][0], dim, pitchColumns);
        put(songX, y, lines[i][1], text, valueColumns);
        put(savedX, y, lines[i][2], text, valueColumns);
    }
    for (auto run : actions.text)
    {
        run.x += actions.bounds.x;
        run.y += actions.bounds.y - top;
        region.text.push_back(std::move(run));
    }
    return region;
}

} // namespace modal::ui
