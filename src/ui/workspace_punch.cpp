// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <utility>

namespace modal::ui
{

// A punch range (decided 24 November 2026) is armed from the cursor's section, a Visual
// selection's time or two labels, and stays armed in its song until cleared, so R records
// another take of it. It follows what it was armed from through edits, and goes, saying why,
// where an edit leaves it nowhere.

std::optional<core::ArmedSpan> Workspace::punchSpan() const
{
    const auto& song = document_.song();
    const auto range = punchRanges_.find(song.identity());
    if (range == punchRanges_.end())
        return std::nullopt;
    return core::find(song, range->second).span;
}

std::string Workspace::armedRangeText(const core::ArmedRange& range, core::ArmedSpan span) const
{
    const auto& song = document_.song();
    const auto times =
        positionText(song.meter(), span.from) + " to " + positionText(song.meter(), span.to);
    if (std::holds_alternative<core::ArmedRange::OfSection>(range.source))
        return song.sections()[song.sectionAt(span.from)]->name() + ", " + times;
    if (const auto* labels = std::get_if<core::ArmedRange::OfLabels>(&range.source))
        return labels->from.name + " to " + labels->to.name + ", " + times;
    return times;
}

CommandResult Workspace::punch(std::string_view arguments)
{
    const auto& song = document_.song();
    if (arguments.find_first_not_of(" \t") == std::string_view::npos)
    {
        if (selection_)
        {
            const auto span = view().selectionSpan();
            if (!span || std::min(span->second, song.end()) <= span->first)
                return CommandResult::error("Select a span of time to punch in");
            return armPunch(
                core::ArmedRange::time(span->first, std::min(span->second, song.end())));
        }
        return armPunch(core::ArmedRange::section(song, song.sectionAt(view().cursor().tick)));
    }
    auto named = namedLabels(arguments, "punch", "punch-labels");
    if (auto* refused = std::get_if<CommandResult>(&named))
        return *refused;
    auto& labels = std::get<NamedLabels>(named);
    return labelsChosen(LabelPurpose::Punch, labels.from, labels.to, std::move(labels.names));
}

CommandResult Workspace::armPunch(core::ArmedRange range)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    const auto found = core::find(song, range);
    if (!found.span)
        return CommandResult::error("Nothing there to punch in");

    std::string replacing;
    if (const auto old = punchRanges_.find(song.identity()); old != punchRanges_.end())
    {
        if (const auto oldSpan = core::find(song, old->second).span;
            oldSpan && old->second != range)
            replacing = ", replacing " + armedRangeText(old->second, *oldSpan);
    }
    if (selection_)
        endVisual();
    punchRanges_.insert_or_assign(song.identity(), range);
    return {true, "Punch range: " + armedRangeText(range, *found.span) + replacing};
}

CommandResult Workspace::clearPunch()
{
    const auto& song = document_.song();
    const auto range = punchRanges_.find(song.identity());
    if (range == punchRanges_.end())
        return {false, "No punch range is armed", false};
    std::string was;
    if (const auto span = core::find(song, range->second).span)
        was = ": " + armedRangeText(range->second, *span);
    punchRanges_.erase(range);
    return {true, "Punch range cleared" + was};
}

void Workspace::notePunchRange()
{
    const auto& song = document_.song();
    const auto range = punchRanges_.find(song.identity());
    if (range == punchRanges_.end())
        return;
    const auto found = core::find(song, range->second);
    if (found.span)
        return;
    punchRanges_.erase(range);
    auto message = "Punch range cleared: " + found.gone;
    setMessage(message_.empty() ? std::move(message) : message_ + " · " + message,
               MessageKind::Warning);
}

} // namespace modal::ui
