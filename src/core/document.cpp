#include <modal/core/document.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

namespace modal::core
{

Document::Document(Song song, std::optional<std::filesystem::path> file)
    : song_(std::move(song))
{
    replace(song_, std::move(file));
}

void Document::replace(Song song, std::optional<std::filesystem::path> file)
{
    song_ = std::move(song);
    file_ = std::move(file);
    states_.clear();
    states_.push_back({song_.parts(), nextId_++, std::nullopt});
    current_ = 0;
    savedId_ = states_.front().id;
    groupDepth_ = 0;
    group_.reset();
    changed();
}

bool Document::edit(std::string title, const EditPlace& place,
                    const std::function<void(Song&)>& change)
{
    Song edited = song_;
    change(edited);
    if (edited == song_)
    {
        return false;
    }
    song_ = std::move(edited);
    if (groupDepth_ == 0)
    {
        record({std::move(title), place, std::chrono::system_clock::now()});
    }
    changed();
    return true;
}

void Document::beginGroup(std::string title, const EditPlace& place)
{
    if (groupDepth_++ == 0)
    {
        group_ = EditStep{std::move(title), place, std::chrono::system_clock::now()};
    }
}

void Document::endGroup()
{
    assert(groupDepth_ > 0);
    if (--groupDepth_ > 0)
    {
        return;
    }
    // Edits that undid each other make no step.
    if (!(Song{states_[current_].parts} == song_))
    {
        record(std::move(*group_));
    }
    group_.reset();
}

std::optional<EditStep> Document::undo()
{
    if (!canUndo() || groupDepth_ > 0)
    {
        return std::nullopt;
    }
    auto step = *states_[current_].step;
    restore(current_ - 1);
    return step;
}

std::optional<EditStep> Document::redo()
{
    if (!canRedo() || groupDepth_ > 0)
    {
        return std::nullopt;
    }
    restore(current_ + 1);
    return *states_[current_].step;
}

void Document::markSaved(std::optional<std::filesystem::path> file)
{
    savedId_ = states_[current_].id;
    if (file)
    {
        file_ = std::move(file);
    }
}

void Document::setUndoLevels(std::size_t levels)
{
    undoLevels_ = levels;
    trim();
}

void Document::record(EditStep step)
{
    states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(current_) + 1, states_.end());
    states_.push_back({song_.parts(), nextId_++, std::move(step)});
    current_ = states_.size() - 1;
    trim();
}

void Document::restore(std::size_t index)
{
    auto parts = states_[index].parts;
    // Track IDs are never reused, even for a track added after undoing the one that had it.
    parts.nextTrackId = std::max(parts.nextTrackId, song_.parts().nextTrackId);
    song_ = Song{std::move(parts)};
    current_ = index;
    trim();
    changed();
}

void Document::trim()
{
    // Only steps that can be undone count; what can be redone stays.
    while (current_ > undoLevels_)
    {
        states_.pop_front();
        --current_;
    }
}

void Document::changed()
{
    if (listener_)
    {
        listener_(song_);
    }
}

} // namespace modal::core
