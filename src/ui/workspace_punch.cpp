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
// where an edit leaves it nowhere. It's part of the song, saved with it, and arming or clearing
// it is an undo step (decided 25 November 2026).

std::optional<core::ArmedSpan> Workspace::punchSpan() const
{
    const auto& song = document_.song();
    if (!song.punch || !song.punchOn)
        return std::nullopt;
    return core::find(song, *song.punch).span;
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
    if (song.punch && *song.punch != range)
    {
        if (const auto oldSpan = core::find(song, *song.punch).span)
            replacing = ", replacing " + armedRangeText(*song.punch, *oldSpan);
    }
    auto text = "Punch range: " + armedRangeText(range, *found.span) + replacing;
    if (selection_)
        endVisual();
    document_.edit("Arm punch range", view().place(),
                   [&](core::Song& armed)
                   {
                       armed.punch = std::move(range);
                       armed.punchOn = true;
                   });
    return {true, std::move(text)};
}

CommandResult Workspace::togglePunch()
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    if (!song.punch)
        return {false, "No punch range is armed: " + keysOf("punch") + " arms one", false};
    const bool on = !song.punchOn;
    document_.edit(on ? "Punching on" : "Punching off", view().place(),
                   [&](core::Song& switched) { switched.punchOn = on; });
    return {true, on ? "Punching on: R records the punch range"
                     : "Punching off, the range kept: R records from the marker"};
}

CommandResult Workspace::clearPunch()
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    if (!song.punch)
        return {false, "No punch range is armed", false};
    std::string was;
    if (const auto span = core::find(song, *song.punch).span)
        was = ": " + armedRangeText(*song.punch, *span);
    document_.edit("Clear punch range", view().place(),
                   [](core::Song& cleared) { cleared.punch.reset(); });
    return {true, "Punch range cleared" + was};
}

void Workspace::noteRanges(std::uint64_t identity, const std::optional<core::ArmedRange>& punch,
                           const std::optional<core::ArmedRange>& loop)
{
    // The edit itself cleared one, in its undo step: say why, as cleared or undone deliberately
    // the range would still be somewhere.
    const auto& song = document_.song();
    if (song.identity() != identity)
        return;
    std::string message;
    const auto note = [&](const std::optional<core::ArmedRange>& before,
                          const std::optional<core::ArmedRange>& now, std::string_view name)
    {
        if (!before || now)
            return;
        const auto found = core::find(song, *before);
        if (!found.span)
            message +=
                (message.empty() ? "" : " · ") + std::string{name} + " cleared: " + found.gone;
    };
    note(punch, song.punch, "Punch range");
    note(loop, song.loop, "Loop");
    if (message.empty())
        return;
    setMessage(message_.empty() ? std::move(message) : message_ + " · " + message,
               MessageKind::Warning);
}

} // namespace modal::ui
