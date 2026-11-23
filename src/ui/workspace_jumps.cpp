// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>

namespace modal::ui
{

namespace
{

std::string viewName(ViewKind kind)
{
    switch (kind)
    {
    case ViewKind::Arrangement:
        return "Arrangement";
    case ViewKind::PianoRoll:
        return "Piano roll";
    case ViewKind::EventList:
        return "Event list";
    case ViewKind::Tracks:
        return "Tracks";
    case ViewKind::Ports:
        return "Ports";
    case ViewKind::Metronome:
        return "Metronome";
    }
    return {};
}

} // namespace

void Workspace::jumped(const SongCursor& from)
{
    jumpedFrom_ = from;
    recordJump({songNumber_, shown(), from});
}

void Workspace::recordJump(const JumpPlace& from)
{
    // Views shown as Modal starts, before any key, are no places the user was.
    if (walkingJumps_ || !keyHandled_)
        return;
    // After going back, a new jump drops the places stepped back over, as a browser's history
    // does (decided 23 November 2026).
    if (jumpAt_ < jumps_.size())
        jumps_.erase(jumps_.begin() + static_cast<std::ptrdiff_t>(jumpAt_) + 1, jumps_.end());
    if (jumps_.empty() || jumps_.back() != from)
        jumps_.push_back(from);
    if (jumps_.size() > kMostJumps)
        jumps_.erase(jumps_.begin(),
                     jumps_.begin() + static_cast<std::ptrdiff_t>(jumps_.size() - kMostJumps));
    jumpAt_ = jumps_.size();
}

bool Workspace::usableJump(const JumpPlace& place) const
{
    // In the song shown, and on a track it still has.
    return place.song == songNumber_ &&
           (!place.cursor.track || document_.song().track(*place.cursor.track) != nullptr);
}

void Workspace::goToJump(const JumpPlace& place)
{
    if (shown() != place.view)
        show(place.view);
    auto to = place.cursor;
    const auto end = document_.song().end();
    if (to.tick >= end)
        to.tick = std::max<core::Tick>(end - 1, 0); // the song's shorter now
    view().setCursor(to);
}

bool Workspace::walkJumps(int count, bool back)
{
    const auto here = this->here();
    const auto before = std::pair{jumps_, jumpAt_}; // as it was, if there's nowhere to go
    if (jumpAt_ >= jumps_.size())
    {
        if (!back)
        {
            refuse("No later place in the jump list", MessageKind::Information);
            macros_.failed = macros_.depth > 0;
            return true;
        }
        // The first step back keeps where it started, so Ctrl-i comes back to it.
        if (jumps_.empty() || jumps_.back() != here)
            jumps_.push_back(here);
        if (jumps_.size() > kMostJumps)
            jumps_.erase(jumps_.begin());
        jumpAt_ = jumps_.size() - 1;
    }
    std::optional<std::size_t> found;
    auto left = std::max(count, 1);
    for (auto i = jumpAt_; left > 0;)
    {
        if (back ? i == 0 : i + 1 >= jumps_.size())
            break;
        i = back ? i - 1 : i + 1;
        if (usableJump(jumps_[i]) && jumps_[i] != here)
        {
            found = i;
            --left;
        }
    }
    if (!found)
    {
        std::tie(jumps_, jumpAt_) = before;
        refuse(back ? "No earlier place in the jump list" : "No later place in the jump list",
               MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        return true;
    }
    walkingJumps_ = true;
    goToJump(jumps_[*found]);
    walkingJumps_ = false;
    jumpAt_ = *found;
    jumpedFrom_ = here.cursor; // so '' goes back, as after any jump
    return true;
}

std::string Workspace::jumpAbout(const JumpPlace& place) const
{
    // "Piano roll, 12:1:000, Bass, C3", as :marks says a mark's place.
    const auto& song = document_.song();
    std::string text = viewName(place.view) + ", " + positionText(song.meter(), place.cursor.tick);
    if (place.cursor.track)
    {
        const auto& tracks = song.tracks();
        const auto it = std::ranges::find_if(tracks, [&](const auto& t)
                                             { return t->id() == *place.cursor.track; });
        if (it != tracks.end())
            text += ", " + ((*it)->name.empty() ? "Track " + std::to_string(it - tracks.begin() + 1)
                                                : (*it)->name);
    }
    if (place.cursor.pitch && place.view == ViewKind::PianoRoll)
        text += ", " + core::pitchName(*place.cursor.pitch);
    return text;
}

bool Workspace::sayJumps()
{
    // This song's places, oldest first, numbered in the list,
    // the one the cursor was last sent to marked with >.
    std::vector<std::pair<std::string, std::string>> lines;
    for (std::size_t i = 0; i < jumps_.size(); ++i)
    {
        if (!usableJump(jumps_[i]))
            continue;
        lines.emplace_back((i == jumpAt_ ? ">" : "") + std::to_string(i + 1), jumpAbout(jumps_[i]));
    }
    if (lines.empty())
    {
        setMessage("No jumps yet: Ctrl-o goes back to where a jump came from",
                   MessageKind::Information);
        return true;
    }
    if (jumpAt_ >= jumps_.size())
        lines.emplace_back(">", "here: " + jumpAbout(here()));
    listing_ = std::move(lines);
    return true;
}

} // namespace modal::ui
