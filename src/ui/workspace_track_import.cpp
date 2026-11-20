// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/tempo_map.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/track_import.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

namespace
{

std::string_view trimmedText(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::size_t noteCount(const core::Song& song, core::TrackId id)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(song.events(id).all(), [](const core::Event& event)
                              { return std::holds_alternative<core::Note>(event.data); }));
}

} // namespace

CommandResult Workspace::importTracks(const CommandCall& call)
{
    if (auto why = recording_.fileOperationRefusal())
        return CommandResult::error(std::move(*why));
    const auto text = trimmedText(call.arguments);
    const auto folder = songFolder(document_, home_);
    const auto pathOf = [&](std::string_view typed)
    {
        auto path = typedPath(typed, folder, home_);
        if (!path.has_extension())
            path += ".modal";
        return path;
    };
    if (!text.empty())
    {
        std::error_code error;
        const auto whole = pathOf(text);
        if (std::filesystem::is_regular_file(whole, error))
            return importTracksFrom(whole, std::nullopt);
        // FILE TRACKS: the first name before a selection that's a file.
        for (auto space = text.find(' '); space != std::string_view::npos;
             space = text.find(' ', space + 1))
        {
            const auto rest = text.substr(space + 1);
            if (!looksLikeTrackSelection(rest))
                continue;
            const auto file = pathOf(trimmedText(text.substr(0, space)));
            if (std::filesystem::is_regular_file(file, error))
                return importTracksFrom(file, std::string{rest});
        }
        return CommandResult::error("Can't find " + utf8Text(whole));
    }
    if (fileHost_ == nullptr)
        return CommandResult::error("No file picker: give a filename");
    const auto version = document_.version();
    fileHost_->chooseOpen(
        [this, version](std::optional<std::filesystem::path> file)
        {
            if (!file)
                return;
            if (version != document_.version())
            {
                report(
                    CommandResult::error("The song changed while choosing a file; import again"));
                return;
            }
            report(importTracksFrom(*file, std::nullopt));
        });
    return {true};
}

CommandResult Workspace::importTracksFrom(const std::filesystem::path& file,
                                          std::optional<std::string> selection)
{
    if (auto why = recording_.fileOperationRefusal())
        return CommandResult::error(std::move(*why));
    // Read from disk, as a song of its own, never opened in the window (decided 20 November
    // 2026: an open song's tracks can be yanked instead).
    core::Document read;
    if (auto opened = openFile(read, file); !opened.opened)
        return CommandResult::error(std::move(opened.message));
    auto from = read.song();
    const auto name = utf8Text(file.filename());
    const auto tracks = from.tracks();
    if (tracks.empty())
        return CommandResult::error(name + " has no tracks");
    if (selection)
    {
        const auto parsed = parseTrackSelection(*selection, tracks.size());
        if (const auto* why = std::get_if<std::string>(&parsed))
            return CommandResult::error(*why);
        const auto& chosen = std::get<std::vector<std::size_t>>(parsed);
        if (chosen.empty())
            return CommandResult::error("No tracks chosen");
        return importChosenTracks(from, name, chosen);
    }

    // The chooser: each track by its number in the file, with what tells parts apart.
    std::size_t nameWidth = 0;
    for (const auto& track : tracks)
        nameWidth = std::max(nameWidth, decodeUtf8(track->name).size());
    nameWidth = std::min<std::size_t>(nameWidth, 24);
    const auto numberWidth = std::to_string(tracks.size()).size();
    std::vector<std::string> names;
    std::vector<std::u32string> rows;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = *tracks[i];
        auto number = std::to_string(i + 1);
        number.insert(0, numberWidth - number.size(), ' ');
        auto trackName = decodeUtf8(track.name);
        if (trackName.size() > nameWidth)
        {
            trackName.resize(nameWidth);
            trackName.back() = U'…';
        }
        trackName.resize(nameWidth, U' ');
        const auto notes = noteCount(from, track.id());
        auto channel = std::to_string(track.channel);
        channel.insert(0, 2 - std::min<std::size_t>(channel.size(), 2), ' ');
        auto count = std::to_string(notes);
        count.insert(0, 5 - std::min<std::size_t>(count.size(), 5), ' ');
        rows.push_back(decodeUtf8(number + "  ") + trackName +
                       decodeUtf8("  ch " + channel + "  " + count +
                                  (notes == 1 ? " note " : " notes") +
                                  (track.drumMap ? "  " + track.drumMap->name : std::string{})));
        names.push_back(track.name);
    }
    trackImportChoice_.emplace(TrackImportChoice{ChoiceList{std::move(names), 0, false},
                                                 std::move(rows),
                                                 std::vector<bool>(tracks.size(), false), file});
    trackImportChoice_->list.name("track");
    return {true};
}

CommandResult Workspace::importChosenTracks(const core::Song& from, const std::string& name,
                                            std::span<const std::size_t> chosen)
{
    if (auto why = recording_.fileOperationRefusal())
        return CommandResult::error(std::move(*why));
    std::vector<core::TrackCopy> copies;
    for (const auto i : chosen)
    {
        if (auto copy = from.copyTrack(from.tracks()[i]->id()))
            copies.push_back(std::move(*copy));
    }
    const auto sections = document_.song().sections().size();
    auto result = putTracks(std::move(copies), false, 1, "Import tracks from " + name);
    if (result.refused)
        return result;

    // The tracks made, from the first, where putTracks leaves the cursor, by their names now.
    const auto& song = document_.song();
    const auto tracks = song.tracks();
    const auto cursor = view().cursor().track;
    const auto first = static_cast<std::size_t>(
        std::ranges::find_if(tracks, [&](const auto& t) { return cursor && t->id() == *cursor; }) -
        tracks.begin());
    std::string names;
    for (std::size_t k = 0; k < chosen.size(); ++k)
    {
        const auto& made =
            first + k < tracks.size() ? tracks[first + k]->name : from.tracks()[chosen[k]]->name;
        names += (k == 0 ? "" : ", ") + made;
    }
    auto message = "Read " + std::to_string(chosen.size()) +
                   (chosen.size() == 1 ? " track from " : " tracks from ") + name + ": " + names +
                   sectionsAddedText(song, sections);
    // Tempo stays with the song (decided 20 November 2026), so a different one is mentioned.
    const auto theirs = core::bpmText(from.tempo().tempoAt(0));
    const auto ours = core::bpmText(song.tempo().tempoAt(0));
    if (theirs != ours)
        message += "; its tempo was " + theirs + ", this song plays at " + ours;
    return {true, std::move(message), false};
}

bool Workspace::trackImportKey(const Key& key)
{
    message_.clear();
    auto& choice = *trackImportChoice_;
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
    {
        trackImportChoice_.reset();
        return true;
    }
    if (command == "command-line-run")
    {
        std::vector<std::size_t> chosen;
        for (std::size_t i = 0; i < choice.ticked.size(); ++i)
        {
            if (choice.ticked[i])
                chosen.push_back(i);
        }
        if (chosen.empty())
        {
            if (const auto highlighted = choice.list.chosen())
                chosen.push_back(*highlighted);
        }
        // As the line that does the same (decided 20 November 2026, after use): run, and kept
        // in the : prompt's history to recall and change.
        const auto selection = trackSelectionText(chosen, choice.ticked.size());
        const auto file = std::move(choice.file);
        trackImportChoice_.reset();
        prompt_.line.open(decodeUtf8("read " + utf8Text(file) + " " + selection));
        prompt_.line.take();
        report(importTracksFrom(file, selection));
        return true;
    }
    if (key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0)
    {
        if (key.character == U' ')
        {
            if (const auto highlighted = choice.list.chosen())
                choice.ticked[*highlighted] = !choice.ticked[*highlighted];
        }
        else if (key.character == U'a')
        {
            // Every track, or none once every one is ticked.
            const bool every = std::ranges::all_of(choice.ticked, [](bool on) { return on; });
            std::fill(choice.ticked.begin(), choice.ticked.end(), !every);
        }
        else if (key.character == U'j' || key.character == U'k')
            choice.list.run(key.character == U'j' ? "command-line-newer" : "command-line-older");
        return true;
    }
    choice.list.run(command);
    return true;
}

Region Workspace::trackImportRegion() const
{
    const auto& choice = *trackImportChoice_;
    const auto bar = messageRow();
    std::vector<std::u32string> labels;
    for (std::size_t i = 0; i < choice.rows.size(); ++i)
        labels.push_back((choice.ticked[i] ? U"[x] " : U"[ ] ") + choice.rows[i]);
    const Rect field{0, bar.y, bar.width, cell_.height};
    return choice.list.region(field, {0, 0, bar.width, field.y}, cell_, labels);
}

} // namespace modal::ui
