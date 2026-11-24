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

CommandResult Workspace::loop(std::string_view arguments)
{
    const auto& song = document_.song();
    std::istringstream input{std::string{arguments}};
    if ((input >> std::ws).eof())
    {
        if (selection_)
        {
            if (const auto span = view().selectionSpan())
                return startLoop(span->first, std::min(span->second, song.end()));
            return CommandResult::error("Select a span of time to loop");
        }
        const auto section = song.sectionAt(view().cursor().tick);
        return startLoop(song.sectionStart(section), song.sectionStart(section + 1));
    }

    std::string fromName;
    std::string toName;
    if (!(input >> std::quoted(fromName) >> std::quoted(toName)) || !(input >> std::ws).eof())
        return CommandResult::error("Give two labels to :loop, quoting names with spaces");

    const auto names = loopLabelNames();
    const auto labels = song.labels();
    const auto from = std::ranges::find(names, fromName);
    const auto to = std::ranges::find(names, toName);
    for (const auto& name : {fromName, toName})
    {
        if (std::ranges::count(names, name) != 1)
        {
            const auto count = std::ranges::count(labels, name, &core::Label::name);
            return CommandResult::error(count > 1 ? "Several labels are named " + name +
                                                        ": choose one with , l L"
                                                  : "No label named " + name);
        }
    }
    auto result = startLoop(labels[static_cast<std::size_t>(from - names.begin())].tick,
                            labels[static_cast<std::size_t>(to - names.begin())].tick);
    if (!result.refused)
        lastLoopLabels_[song.identity()] = {std::move(fromName), std::move(toName)};
    return result;
}

CommandResult Workspace::startLoop(core::Tick from, core::Tick to)
{
    if (from >= to)
        return CommandResult::error("The end of a loop must be after its start");
    if (player_ == nullptr)
        return CommandResult::error("Nothing to play to");
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    if (!player_->playLoop(document_.song(), from, to))
        return CommandResult::error("The engine didn't take that loop: try again");

    if (selection_)
        endVisual();
    playedFrom_ = view().cursor();
    following_ = true;
    returning_ = false;
    showPlayhead();
    const auto& meter = document_.song().meter();
    return {true, "Looping " + positionText(meter, from) + " to " + positionText(meter, to)};
}

CommandResult Workspace::chooseLoopLabel(std::optional<std::size_t> from)
{
    if (recording_.pending())
        return CommandResult::error("Recording: Space stops it first");
    const auto& song = document_.song();
    const auto labels = song.labels();
    if (labels.size() < 2 || labels.front().tick == labels.back().tick)
        return CommandResult::error("Put labels at two different times to loop between them");

    const auto names = loopLabelNames();
    const auto previous = lastLoopLabels_.find(song.identity());
    std::vector<std::string> choices;
    std::vector<std::size_t> indices;
    std::size_t highlighted = 0;
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (from ? labels[i].tick <= labels[*from].tick : labels[i].tick >= labels.back().tick)
            continue;
        if (previous != lastLoopLabels_.end() &&
            names[i] == (from ? previous->second.second : previous->second.first))
            highlighted = choices.size();
        choices.push_back(names[i] + "  " + positionText(song.meter(), labels[i].tick));
        indices.push_back(i);
    }
    loopLabelChoice_.emplace(LoopLabelChoice{ChoiceList{std::move(choices), highlighted, true},
                                             std::move(indices), from});
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
            loopLabelChoice_.reset();
            if (!from)
                report(chooseLoopLabel(label));
            else
            {
                const auto& song = document_.song();
                const auto labels = song.labels();
                const auto result = startLoop(labels[*from].tick, labels[label].tick);
                if (!result.refused)
                {
                    const auto names = loopLabelNames();
                    lastLoopLabels_[song.identity()] = {names[*from], names[label]};
                }
                report(result);
            }
        }
    }
    else if (!choice.list.run(command) && key.named == NamedKey::None &&
             (key.modifiers & (kCtrl | kAlt)) == 0)
        choice.list.type(key.character);
    return true;
}

} // namespace modal::ui
