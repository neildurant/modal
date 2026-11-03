// SPDX-License-Identifier: GPL-3.0-or-later

// The workspace's search (/, n, *, gn and the , s group's matches) and finds (f, F) (split
// from workspace.cpp, November 2026, after a code review).

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <tuple>
#include <utility>

namespace modal::ui
{

bool Workspace::search(const std::string& text)
{
    // An empty search has nothing to match, so clears the matches (decided November 2026).
    const auto start = text.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        searching_.query.reset();
        return true;
    }
    const auto parsed = core::parseQuery(text, scales());
    if (!parsed.query)
    {
        setMessage(parsed.error, MessageKind::Warning);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    searching_.query = parsed.query;
    searching_.text = text.substr(start, text.find_last_not_of(' ') - start + 1);
    searching_.forward = true;
    return goToMatch(true, 0);
}

bool Workspace::searchHere(bool forward, int count)
{
    // The exact pitch, as Vim's * takes the whole word (decided November 2026), or the
    // controller.
    const auto query = view().queryHere();
    if (!query)
    {
        setMessage("No note or controller here to search for", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    searching_.query = query;
    searching_.text = query->kind == core::EventQuery::Kind::Controller
                          ? "cc" + std::to_string(*query->controller)
                      : query->kind == core::EventQuery::Kind::Bend
                          ? std::string{"bend"}
                          : core::pitchName(*query->pitch);
    searching_.forward = forward;
    return goToMatch(true, count);
}

bool Workspace::goToMatch(bool next, int count)
{
    // A jump, as in Vim, so '' goes back from it.
    const bool forward = next == searching_.forward;
    const auto command = forward ? "cursor-match-next" : "cursor-match-previous";
    const auto before = view().cursor();
    const auto moved = view().run(command, count);
    if (!moved)
    {
        return sayWhereFor(command);
    }
    if (wasPlaying_)
    {
        following_ = false;
    }
    if (view().cursor() == before)
    {
        const bool on = view().matchStatus(*searching_.query).starts_with("match");
        setMessage(on ? "This is the only match for " + searching_.text
                      : "No match for " + searching_.text + " on this track",
                   MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    if (mode_ != Mode::Visual)
    {
        jumpedFrom_ = before;
    }
    if (searching_.gnRun)
    {
        const auto status = view().matchStatus(*searching_.query);
        setMessage("Skipped to " + status + " · " + keysOf("repeat") + " changes it, " +
                       keysOf("search-next") + " skips again",
                   MessageKind::Information);
        searching_.gnKept = true;
    }
    return true;
}

bool Workspace::select(std::string_view command)
{
    // Lower case the bar, section, track or what's visible; the capitals (, s B) the matches
    // in it, a selection with the search as its filter (decided November 2026).
    const bool matches = command.starts_with("select-matches");
    if (matches && !searching_.query)
    {
        setMessage("No search to select the matches of: " + keysOf("search") + " searches first",
                   MessageKind::Information);
        return true;
    }
    if (command == "select-matches")
    {
        if (!selection_)
        {
            setMessage("Nothing selected to keep the matches in", MessageKind::Information);
            return true;
        }
        selection_->filter = searching_.query;
        return true;
    }
    const auto object = command.ends_with("bar")       ? "object-bar-contents"
                        : command.ends_with("section") ? "object-section-contents"
                        : command.ends_with("track")   ? "object-track"
                                                       : "object-visible";
    if (!selection_)
    {
        selection_ = Selection{.anchor = view().cursor(),
                               .kind = SelectionKind::Range,
                               .where = where(),
                               .index = view().selectionIndex(),
                               .span = std::nullopt,
                               .cut = false};
    }
    view().selectObject(object, 0, *selection_);
    selection_->filter = matches ? searching_.query : std::nullopt;
    mode_ = Mode::Visual;
    return true;
}

bool Workspace::onlyMatches(OperatorTarget& target)
{
    const auto& song = document_.song();
    std::size_t count = 0;
    for (auto& part : target.parts)
    {
        const auto& events = song.events(part.track);
        std::erase_if(part.events, [&](std::size_t i)
                      { return !core::matches(*selection_->filter, events[i].data, song.ppqn()); });
        count += part.events.size();
    }
    if (count == 0)
    {
        setMessage("No matches in the selection", MessageKind::Information);
        return false;
    }
    target.what = count == 1 ? std::string{"1 match"} : std::to_string(count) + " matches";
    return true;
}

bool Workspace::operateOnMatches(std::string_view op)
{
    // d and y on the matches in the selection: taken as a register holds them, from the
    // selection's start, as the selection would be, only fewer.
    auto target = view().target("visual", 0);
    if (!target || !onlyMatches(*target))
    {
        return true;
    }
    const auto span = view().selectionSpan();
    return takeTarget(op, *target, span ? span->first : 0, span ? span->second - span->first : 0);
}

bool Workspace::toMatchForGn()
{
    // The match at the cursor, unless gn has just changed it, or the next; whether there's one.
    if (!searching_.query)
    {
        setMessage("No search yet: " + keysOf("search") + " searches", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return false;
    }
    if (!view().matchesHere(*searching_.query) ||
        (searching_.gnLast && *searching_.gnLast == view().cursor()))
    {
        const auto before = view().cursor();
        goToMatch(true, 0);
        if (view().cursor() == before && searching_.gnLast && *searching_.gnLast == before)
        {
            return false; // the only match, done already
        }
    }
    return view().matchesHere(*searching_.query).has_value();
}

bool Workspace::operateOnMatch(std::string_view op, int amount)
{
    if (!toMatchForGn())
    {
        return true;
    }
    const auto part = *view().matchesHere(*searching_.query);
    const auto status = view().matchStatus(*searching_.query);
    const auto at = status.starts_with("match") ? status : std::string{"the match"};
    const auto count = part.events.size();
    OperatorTarget target{.parts = {part},
                          .what = count == 1 ? std::string{"1 match"}
                                             : std::to_string(count) + " matches",
                          .notesOnly = false};
    const auto here = view().cursor();
    const bool taking = op == "operator-delete" || op == "operator-yank";
    if (taking)
    {
        takeTarget(op, target, document_.song().events(part.track)[part.events.front()].tick, 0);
    }
    else
    {
        presetTarget_ = std::move(target);
        transform(op, amount, "object-match", 0);
        presetTarget_.reset();
    }
    if (op == "operator-yank")
    {
        setMessage("Yanked " + at, MessageKind::Information);
        return true;
    }
    searching_.gnLast = here; // a change, which . goes on from
    const auto done = (op == "operator-delete" ? "Deleted " : "Changed ") + at;
    // Then on to the next match, so it's seen before . changes it (decided November 2026).
    const auto remind = [this, done]
    {
        const auto from = view().cursor();
        view().run("cursor-match-next", 1);
        if (view().cursor() == from || !view().matchesHere(*searching_.query))
        {
            setMessage(done + " · no other match", MessageKind::Information);
            return;
        }
        if (wasPlaying_)
        {
            following_ = false;
        }
        setMessage(done + " · on to the next: " + keysOf("repeat") + " changes it, " +
                       keysOf("search-next") + " skips it",
                   MessageKind::Information);
        searching_.gnRun = true;
        searching_.gnKept = true;
    };
    if (prompt_.answer)
    {
        // As ^ asks how far: once answered.
        prompt_.answer = [inner = std::move(prompt_.answer), remind](const std::string& text)
        {
            inner(text);
            remind();
        };
        return true;
    }
    remind();
    return true;
}

bool Workspace::selectMatch()
{
    // gn without an operator selects the match, as in Vim, only it, by the search's filter.
    if (!toMatchForGn())
    {
        return true;
    }
    if (!selection_)
    {
        selection_ = Selection{.anchor = view().cursor(),
                               .kind = SelectionKind::Range,
                               .where = where(),
                               .index = view().selectionIndex(),
                               .span = std::nullopt,
                               .cut = false};
    }
    view().selectObject(where() == Where::Tracks ? "object-bar-contents" : "object-note", 0,
                        *selection_);
    selection_->filter = searching_.query;
    mode_ = Mode::Visual;
    setMessage("The match selected · an operator changes it", MessageKind::Information);
    return true;
}

void Workspace::searchWrapped(bool forward)
{
    setMessage(forward ? "Past the last match, round to the first"
                       : "Before the first match, round to the last",
               MessageKind::Information);
}

bool Workspace::startFind(bool forward, int count)
{
    // After a find, f and F go to its next and previous match, as Doom Emacs's evil-snipe does
    // (decided October 2026); otherwise, and after an operator, they take a note name.
    if (finding_.repeats && mode_ != Mode::OperatorPending)
    {
        if (!goToFound(forward, count))
        {
            macroFailed_ = macroDepth_ > 0;
        }
        return true;
    }
    awaiting_ = Awaiting{forward ? "find-next" : "find-previous", count};
    return true;
}

bool Workspace::operateToFound(bool forward, int count)
{
    if (!runOperator(forward ? "cursor-find-next" : "cursor-find-previous", count))
    {
        sayNotFound(forward);
        return false;
    }
    return true;
}

bool Workspace::nameFind(std::string_view command, char32_t letter, int count)
{
    // The letter, in either case (decided November 2026, as capitals aren't needed to tell
    // notes apart). The cursor goes at once, and again from where it was if a sharp or flat
    // follows. So does an operator (decided November 2026, after waiting for the key timeout
    // first was too hurried): done again over the new range, as one undo step, if a sharp or
    // flat follows, however long after.
    const bool forward = command == "find-next";
    const char32_t upper = letter >= U'a' && letter <= U'g' ? letter - U'a' + U'A' : letter;
    if (upper < U'A' || upper > U'G')
    {
        setMessage(keysOf(command) + " takes a note name after it, such as " + keysOf(command) +
                       "E, " + keysOf(command) + "F# or " + keysOf(command) + "Bb",
                   MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        if (operator_)
        {
            operator_.reset();
            mode_ = Mode::Normal;
        }
        return true;
    }
    finding_.naming =
        Naming{.letter = upper, .forward = forward, .count = count, .from = view().cursor()};
    finding_.query =
        core::EventQuery{.pitchClass = core::parsePitchClass(encodeUtf8(std::u32string(1, upper)))};
    finding_.name = encodeUtf8(std::u32string(1, upper));
    if (operator_)
    {
        finding_.naming->redo = Redo{.op = *operator_,
                                     .song = document_.song(),
                                     .unnamed = register_,
                                     .registers = registers_,
                                     .registerName = registerName_,
                                     .registerBefore = registerBefore_,
                                     .cut = cutNext_};
        document_.beginGroup({}, view().place());
        finding_.naming->failed = !operateToFound(forward, count);
        return true;
    }
    finding_.naming->failed = !goToFound(forward, count);
    return true;
}

bool Workspace::finishNaming(std::optional<char32_t> accidental)
{
    auto naming = *std::exchange(finding_.naming, std::nullopt);
    auto name = encodeUtf8(std::u32string(1, naming.letter));
    if (accidental)
    {
        name += static_cast<char>(*accidental);
    }
    finding_.query = core::EventQuery{.pitchClass = core::parsePitchClass(name)};
    finding_.name = name;
    if (naming.redo)
    {
        // The operator again, from the song, registers and cursor before it, in the undo group
        // it began, which then ends as one step.
        auto& redo = *naming.redo;
        if (accidental)
        {
            document_.edit({}, view().place(), [&](core::Song& song) { song = redo.song; });
            register_ = std::move(redo.unnamed);
            registers_ = std::move(redo.registers);
            registerName_ = redo.registerName;
            registerBefore_ = std::move(redo.registerBefore);
            cutNext_ = redo.cut;
            refresh();
            view().setCursor(naming.from);
            operator_ = redo.op;
            mode_ = Mode::OperatorPending;
            if (!operateToFound(naming.forward, naming.count))
            {
                macroFailed_ = macroDepth_ > 0;
            }
        }
        else if (naming.failed && macroDepth_ > 0)
        {
            macroFailed_ = true;
        }
        document_.endGroup();
        refresh();
        return true;
    }
    // Again from where the find started, for the sharp or flat.
    view().setCursor(naming.from);
    if (!goToFound(naming.forward, naming.count))
    {
        macroFailed_ = macroDepth_ > 0;
    }
    return true;
}

void Workspace::settleNaming()
{
    // A name with nothing after it: an operator's done as it is, and a find that found nothing
    // fails a macro now.
    if (!finding_.naming)
    {
        return;
    }
    if (finding_.naming->redo)
    {
        finishNaming(std::nullopt);
        return;
    }
    if (finding_.naming->failed && macroDepth_ > 0)
    {
        macroFailed_ = true;
    }
    finding_.naming.reset();
}

bool Workspace::goToFound(bool forward, int count)
{
    // Not a jump, as Vim's f isn't, so '' stays where it was.
    const auto command = forward ? "cursor-find-next" : "cursor-find-previous";
    const auto before = view().cursor();
    finding_.forward = forward;
    finding_.repeats = true;
    finding_.kept = true;
    const auto moved = view().run(command, count);
    if (!moved)
    {
        finding_.repeats = false;
        sayWhereFor(command);
        return false;
    }
    if (wasPlaying_)
    {
        following_ = false;
    }
    if (view().cursor() == before)
    {
        sayNotFound(forward);
        return false;
    }
    return true;
}

void Workspace::sayNotFound(bool forward)
{
    // "No E after here in Bass".
    const auto& song = document_.song();
    const auto cursor = view().cursor();
    const auto* track = cursor.track ? song.track(*cursor.track) : nullptr;
    std::string text = "No " + finding_.name + (forward ? " after here" : " before here");
    if (track != nullptr && !track->name.empty())
    {
        text += " in " + track->name;
    }
    setMessage(text, MessageKind::Information);
}

} // namespace modal::ui
