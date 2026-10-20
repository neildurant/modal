#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace modal::core
{

// Where an edit was made, for undo and redo to go back to: the cursor's track (none for the
// conductor) and position, and the view's state (its scroll and zoom, or page), kept as the
// view gives it. Moving and scrolling aren't undo steps themselves.
struct EditPlace
{
    std::optional<TrackId> track;
    Tick tick = 0;
    std::vector<std::int64_t> view = {};

    friend bool operator==(const EditPlace&, const EditPlace&) = default;
};

// One undo step: what it did, as a title that reads on its own in a list ("Delete 3 events in
// Bass"), where, and when.
struct EditStep
{
    std::string title;
    EditPlace place;
    std::chrono::system_clock::time_point time;
};

// A song being edited, with its file and its undo history. Every change to the song goes
// through it, as one undo step, or part of a group made one step; undo and redo go back and
// forth through the history, which is linear. Past versions are kept as the song's parts,
// sharing what each edit didn't touch, so a step costs about what it changed.
class Document
{
public:
    static constexpr std::size_t kDefaultUndoLevels = 1000;

    explicit Document(Song song = Song{}, std::optional<std::filesystem::path> file = std::nullopt);

    const Song& song() const { return song_; }

    // The file the song was loaded from or last saved to, if any.
    const std::optional<std::filesystem::path>& file() const { return file_; }

    // Starts again with another song, as when opening a file: no history, and unmodified.
    void replace(Song song, std::optional<std::filesystem::path> file);

    // Changes a copy of the song and keeps it, as one undo step, unless the change left the
    // song as it was. Returns whether it changed the song.
    bool edit(std::string title, const EditPlace& place, const std::function<void(Song&)>& change);

    // Makes the edits until the matching end one undo step, with this title and place, as a
    // recording pass, a macro or a repeat with . will. Groups can nest; the outermost counts.
    void beginGroup(std::string title, const EditPlace& place);
    void endGroup();

    // Undoes or redoes a step, returning it, to say what was done and go back to where; or
    // nothing if there's no step to undo or redo.
    std::optional<EditStep> undo();
    std::optional<EditStep> redo();

    bool canUndo() const { return current_ > 0; }
    bool canRedo() const { return current_ + 1 < states_.size(); }

    // Whether the song differs from the version last saved or loaded; undoing back to that
    // version makes it unmodified again.
    bool modified() const { return states_[current_].id != savedId_; }

    // Records that the song as it is now has been saved, to a file if given.
    void markSaved(std::optional<std::filesystem::path> file = std::nullopt);

    // How many steps can be undone (undolevels); the oldest go when there are more. Steps
    // that can be redone don't count.
    void setUndoLevels(std::size_t levels);

    // Called after every change to the song, by an edit, undo, redo or replace, as when
    // handing the engine each new snapshot.
    using Listener = std::function<void(const Song&)>;
    void setListener(Listener listener) { listener_ = std::move(listener); }

private:
    // A version of the song, and the step that made it (none for the first).
    struct State
    {
        Song::Parts parts;
        std::uint64_t id;
        std::optional<EditStep> step;
    };

    // Keeps the song as it is now as a new version, dropping any that could have been
    // redone and the oldest beyond the undo levels.
    void record(EditStep step);
    // Goes to a version, rebuilding the song from its parts.
    void restore(std::size_t index);
    void trim();
    void changed();

    Song song_;
    std::optional<std::filesystem::path> file_;
    std::deque<State> states_;
    std::size_t current_ = 0;
    std::uint64_t nextId_ = 0;
    std::uint64_t savedId_ = 0;
    std::size_t undoLevels_ = kDefaultUndoLevels;
    int groupDepth_ = 0;
    std::optional<EditStep> group_; // the step being grouped
    Listener listener_;
};

} // namespace modal::core
