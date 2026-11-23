// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>

namespace modal::ui
{

void Workspace::notePut(std::string_view command, int count, Mode mode,
                        const std::optional<Selection>& selection, const EventRegister& put)
{
    // What [p and ]p need to put another copy in its place: the put, as it was asked for, and
    // where the song and cursor were left, which tell that nothing's happened since.
    const auto at = std::ranges::find(pasteHistory_, put);
    const auto index = swapTo_ ? swapTo_
                       : at == pasteHistory_.end()
                           ? std::nullopt
                           : std::optional{static_cast<std::size_t>(at - pasteHistory_.begin())};
    lastPut_ = LastPut{.command = std::string{command},
                       .count = count,
                       .mode = mode,
                       .selection = selection,
                       .index = index,
                       .version = document_.version(),
                       .song = songNumber_,
                       .view = shown(),
                       .after = view().cursor()};
    // Where the copy put is in the history, while there's another to swap it for (decided
    // 23 November 2026, as what [p, ]p and p would put was hard to picture).
    if (index && pasteHistory_.size() > 1)
    {
        setMessage(message_ + " · \"" + std::to_string(*index + 1) + " of " +
                       std::to_string(pasteHistory_.size()) + " (" + keysOf("put-older") +
                       " older, " + keysOf("put-newer") + " newer)",
                   messageKind_);
    }
}

std::optional<std::size_t> Workspace::unnamedInHistory() const
{
    if (lastPut_ && lastPut_->index && *lastPut_->index < pasteHistory_.size() &&
        pasteHistory_[*lastPut_->index] == register_)
        return lastPut_->index;
    const auto at = std::ranges::find(pasteHistory_, register_);
    if (at == pasteHistory_.end())
        return std::nullopt;
    return static_cast<std::size_t>(at - pasteHistory_.begin());
}

bool Workspace::swapPut(int count, bool older)
{
    // Straight after a put (decided 23 November 2026): the song, the view and the cursor as
    // the put left them.
    repeat_.notAChange = true; // . goes on repeating the put, of what's there now
    const bool straightAfter = lastPut_ && lastPut_->version == document_.version() &&
                               lastPut_->song == songNumber_ && lastPut_->view == shown() &&
                               lastPut_->after == view().cursor();
    if (!straightAfter)
    {
        lastPut_.reset();
        refuse(keysOf("put-older") + " and " + keysOf("put-newer") +
                   " swap what was just put for an older or newer copy from the paste "
                   "history, straight after a put",
               MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        return true;
    }
    const auto steps = static_cast<std::size_t>(std::max(count, 1));
    const auto& last = *lastPut_;
    std::optional<std::size_t> target;
    if (older)
    {
        const auto to = last.index ? *last.index + steps : steps - 1;
        if (to < pasteHistory_.size())
            target = to;
    }
    else if (last.index && *last.index >= steps)
    {
        target = *last.index - steps;
    }
    if (!target)
    {
        refuse(older ? "No older copy in the paste history, which holds " +
                           std::to_string(pasteHistory_.size())
                     : std::string{"No newer copy in the paste history"},
               MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        return true;
    }

    // The put undone, then the other copy put as it was, the two one undo step as the put
    // was; the unnamed register holding what's there, for . and p, as if it had been put.
    const auto put = last;
    const auto step = document_.undo();
    if (!step)
        return true;
    view().restore(step->place);
    mode_ = put.mode;
    selection_ = put.selection;
    register_ = pasteHistory_[*target];
    registerSeen_ = register_;
    swapTo_ = target;
    run(put.command, put.count); // saying what it put, and where that is in the history
    swapTo_.reset();
    return true;
}

} // namespace modal::ui
