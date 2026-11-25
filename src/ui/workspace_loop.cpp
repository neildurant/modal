// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace modal::ui
{

std::vector<std::string> Workspace::loopLabelNames() const
{
    const auto& song = document_.song();
    std::map<std::string_view, std::size_t> counts;
    for (const auto& label : song.labels())
        ++counts[label.name];

    std::vector<std::string> names;
    for (const auto& label : song.labels())
    {
        if (counts.at(label.name) == 1)
        {
            names.push_back(label.name);
            continue;
        }
        const auto section = song.sectionAt(label.tick);
        names.push_back(song.sections()[section]->name() + " (" + std::to_string(section + 1) +
                        ") › " + label.name);
    }
    return names;
}

std::variant<Workspace::NamedLabels, CommandResult>
Workspace::namedLabels(std::string_view arguments, std::string_view command,
                       std::string_view chooser) const
{
    std::istringstream input{std::string{arguments}};
    std::string fromName;
    std::string toName;
    if (!(input >> std::quoted(fromName) >> std::quoted(toName)) || !(input >> std::ws).eof())
        return CommandResult::error("Give two labels to :" + std::string{command} +
                                    ", quoting names with spaces");

    const auto names = loopLabelNames();
    const auto labels = document_.song().labels();
    for (const auto& name : {fromName, toName})
    {
        if (std::ranges::count(names, name) != 1)
        {
            const auto count = std::ranges::count(labels, name, &core::Label::name);
            return CommandResult::error(count > 1 ? "Several labels are named " + name +
                                                        ": choose one with " + keysOf(chooser)
                                                  : "No label named " + name);
        }
    }
    const auto index = [&](const std::string& name)
    { return static_cast<std::size_t>(std::ranges::find(names, name) - names.begin()); };
    return NamedLabels{index(fromName), index(toName), {fromName, toName}};
}

CommandResult Workspace::loop(std::string_view arguments)
{
    const auto& song = document_.song();
    if (arguments.find_first_not_of(" 	") == std::string_view::npos)
    {
        if (selection_)
        {
            const auto span = view().selectionSpan();
            if (!span || std::min(span->second, song.end()) <= span->first)
                return CommandResult::error("Select a span of time to loop");
            return armLoop(core::ArmedRange::time(span->first, std::min(span->second, song.end())));
        }
        return armLoop(core::ArmedRange::section(song, song.sectionAt(view().cursor().tick)));
    }
    auto named = namedLabels(arguments, "loop", "loop-labels");
    if (auto* refused = std::get_if<CommandResult>(&named))
        return *refused;
    auto& labels = std::get<NamedLabels>(named);
    return labelsChosen(LabelPurpose::Loop, labels.from, labels.to, std::move(labels.names));
}

CommandResult Workspace::labelsChosen(LabelPurpose purpose, std::size_t from, std::size_t to,
                                      std::pair<std::string, std::string> names)
{
    const auto& song = document_.song();
    const auto labels = song.labels();
    const bool loop = purpose == LabelPurpose::Loop;
    auto result = labels[from].tick >= labels[to].tick
                      ? CommandResult::error(loop ? "The end of a loop must be after its start"
                                                  : "The end of a punch range must be after its "
                                                    "start")
                  : loop ? armLoop(core::ArmedRange::labels(song, from, to))
                         : armPunch(core::ArmedRange::labels(song, from, to));
    if (!result.refused)
        (purpose == LabelPurpose::Loop ? lastLoopLabels_ : lastPunchLabels_)[song.identity()] =
            std::move(names);
    return result;
}

// A loop (decided 25 November 2026) is armed as a punch range is, from the cursor's section, a
// Visual selection's time or two labels, saved with the song, and played by Space while on.
// Arming, clearing and switching it are undo steps.

std::optional<core::ArmedSpan> Workspace::loopSpan() const
{
    const auto& song = document_.song();
    if (!song.loop || !song.loopOn)
        return std::nullopt;
    return core::find(song, *song.loop).span;
}

core::ArmedSpans Workspace::armedRanges() const
{
    const auto& song = document_.song();
    return {punchSpan(), song.loop ? core::find(song, *song.loop).span : std::nullopt, song.loopOn};
}

CommandResult Workspace::armLoop(core::ArmedRange range)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    const auto found = core::find(song, range);
    if (!found.span)
        return CommandResult::error("Nothing there to loop");

    std::string replacing;
    if (song.loop && *song.loop != range)
    {
        if (const auto oldSpan = core::find(song, *song.loop).span)
            replacing = ", replacing " + armedRangeText(*song.loop, *oldSpan);
    }
    auto text = "Loop range: " + armedRangeText(range, *found.span) + replacing +
                (player_ != nullptr && player_->playing() ? "; the next Space plays it"
                                                          : "; Space plays it");
    if (selection_)
        endVisual();
    document_.edit("Arm loop", view().place(),
                   [&](core::Song& armed)
                   {
                       armed.loop = std::move(range);
                       armed.loopOn = true;
                   });
    return {true, std::move(text)};
}

CommandResult Workspace::clearLoop()
{
    const auto& song = document_.song();
    if (!song.loop)
        return {false, "No loop is armed", false};
    std::string was;
    if (const auto span = core::find(song, *song.loop).span)
        was = ": " + armedRangeText(*song.loop, *span);
    document_.edit("Clear loop", view().place(), [](core::Song& cleared) { cleared.loop.reset(); });
    return {true, "Loop cleared" + was};
}

CommandResult Workspace::toggleLoop()
{
    const auto& song = document_.song();
    if (!song.loop)
        return {false, "No loop is armed: " + keysOf("loop") + " arms one", false};
    const bool on = !song.loopOn;
    document_.edit(on ? "Looping on" : "Looping off", view().place(),
                   [&](core::Song& switched) { switched.loopOn = on; });
    return {true, on ? "Looping on: Space plays the loop"
                     : "Looping off, the loop kept: Space plays from the marker"};
}

bool Workspace::startPlaying()
{
    const auto& song = document_.song();
    const auto loop = loopSpan();
    if (!loop)
        return player_->play(song);
    if (!player_->playLoop(song, loop->from, loop->to))
        return false;
    setMessage("Looping " + armedRangeText(*song.loop, *loop), MessageKind::Information);
    return true;
}

CommandResult Workspace::chooseLoopLabel(std::optional<std::size_t> from, LabelPurpose purpose)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    const auto labels = song.labels();
    if (labels.size() < 2 || labels.front().tick == labels.back().tick)
        return CommandResult::error(purpose == LabelPurpose::Loop
                                        ? "Put labels at two different times to loop between them"
                                        : "Put labels at two different times to punch between "
                                          "them");

    const auto names = loopLabelNames();
    const auto& last = purpose == LabelPurpose::Loop ? lastLoopLabels_ : lastPunchLabels_;
    const auto previous = last.find(song.identity());
    std::vector<std::string> choices;
    std::vector<std::size_t> indices;
    std::size_t highlighted = 0;
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (from ? labels[i].tick <= labels[*from].tick : labels[i].tick >= labels.back().tick)
            continue;
        if (previous != last.end() &&
            names[i] == (from ? previous->second.second : previous->second.first))
            highlighted = choices.size();
        choices.push_back(names[i] + "  " + positionText(song.meter(), labels[i].tick));
        indices.push_back(i);
    }
    loopLabelChoice_.emplace(LoopLabelChoice{ChoiceList{std::move(choices), highlighted, true},
                                             std::move(indices), from, purpose});
    loopLabelChoice_->list.name("label");
    return {true};
}

bool Workspace::loopLabelKey(const Key& key)
{
    message_.clear();
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    auto& choice = *loopLabelChoice_;
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
        loopLabelChoice_.reset();
    else if (command == "command-line-run")
    {
        if (const auto selected = choice.list.chosen())
        {
            const auto label = choice.labels[*selected];
            const auto from = choice.from;
            const auto purpose = choice.purpose;
            loopLabelChoice_.reset();
            if (!from)
                report(chooseLoopLabel(label, purpose));
            else
            {
                const auto names = loopLabelNames();
                report(labelsChosen(purpose, *from, label, {names[*from], names[label]}));
            }
        }
    }
    else if (!choice.list.run(command) && key.named == NamedKey::None &&
             (key.modifiers & (kCtrl | kAlt)) == 0)
        choice.list.type(key.character);
    return true;
}

} // namespace modal::ui
